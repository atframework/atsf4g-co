// Copyright 2024 atframework
// Created by owent

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  include <WinSock2.h>
#endif

#include "logic/hpa/logic_hpa_policy.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/common/key_value_iterable_view.h>
#include <opentelemetry/exporters/prometheus/exporter_utils.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/version.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <memory/object_allocator.h>

#include <rpc/telemetry/exporter/prometheus_utility.h>
#include <rpc/telemetry/opentelemetry_utility.h>
#include <rpc/telemetry/rpc_global_service.h>
#include <rpc/telemetry/semantic_conventions.h>

#include <utility/protobuf_mini_dumper.h>

#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>

#include "logic/hpa/logic_hpa_controller.h"
#include "logic/hpa/logic_hpa_observer.h"
#include "logic/hpa/logic_hpa_puller.h"

namespace {

/**
 * @brief 用于实现 otel-cpp 多容器接入KeyValueIterable时，不需要拷贝数据
 *
 */
class UTIL_SYMBOL_LOCAL logic_hpa_policy_local_key_value_view final : public opentelemetry::common::KeyValueIterable {
 public:
  explicit logic_hpa_policy_local_key_value_view(
      const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& shared,
      const std::unordered_map<std::string, std::string>& priv) noexcept
      : shared_{&shared}, privated_{&priv} {
    cached_size_ = privated_->size();

    for (auto& kv : *shared_) {
      if (privated_->end() == privated_->find(kv.first)) {
        ++cached_size_;
      }
    }
  }

  // KeyValueIterable
  bool ForEachKeyValue(
      opentelemetry::nostd::function_ref<bool(opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue)>
          callback) const noexcept override {
    for (auto& kv : *privated_) {
      if (!callback(kv.first, kv.second)) {
        return false;
      }
    }

    for (auto& kv : *shared_) {
      if (privated_->end() == privated_->find(kv.first)) {
        if (!callback(kv.first, kv.second)) {
          return false;
        }
      }
    }
    return true;
  }

  size_t size() const noexcept override { return cached_size_; }

 private:
  size_t cached_size_;
  const std::unordered_map<std::string, opentelemetry::common::AttributeValue>* shared_;
  const std::unordered_map<std::string, std::string>* privated_;
};

// Prometheus label value 转义
static std::string convert_to_prometheus_label_value(const std::string& value) {
  std::stringstream out;
  for (const auto& c : value) {
    switch (c) {
      case '\n':
        out << '\\' << 'n';
        break;

      case '\\':
        out << '\\' << c;
        break;

      case '"':
        out << '\\' << c;
        break;

      default:
        out << c;
        break;
    }
  }
  return out.str();
}

// Prometheus 时间单位转义
static std::string convert_to_prometheus_duration_value(const google::protobuf::Duration& value) {
  int64_t duration_sec = value.seconds();
  if (duration_sec <= 0) {
    return "0s";
  }
  if (duration_sec % 3600 == 0) {
    return util::log::format("{}h", duration_sec / 3600);
  } else if (duration_sec % 60 == 0) {
    return util::log::format("{}m", duration_sec / 60);
  } else {
    return util::log::format("{}s", duration_sec);
  }
}

static std::string build_attribute_hint(
    std::unordered_map<std::string, std::string>& stored,
    gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (attributes.empty()) {
    return {};
  }

  for (auto& kv : attributes) {
    stored[rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
        static_cast<std::string>(kv.first), true)] =
        rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(kv.second);
  }

  std::stringstream ss;
  ss << ", attribtes: ";
  bool first_attribute = true;
  for (auto& kv : stored) {
    if (!first_attribute) {
      ss << ", ";
    } else {
      first_attribute = false;
    }
    ss << kv.first << "=\"" << convert_to_prometheus_label_value(kv.second) << "\"";
  }

  return ss.str();
}
}  // namespace

struct logic_hpa_policy::observable_callback_int64_data {
  observable_callback_set<observable_callback_int64>* owner = nullptr;
  std::list<observable_callback_data<observable_callback_int64>>::iterator iterator;
  int64_t version = 0;

  observable_callback_int64_data() {}
};

struct logic_hpa_policy::observable_callback_double_data {
  observable_callback_set<observable_callback_double>* owner = nullptr;
  std::list<observable_callback_data<observable_callback_double>>::iterator iterator;
  int64_t version = 0;

  observable_callback_double_data() {}
};

struct logic_hpa_policy::observable_callback_custom_data {
  observable_callback_set<observable_callback_custom>* owner = nullptr;
  std::list<observable_callback_data<observable_callback_custom>>::iterator iterator;
  int64_t version = 0;

  observable_callback_custom_data() {}
};

SERVER_FRAME_API logic_hpa_policy::logic_hpa_policy(
    logic_hpa_controller& controller, const std::shared_ptr<rpc::telemetry::group_type>& telemetry_group,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& hpa_cfg,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg,
    const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& common_attributes_reference,
    const std::unordered_map<std::string, std::string>& common_selectors)
    : controller_(&controller),
      stoping_(false),
      ready_(false),
      private_data_(nullptr),
      telemetry_group_(telemetry_group),
      pull_aggregation_type_(policy_cfg.aggregation()),
      pull_range_reduce_type_(logic_hpa_range_reduce_type::kLast),
      pull_metrics_name_mode_(logic_hpa_pull_metrics_name_mode::kWithNameAndUnit),
      scaling_up_value_(policy_cfg.scaling_up_value()),
      scaling_down_value_(policy_cfg.scaling_down_value()),
      async_instrument_callback_int64_(nullptr),
      async_instrument_callback_double_(nullptr),
      async_instrument_callback_custom_(nullptr) {
  event_on_pull_range_callback_list_.version = 1;
  event_on_pull_instant_callback_list_.version = 1;
  observable_callback_int64_.version = 1;
  observable_callback_double_.version = 1;
  observable_callback_custom_.version = 1;

  metrics_name_ = policy_cfg.metrics_name();
  metrics_unit_ = policy_cfg.metrics_unit();
  metrics_description_ = policy_cfg.metrics_description();
  metrics_type_ = policy_cfg.metrics_type();

  next_pull_timepoint_ = std::chrono::system_clock::from_time_t(0);
  pull_interval_ = protobuf_to_chrono_duration<>(hpa_cfg.metrics().pull_interval());
  if (pull_interval_ < std::chrono::seconds{1}) {
    pull_interval_ = std::chrono::seconds{60};
  }
  retry_interval_ = protobuf_to_chrono_duration<>(hpa_cfg.metrics().pull_retry_interval());
  if (retry_interval_ < pull_interval_) {
    retry_interval_ = pull_interval_;
  }
  switch (hpa_cfg.metrics().pull_metrics_name_mode()) {
    case PROJECT_NAMESPACE_ID::config::EN_LOGIC_HPA_PULL_METRICS_NAME_WITH_NAME_ONLY: {
      pull_metrics_name_mode_ = logic_hpa_pull_metrics_name_mode::kWithNameOnly;
      break;
    }
    default:
      break;
  }

  // 生成labels
  attributes_lifetime_.reserve(common_attributes_reference.size() + static_cast<size_t>(policy_cfg.labels().size()));
  attributes_reference_.reserve(common_attributes_reference.size() + static_cast<size_t>(policy_cfg.labels().size()));
  for (auto& kv : common_attributes_reference) {
    auto& attribute_with_lifetime = attributes_lifetime_[kv.first];
    attribute_with_lifetime = rpc::telemetry::opentelemetry_utility::convert_attribute_value_to_string(kv.second);
    attributes_reference_[kv.first] = attribute_with_lifetime;
  }
  for (auto& kv : policy_cfg.labels()) {
    std::string sanitize_key =
        rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(kv.first, true);
    attributes_lifetime_[sanitize_key] = kv.second;
    attributes_reference_[sanitize_key] = attributes_lifetime_[sanitize_key];
  }

  // 生成selectors
  std::unordered_set<std::string> ignore_selectors;
  ignore_selectors.reserve(static_cast<size_t>(policy_cfg.without_auto_selectors_size()));
  for (auto& ignore_selector : policy_cfg.without_auto_selectors()) {
    ignore_selectors.insert(
        rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(ignore_selector, true));
  }
  size_t selectors_size = static_cast<size_t>(policy_cfg.selectors_size());
  selectors_.reserve(common_selectors.size() + selectors_size);
  for (auto& kv : common_selectors) {
    if (ignore_selectors.end() != ignore_selectors.find(kv.first)) {
      continue;
    }

    selectors_[kv.first] = convert_to_prometheus_label_value(kv.second);
  }
  for (auto& kv : policy_cfg.selectors()) {
    std::string sanitize_key =
        rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(kv.first, true);
    // 如果没有显式设置且要忽略则跳过
    if (ignore_selectors.end() != ignore_selectors.find(sanitize_key) &&
        attributes_lifetime_.end() != attributes_lifetime_.find(sanitize_key)) {
      continue;
    }

    if (ignore_selectors.end() != ignore_selectors.find(kv.first) &&
        attributes_lifetime_.end() != attributes_lifetime_.find(kv.first)) {
      continue;
    }

    selectors_[sanitize_key] = convert_to_prometheus_label_value(kv.second);
  }

  // 生成query
  make_query(policy_cfg);

  // 创建 puller
  puller_ = logic_hpa_puller_factory::make_new_instance(*this, telemetry_group, hpa_cfg, policy_cfg);
}

SERVER_FRAME_API logic_hpa_policy::~logic_hpa_policy() {
  puller_.reset();

  reset_instrument();
}

SERVER_FRAME_API const std::string& logic_hpa_policy::get_metrics_name() const noexcept { return metrics_name_; }

SERVER_FRAME_API const std::string& logic_hpa_policy::get_metrics_unit() const noexcept { return metrics_unit_; }

SERVER_FRAME_API const std::string& logic_hpa_policy::get_metrics_description() const noexcept {
  return metrics_description_;
}

SERVER_FRAME_API const std::string& logic_hpa_policy::get_query() const noexcept { return pull_query_; }

SERVER_FRAME_API int logic_hpa_policy::tick() {
  if (stoping_) {
    return 0;
  }

  int ret = 0;
  auto now = util::time::time_utility::sys_now();

  if (puller_) {
    int res = puller_->tick(now);
    if (res > 0) {
      ret += res;
    }
  }

  // 定期拉取任务
  if (now > next_pull_timepoint_) {
    bool pulling_available = is_pulling_available();
    if (pulling_available && !is_stoping()) {
      if (puller_ && !puller_->is_pulling()) {
        if (puller_->do_pull()) {
          next_pull_timepoint_ = now + pull_interval_;
        } else {
          next_pull_timepoint_ = now + retry_interval_;
        }
      }
    } else {
      next_pull_timepoint_ = now + pull_interval_;
    }
  }

  return ret;
}

SERVER_FRAME_API void logic_hpa_policy::stop() {
  if (stoping_) {
    return;
  }
  stoping_ = true;

  if (puller_) {
    puller_->stop();
  }
  reset_instrument();
}

SERVER_FRAME_API void logic_hpa_policy::reset_instrument() {
  reset_instrument_int64();
  reset_instrument_double();
  reset_instrument_custom();
}

void logic_hpa_policy::reset_instrument_int64() {
  if (async_instruments_int64_ && nullptr != async_instrument_callback_int64_) {
    FWLOGINFO("[HPA]: Policy {} cleanup int64 observer", metrics_name_);
    async_instruments_int64_->RemoveCallback(async_instrument_callback_int64_, this);
    async_instrument_callback_int64_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }

  std::list<observable_callback_data<observable_callback_int64>> cleanup_callback_int64;
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    cleanup_callback_int64.swap(observable_callback_int64_.observable);
  }
  ++observable_callback_int64_.version;
}

void logic_hpa_policy::reset_instrument_double() {
  if (async_instruments_double_ && nullptr != async_instrument_callback_double_) {
    FWLOGINFO("[HPA]: Policy {} cleanup double observer", metrics_name_);
    async_instruments_double_->RemoveCallback(async_instrument_callback_double_, this);
    async_instrument_callback_double_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }

  std::list<observable_callback_data<observable_callback_double>> cleanup_callback_double;
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    cleanup_callback_double.swap(observable_callback_double_.observable);
  }
  ++observable_callback_double_.version;
}

void logic_hpa_policy::reset_instrument_custom() {
  if (async_instruments_custom_ && nullptr != async_instrument_callback_custom_) {
    FWLOGINFO("[HPA]: Policy {} cleanup custom observer", metrics_name_);
    async_instruments_custom_->RemoveCallback(async_instrument_callback_custom_, this);
    async_instruments_custom_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }

  std::list<observable_callback_data<observable_callback_custom>> cleanup_callback_custom;
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    cleanup_callback_custom.swap(observable_callback_custom_.observable);
  }
  ++observable_callback_custom_.version;
}

void logic_hpa_policy::make_query(const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (!policy_cfg.query().empty()) {
    pull_query_ = policy_cfg.query();
    return;
  }

  std::list<gsl::string_view> query_slice;

  // aggregation [by|without ...(<label list>)] (simple_functions(<metrics_name>{selectors})...)
  make_query_metrics_name(query_slice);
  make_query_selectors(query_slice);
  make_query_simple_function(query_slice, policy_cfg);
  make_query_aggregation_operator(query_slice, policy_cfg);

  std::stringstream ss;
  for (auto& slice : query_slice) {
    ss.write(slice.data(), static_cast<std::streamsize>(slice.size()));
  }
  pull_query_ = ss.str();
}

void logic_hpa_policy::make_query_metrics_name(std::list<gsl::string_view>& query_slice) {
  // 指标名, 名字转换规则不同版本有变化，我们采用测试的方式获取实际的指标名

  opentelemetry::sdk::metrics::ResourceMetrics fake_resource_metrics;
  opentelemetry::sdk::metrics::MetricData fake_metrics_data;
  opentelemetry::sdk::metrics::PointDataAttributes fake_point_data;
  auto fake_scope = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("none");
  fake_metrics_data.aggregation_temporality = opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  fake_metrics_data.instrument_descriptor.name_ = metrics_name_;
  fake_metrics_data.instrument_descriptor.description_ = metrics_description_;
  fake_metrics_data.instrument_descriptor.unit_ = metrics_unit_;
  fake_metrics_data.instrument_descriptor.value_type_ = opentelemetry::sdk::metrics::InstrumentValueType::kLong;

  // @see OtlpMetricUtils::GetAggregationType in otel-cpp
  switch (metrics_type_) {
    case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_METRICS_TYPE_COUNTER: {
      fake_metrics_data.instrument_descriptor.type_ = opentelemetry::sdk::metrics::InstrumentType::kObservableCounter;
      fake_point_data.point_data = opentelemetry::sdk::metrics::SumPointData{};
      break;
    }
    default: {
      fake_metrics_data.instrument_descriptor.type_ = opentelemetry::sdk::metrics::InstrumentType::kObservableGauge;
      fake_point_data.point_data = opentelemetry::sdk::metrics::LastValuePointData{};
      break;
    }
  }
  fake_metrics_data.point_data_attr_.push_back(fake_point_data);
#if OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR >= 1012
  fake_resource_metrics.scope_metric_data_.push_back(
      {fake_scope.get(), std::vector<opentelemetry::sdk::metrics::MetricData>{fake_metrics_data}});
#else
  fake_resource_metrics.scope_metric_data_.push_back({fake_scope.get(), {fake_metrics_data}});
#endif
#if OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR < 1012
  auto prometheus_family =
      opentelemetry::exporter::metrics::PrometheusExporterUtils::TranslateToPrometheus(fake_resource_metrics);
#else
  auto prometheus_family =
      opentelemetry::exporter::metrics::PrometheusExporterUtils::TranslateToPrometheus(fake_resource_metrics, false);
#endif

  if (!prometheus_family.empty()) {
    pull_metrics_name_ = (*prometheus_family.rbegin()).name;
  }

  // Fallback
  if (pull_metrics_name_.empty()) {
#if OPENTELEMETRY_VERSION_MAJOR * 1000 + OPENTELEMETRY_VERSION_MINOR <= 1012
    if (pull_metrics_name_mode_ == logic_hpa_pull_metrics_name_mode::kWithNameOnly) {
      pull_metrics_name_ =
          rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(metrics_name_, true);
    } else {
      prometheus_metrics_name_ =
          rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(metrics_name_, true) + "_" +
          metrics_unit_;
    }
#else
    if (metrics_unit_.empty() || pull_metrics_name_mode_ == logic_hpa_pull_metrics_name_mode::kWithNameOnly) {
      pull_metrics_name_ =
          rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(metrics_name_, true);
    } else {
      pull_metrics_name_ =
          rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(metrics_name_, true) + "_" +
          metrics_unit_;
    }
#endif
  }

  query_slice.push_back(pull_metrics_name_);
}

void logic_hpa_policy::make_query_selectors(std::list<gsl::string_view>& query_slice) {
  if (selectors_.empty()) {
    return;
  }
  const char* begin = "{";
  for (auto& kv : selectors_) {
    query_slice.push_back(begin);
    begin = ",";

    query_slice.push_back(kv.first);
    query_slice.push_back("=\"");
    query_slice.push_back(kv.second);
    query_slice.push_back("\"");
  }
  query_slice.push_back("}");
}

void logic_hpa_policy::make_query_simple_function(std::list<gsl::string_view>& query_slice,
                                                  const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  // 封装一些常用的简单数据操作函数
  // @see https://prometheus.io/docs/prometheus/latest/querying/functions/
  for (auto& func : policy_cfg.simple_function()) {
    switch (func.function_type_case()) {
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kRate: {
        if (func.rate().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.rate()));

        query_slice.push_front("rate(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kIncrease: {
        if (func.increase().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.increase()));

        query_slice.push_front("increase(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kIrate: {
        if (func.irate().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.irate()));

        query_slice.push_front("irate(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kAvgOverTime: {
        if (func.avg_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.avg_over_time()));

        query_slice.push_front("avg_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kMinOverTime: {
        if (func.min_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.min_over_time()));

        query_slice.push_front("min_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kMaxOverTime: {
        if (func.max_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.max_over_time()));

        query_slice.push_front("max_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kSumOverTime: {
        if (func.sum_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.sum_over_time()));

        query_slice.push_front("sum_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kCountOverTime: {
        if (func.count_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.count_over_time()));

        query_slice.push_front("count_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::logic_hpa_policy_simple_function::kLastOverTime: {
        if (func.last_over_time().seconds() <= 0) {
          break;
        }
        simple_function_parameters_.push_back(convert_to_prometheus_duration_value(func.last_over_time()));

        query_slice.push_front("last_over_time(");
        query_slice.push_back("[");
        query_slice.push_back(*simple_function_parameters_.rbegin());
        query_slice.push_back("])");
        break;
      }
      default:
        break;
    }
  }
}

void logic_hpa_policy::make_query_aggregation_operator(
    std::list<gsl::string_view>& query_slice, const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& policy_cfg) {
  if (pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_NONE) {
    return;
  }

  std::list<gsl::string_view> aggregation_slice;
  // 封装一些常用的简单数据聚合函数
  // @see https://prometheus.io/docs/prometheus/latest/querying/operators/#aggregation-operators
  if (pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_SUM ||
      pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_AVG ||
      pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_COUNT ||
      pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_MIN ||
      pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_MAX) {
    switch (pull_aggregation_type_) {
      case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_SUM: {
        aggregation_slice.push_back("sum");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_AVG: {
        aggregation_slice.push_back("avg");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_COUNT: {
        aggregation_slice.push_back("count");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_MIN: {
        aggregation_slice.push_back("min");
        break;
      }
      case PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_MAX: {
        aggregation_slice.push_back("max");
        break;
      }
      default:
        return;
    }

    const char* begin = "";
    if (policy_cfg.aggregation_parameter().has_by()) {
      begin = " by (";
      for (auto& label : policy_cfg.aggregation_parameter().by().labels()) {
        if (label.empty()) {
          continue;
        }
        pull_aggregation_parameters_.push_back(
            rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(label, true));
      }
    } else if (policy_cfg.aggregation_parameter().has_without()) {
      begin = " without (";
      for (auto& label : policy_cfg.aggregation_parameter().without().labels()) {
        if (label.empty()) {
          continue;
        }
        pull_aggregation_parameters_.push_back(
            rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(label, true));
      }
    }
    if (!pull_aggregation_parameters_.empty()) {
      for (auto& label : pull_aggregation_parameters_) {
        aggregation_slice.push_back(begin);
        begin = ",";
        aggregation_slice.push_back(label);
      }
      aggregation_slice.push_back(")");
    }

    if (!aggregation_slice.empty()) {
      aggregation_slice.push_back(" (");
      query_slice.insert(query_slice.begin(), aggregation_slice.begin(), aggregation_slice.end());
      query_slice.push_back(")");
    }
    return;
  }

  if (pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_COUNT_VALUES) {
    if (!policy_cfg.aggregation_parameter().has_count_values() ||
        policy_cfg.aggregation_parameter().count_values().as().empty()) {
      return;
    }

    pull_aggregation_parameters_.push_back(rpc::telemetry::exporter::metrics::PrometheusUtility::SanitizePrometheusName(
        policy_cfg.aggregation_parameter().count_values().as(), true));

    query_slice.push_front("\", ");
    query_slice.push_front(*pull_aggregation_parameters_.rbegin());
    query_slice.push_front("count_values(\"");
    query_slice.push_back(")");
    return;
  }

  if (pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_TOPK ||
      pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_BOTTOMK) {
    if (policy_cfg.aggregation_parameter().k() <= 0) {
      return;
    }

    pull_aggregation_parameters_.push_back(util::log::format("{}", policy_cfg.aggregation_parameter().k()));

    query_slice.push_front(", ");
    query_slice.push_front(*pull_aggregation_parameters_.rbegin());
    if (pull_aggregation_type_ == PROJECT_NAMESPACE_ID::config::EN_HPA_POLICY_AGGREGATION_TOPK) {
      query_slice.push_front("topk(");
    } else {
      query_slice.push_front("bottomk(");
    }
    query_slice.push_back(")");
    return;
  }
}

void logic_hpa_policy::do_ready() {
  if (ready_) {
    return;
  }

  ready_ = true;

  FWLOGDEBUG("[HPA]: Policy {} get ready", metrics_name_);

  // 可能在回调中调用 clear_event_on_ready(), 此时要强行退出
  auto current_clear_version = event_on_ready_callback_.version;

  // 允许在回调中继续追加事件监听，和删除当前回调的事件
  for (auto iter = event_on_ready_callback_.callbacks.begin(); iter != event_on_ready_callback_.callbacks.end();) {
    auto current_iter = iter++;

    if ((*current_iter).active != logic_hpa_event_active_type::kUnactive && (*current_iter).callback) {
      (*current_iter).callback(*this);
    }

    // 在回调中调用 clear_event_on_ready(), 此时要强行退出
    if (current_clear_version != event_on_ready_callback_.version) {
      break;
    }
  }
}

SERVER_FRAME_API logic_hpa_policy::observable_callback_int64_handle logic_hpa_policy::add_observer_int64(
    observable_callback_int64 fn,
    gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (!fn) {
    return nullptr;
  }

  if (!telemetry_group_) {
    return nullptr;
  }

  if (stoping_) {
    return nullptr;
  }

  observable_callback_int64_handle ret = atfw::memory::stl::make_shared<observable_callback_int64_data>();
  if (!ret) {
    return ret;
  }

  // 注册回调列表
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    ret->owner = &observable_callback_int64_;
    ret->version = observable_callback_int64_.version;
    ret->iterator = observable_callback_int64_.observable.insert(observable_callback_int64_.observable.end(),
                                                                 observable_callback_data<observable_callback_int64>());

    observable_callback_data<observable_callback_int64>& new_callback = *ret->iterator;
    new_callback.callback = std::move(fn);

    std::string attribute_hint = build_attribute_hint(new_callback.attributes, attributes);

    FWLOGINFO("[HPA]: Policy {} setup int64 observer{}", metrics_name_, attribute_hint);

    if (!async_instruments_int64_) {
      async_instruments_int64_ = rpc::telemetry::global_service::mutable_metrics_observable_gauge_int64(
          "hpa", {metrics_name_, metrics_description_, metrics_unit_}, telemetry_group_);
    }

    if (!async_instruments_int64_) {
      FWLOGERROR("[HPA]: Policy {} create async instrument for int64 observer failed", metrics_name_);
      observable_callback_int64_.observable.pop_back();
      return nullptr;
    }
  }

  using otel_observer_result_int64 = opentelemetry::metrics::ObserverResultT<int64_t>;
  using otel_observer_result_double = opentelemetry::metrics::ObserverResultT<double>;

  // 没注册过回调到otel-cpp，需要注册回调一次
  // 后续的增加和删除只是在本地的回调列表中增加和删除，不影响otel-cpp的入口
  if (nullptr == async_instrument_callback_int64_) {
    async_instrument_callback_int64_ = [](opentelemetry::metrics::ObserverResult result, void* raw) {
      logic_hpa_policy* self = reinterpret_cast<logic_hpa_policy*>(raw);
      if (nullptr == self) {
        return;
      }

      if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(
              result)) {
        auto type_result =
            opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(result);
        if (type_result) {
          std::lock_guard<std::recursive_mutex> lock_guard{self->metrics_resource_lock_};
          for (auto iter = self->observable_callback_int64_.observable.begin();
               iter != self->observable_callback_int64_.observable.end();) {
            auto current_iter = iter++;
            auto result_value = current_iter->callback(*self);
            if (logic_hpa_observable_value::is_nan(result_value)) {
              continue;
            }
            type_result->Observe(result_value, logic_hpa_policy_local_key_value_view{self->attributes_reference_,
                                                                                     current_iter->attributes});
          }
        }
      } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(
                     result)) {
        auto type_result =
            opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(result);
        if (type_result) {
          std::lock_guard<std::recursive_mutex> lock_guard{self->metrics_resource_lock_};
          for (auto iter = self->observable_callback_int64_.observable.begin();
               iter != self->observable_callback_int64_.observable.end();) {
            auto current_iter = iter++;
            auto result_value = current_iter->callback(*self);
            if (logic_hpa_observable_value::is_nan(result_value)) {
              continue;
            }
            type_result->Observe(
                static_cast<double>(result_value),
                logic_hpa_policy_local_key_value_view{self->attributes_reference_, current_iter->attributes});
          }
        }
      }

      if (!self->ready_ && !self->is_pulling_available()) {
        self->do_ready();
      }
    };
    async_instruments_int64_->AddCallback(async_instrument_callback_int64_, this);
  }

  return ret;
}

SERVER_FRAME_API bool logic_hpa_policy::remove_observer_int64(observable_callback_int64_handle& handle) {
  if (!handle) {
    return false;
  }

  if (handle->owner != &observable_callback_int64_) {
    return false;
  }

  // 版本不一致则是执行过clear，直接忽略即可
  if (handle->version != observable_callback_int64_.version) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
  if (handle->iterator == observable_callback_int64_.observable.end()) {
    handle->version = 0;
    handle->owner = nullptr;
    return false;
  }

  // 删除内置的回调列表
  observable_callback_int64_.observable.erase(handle->iterator);
  handle->iterator = observable_callback_int64_.observable.end();
  handle->version = 0;
  handle->owner = nullptr;

  // 如果内置的回调列表一清空，则可以清空otel-cpp的回调注册
  if (observable_callback_int64_.observable.empty() && async_instruments_int64_ &&
      nullptr != async_instrument_callback_int64_) {
    FWLOGINFO("[HPA]: Policy {} cleanup int64 observer", metrics_name_);
    async_instruments_int64_->RemoveCallback(async_instrument_callback_int64_, this);
    async_instrument_callback_int64_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }
  return true;
}

SERVER_FRAME_API logic_hpa_policy::observable_callback_double_handle logic_hpa_policy::add_observer_double(
    observable_callback_double fn,
    gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (!fn) {
    return nullptr;
  }

  if (!telemetry_group_) {
    return nullptr;
  }

  if (stoping_) {
    return nullptr;
  }

  observable_callback_double_handle ret = atfw::memory::stl::make_shared<observable_callback_double_data>();
  if (!ret) {
    return ret;
  }

  // 注册回调列表
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    ret->owner = &observable_callback_double_;
    ret->version = observable_callback_double_.version;
    ret->iterator = observable_callback_double_.observable.insert(
        observable_callback_double_.observable.end(), observable_callback_data<observable_callback_double>());

    observable_callback_data<observable_callback_double>& new_callback = *ret->iterator;
    new_callback.callback = std::move(fn);

    std::string attribute_hint = build_attribute_hint(new_callback.attributes, attributes);

    FWLOGINFO("[HPA]: Policy {} setup double observer{}", metrics_name_, attribute_hint);

    if (!async_instruments_double_) {
      async_instruments_double_ = rpc::telemetry::global_service::mutable_metrics_observable_gauge_double(
          "hpa", {metrics_name_, metrics_description_, metrics_unit_}, telemetry_group_);
    }

    if (!async_instruments_double_) {
      FWLOGERROR("[HPA]: Policy {} create async instrument for double observer failed", metrics_name_);
      observable_callback_double_.observable.pop_back();
      return nullptr;
    }
  }

  using otel_observer_result_int64 = opentelemetry::metrics::ObserverResultT<int64_t>;
  using otel_observer_result_double = opentelemetry::metrics::ObserverResultT<double>;

  // 没注册过回调到otel-cpp，需要注册回调一次
  // 后续的增加和删除只是在本地的回调列表中增加和删除，不影响otel-cpp的入口
  if (nullptr == async_instrument_callback_double_) {
    async_instrument_callback_double_ = [](opentelemetry::metrics::ObserverResult result, void* raw) {
      logic_hpa_policy* self = reinterpret_cast<logic_hpa_policy*>(raw);
      if (nullptr == self) {
        return;
      }

      if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(
              result)) {
        auto type_result =
            opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(result);
        if (type_result) {
          std::lock_guard<std::recursive_mutex> lock_guard{self->metrics_resource_lock_};
          for (auto iter = self->observable_callback_double_.observable.begin();
               iter != self->observable_callback_double_.observable.end();) {
            auto current_iter = iter++;
            auto result_value = current_iter->callback(*self);
            if (logic_hpa_observable_value::is_nan(result_value)) {
              continue;
            }
            type_result->Observe(
                static_cast<int64_t>(result_value + std::numeric_limits<float>::epsilon()),
                logic_hpa_policy_local_key_value_view{self->attributes_reference_, current_iter->attributes});
          }
        }
      } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(
                     result)) {
        auto type_result =
            opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(result);
        if (type_result) {
          std::lock_guard<std::recursive_mutex> lock_guard{self->metrics_resource_lock_};
          for (auto iter = self->observable_callback_double_.observable.begin();
               iter != self->observable_callback_double_.observable.end();) {
            auto current_iter = iter++;
            auto result_value = current_iter->callback(*self);
            if (logic_hpa_observable_value::is_nan(result_value)) {
              continue;
            }
            type_result->Observe(result_value, logic_hpa_policy_local_key_value_view{self->attributes_reference_,
                                                                                     current_iter->attributes});
          }
        }
      }

      if (!self->ready_ && !self->is_pulling_available()) {
        self->do_ready();
      }
    };
    async_instruments_double_->AddCallback(async_instrument_callback_double_, this);
  }

  return ret;
}

SERVER_FRAME_API bool logic_hpa_policy::remove_observer_double(observable_callback_double_handle& handle) {
  if (!handle) {
    return false;
  }

  if (handle->owner != &observable_callback_double_) {
    return false;
  }

  // 版本不一致则是执行过clear，直接忽略即可
  if (handle->version != observable_callback_double_.version) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
  if (handle->iterator == observable_callback_double_.observable.end()) {
    handle->version = 0;
    handle->owner = nullptr;
    return false;
  }

  // 删除内置的回调列表
  observable_callback_double_.observable.erase(handle->iterator);
  handle->iterator = observable_callback_double_.observable.end();
  handle->version = 0;
  handle->owner = nullptr;

  // 如果内置的回调列表一清空，则可以清空otel-cpp的回调注册
  if (observable_callback_double_.observable.empty() && async_instruments_double_ &&
      nullptr != async_instrument_callback_double_) {
    FWLOGINFO("[HPA]: Policy {} cleanup double observer", metrics_name_);
    async_instruments_double_->RemoveCallback(async_instrument_callback_double_, this);
    async_instrument_callback_double_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }
  return true;
}

SERVER_FRAME_API logic_hpa_policy::observable_callback_custom_handle logic_hpa_policy::add_observer_custom(
    custom_observer_register_type register_type, observable_callback_custom fn,
    gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (!fn) {
    return nullptr;
  }

  if (!telemetry_group_) {
    return nullptr;
  }

  if (stoping_) {
    return nullptr;
  }

  observable_callback_custom_handle ret = atfw::memory::stl::make_shared<observable_callback_custom_data>();
  if (!ret) {
    return ret;
  }

  // 注册回调列表
  {
    std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
    ret->owner = &observable_callback_custom_;
    ret->version = observable_callback_custom_.version;
    ret->iterator = observable_callback_custom_.observable.insert(
        observable_callback_custom_.observable.end(), observable_callback_data<observable_callback_custom>());

    observable_callback_data<observable_callback_custom>& new_callback = *ret->iterator;
    new_callback.callback = std::move(fn);

    std::string attribute_hint = build_attribute_hint(new_callback.attributes, attributes);

    FWLOGINFO("[HPA]: Policy {} setup custom observer{}", metrics_name_, attribute_hint);

    if (!async_instruments_custom_) {
      if (custom_observer_register_type::kDouble == register_type) {
        async_instruments_custom_ = rpc::telemetry::global_service::mutable_metrics_observable_gauge_double(
            "hpa", {metrics_name_, metrics_description_, metrics_unit_}, telemetry_group_);
      } else {
        async_instruments_custom_ = rpc::telemetry::global_service::mutable_metrics_observable_gauge_int64(
            "hpa", {metrics_name_, metrics_description_, metrics_unit_}, telemetry_group_);
      }
    }

    if (!async_instruments_custom_) {
      FWLOGERROR("[HPA]: Policy {} create async instrument for custom observer failed", metrics_name_);
      observable_callback_custom_.observable.pop_back();
      return nullptr;
    }
  }

  // 没注册过回调到otel-cpp，需要注册回调一次
  // 后续的增加和删除只是在本地的回调列表中增加和删除，不影响otel-cpp的入口
  if (nullptr == async_instrument_callback_custom_) {
    async_instrument_callback_custom_ = [](opentelemetry::metrics::ObserverResult result, void* raw) {
      logic_hpa_policy* self = reinterpret_cast<logic_hpa_policy*>(raw);
      if (nullptr == self) {
        return;
      }

      bool has_record = false;
      std::lock_guard<std::recursive_mutex> lock_guard{self->metrics_resource_lock_};
      for (auto iter = self->observable_callback_custom_.observable.begin();
           iter != self->observable_callback_custom_.observable.end();) {
        auto current_iter = iter++;

        logic_hpa_observer current_observer{result, self->attributes_reference_, current_iter->attributes};
        current_iter->callback(*self, current_observer);

        if (current_observer.has_record()) {
          has_record = true;
        }
      }

      if (has_record && !self->ready_ && !self->is_pulling_available()) {
        self->do_ready();
      }
    };
    async_instruments_custom_->AddCallback(async_instrument_callback_custom_, this);
  }

  return ret;
}

SERVER_FRAME_API bool logic_hpa_policy::remove_observer_custom(observable_callback_custom_handle& handle) {
  if (!handle) {
    return false;
  }

  if (handle->owner != &observable_callback_custom_) {
    return false;
  }

  // 版本不一致则是执行过clear，直接忽略即可
  if (handle->version != observable_callback_custom_.version) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock_guard{metrics_resource_lock_};
  if (handle->iterator == observable_callback_custom_.observable.end()) {
    handle->version = 0;
    handle->owner = nullptr;
    return false;
  }

  // 删除内置的回调列表
  observable_callback_custom_.observable.erase(handle->iterator);
  handle->iterator = observable_callback_custom_.observable.end();
  handle->version = 0;
  handle->owner = nullptr;

  // 如果内置的回调列表一清空，则可以清空otel-cpp的回调注册
  if (observable_callback_custom_.observable.empty() && async_instruments_custom_ &&
      nullptr != async_instrument_callback_custom_) {
    FWLOGINFO("[HPA]: Policy {} cleanup custom observer", metrics_name_);
    async_instruments_custom_->RemoveCallback(async_instrument_callback_custom_, this);
    async_instruments_custom_ = nullptr;

    // TODO(owent): 等otel-cpp支持Meter内删除单个instrument对应的storage和view时，要删除存储区和视图
  }
  return true;
}

SERVER_FRAME_API bool logic_hpa_policy::is_stopped() const noexcept {
  if (!stoping_) {
    return false;
  }

  if (puller_ && !puller_->is_stopped()) {
    return false;
  }

  return true;
}

SERVER_FRAME_API bool logic_hpa_policy::can_pulling_available() const noexcept {
  if (!puller_) {
    return false;
  }

  return puller_->can_pulling_available();
}

SERVER_FRAME_API bool logic_hpa_policy::is_pulling_available() const noexcept {
  if (!can_pulling_available()) {
    return false;
  }

  // 任意回调是激活状态，则需要拉取数据触发拉取回调
  for (auto& callback : event_on_pull_range_callback_list_.callbacks) {
    if (callback.active == logic_hpa_event_active_type::kActive) {
      return true;
    }
  }

  for (auto& callback : event_on_pull_instant_callback_list_.callbacks) {
    if (callback.active == logic_hpa_event_active_type::kActive) {
      return true;
    }
  }

  return false;
}

SERVER_FRAME_API bool logic_hpa_policy::has_scaling_configure() const noexcept {
  return scaling_up_value_ > 0 || scaling_down_value_ > 0;
}

SERVER_FRAME_API logic_hpa_range_reduce_type logic_hpa_policy::get_pull_range_reduce_type() const noexcept {
  return pull_range_reduce_type_;
}

SERVER_FRAME_API void logic_hpa_policy::set_pull_range_reduce_type(logic_hpa_range_reduce_type t) noexcept {
  pull_range_reduce_type_ = t;
}

SERVER_FRAME_API logic_hpa_policy::event_on_pull_range_callback_handle logic_hpa_policy::add_event_on_pull_range(
    event_callback_on_pull_range fn, logic_hpa_event_active_type active) {
  if (!fn) {
    return {event_on_pull_range_callback_list_.callbacks.end(), 0};
  }

  // Active pulling immediately
  if (can_pulling_available() && !is_pulling_available()) {
    ready_ = false;
    next_pull_timepoint_ = util::time::time_utility::sys_now();
  }

  return {event_on_pull_range_callback_list_.callbacks.emplace(
              event_on_pull_range_callback_list_.callbacks.end(),
              event_callback_data<event_callback_on_pull_range>{std::move(fn), active}),
          event_on_pull_range_callback_list_.version};
}

SERVER_FRAME_API void logic_hpa_policy::remove_event_on_pull_range(event_on_pull_range_callback_handle& handle) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_pull_range_callback_list_.version) {
    handle = {event_on_pull_range_callback_list_.callbacks.end(), event_on_pull_range_callback_list_.version};
    return;
  }

  if (handle.iterator == event_on_pull_range_callback_list_.callbacks.end()) {
    return;
  }

  event_on_pull_range_callback_list_.callbacks.erase(handle.iterator);
  handle = {event_on_pull_range_callback_list_.callbacks.end(), event_on_pull_range_callback_list_.version};
}

SERVER_FRAME_API void logic_hpa_policy::set_event_on_pull_range_active(event_on_pull_range_callback_handle& handle,
                                                                       logic_hpa_event_active_type active) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_pull_range_callback_list_.version) {
    handle = {event_on_pull_range_callback_list_.callbacks.end(), event_on_pull_range_callback_list_.version};
    return;
  }

  if (handle.iterator == event_on_pull_range_callback_list_.callbacks.end()) {
    return;
  }

  (*handle.iterator).active = active;
}

SERVER_FRAME_API void logic_hpa_policy::clear_event_on_pull_range() {
  ++event_on_pull_range_callback_list_.version;
  event_on_pull_range_callback_list_.callbacks.clear();
}

SERVER_FRAME_API bool logic_hpa_policy::is_event_on_pull_range_handle_valid(
    const event_on_pull_range_callback_handle& handle) {
  if (handle.version != event_on_pull_range_callback_list_.version) {
    return false;
  }

  return handle.iterator != event_on_pull_range_callback_list_.callbacks.end();
}

SERVER_FRAME_API logic_hpa_policy::event_on_pull_instant_callback_handle logic_hpa_policy::add_event_on_pull_instant(
    event_callback_on_pull_instant fn, logic_hpa_event_active_type active) {
  if (!fn) {
    return {event_on_pull_instant_callback_list_.callbacks.end(), 0};
  }

  // Active pulling immediately
  if (can_pulling_available() && !is_pulling_available()) {
    ready_ = false;
    next_pull_timepoint_ = util::time::time_utility::sys_now();
  }

  return {event_on_pull_instant_callback_list_.callbacks.emplace(
              event_on_pull_instant_callback_list_.callbacks.end(),
              event_callback_data<event_callback_on_pull_instant>{std::move(fn), active}),
          event_on_pull_instant_callback_list_.version};
}

SERVER_FRAME_API void logic_hpa_policy::remove_event_on_pull_instant(event_on_pull_instant_callback_handle& handle) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_pull_instant_callback_list_.version) {
    handle = {event_on_pull_instant_callback_list_.callbacks.end(), event_on_pull_instant_callback_list_.version};
    return;
  }

  if (handle.iterator == event_on_pull_instant_callback_list_.callbacks.end()) {
    return;
  }

  event_on_pull_instant_callback_list_.callbacks.erase(handle.iterator);
  handle = {event_on_pull_instant_callback_list_.callbacks.end(), event_on_pull_instant_callback_list_.version};
}

SERVER_FRAME_API void logic_hpa_policy::set_event_on_pull_instant_active(event_on_pull_instant_callback_handle& handle,
                                                                         logic_hpa_event_active_type active) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_pull_instant_callback_list_.version) {
    handle = {event_on_pull_instant_callback_list_.callbacks.end(), event_on_pull_instant_callback_list_.version};
    return;
  }

  if (handle.iterator == event_on_pull_instant_callback_list_.callbacks.end()) {
    return;
  }

  (*handle.iterator).active = active;
}

SERVER_FRAME_API void logic_hpa_policy::clear_event_on_pull_instant() {
  ++event_on_pull_instant_callback_list_.version;
  event_on_pull_instant_callback_list_.callbacks.clear();
}

SERVER_FRAME_API bool logic_hpa_policy::is_event_on_pull_instant_handle_valid(
    const event_on_pull_instant_callback_handle& handle) {
  if (handle.version != event_on_pull_instant_callback_list_.version) {
    return false;
  }

  return handle.iterator != event_on_pull_instant_callback_list_.callbacks.end();
}

SERVER_FRAME_API void logic_hpa_policy::set_event_on_pull_error(event_callback_on_pull_error fn) {
  event_on_pull_error_callback_ = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_policy::clear_event_on_pull_error() {
  event_on_pull_error_callback_ = event_callback_on_pull_error{};
}

SERVER_FRAME_API void logic_hpa_policy::set_event_on_pull_warning(event_callback_on_pull_warning fn) {
  event_on_pull_warning_callback_ = std::move(fn);
}

SERVER_FRAME_API void logic_hpa_policy::clear_event_on_pull_warning() {
  event_on_pull_warning_callback_ = event_callback_on_pull_warning{};
}

SERVER_FRAME_API logic_hpa_policy::event_callback_on_ready_handle logic_hpa_policy::add_event_on_ready(
    event_callback_on_ready fn, logic_hpa_event_active_type active) {
  if (!fn) {
    return {event_on_ready_callback_.callbacks.end(), 0};
  }

  return {
      event_on_ready_callback_.callbacks.emplace(event_on_ready_callback_.callbacks.end(),
                                                 event_callback_data<event_callback_on_ready>{std::move(fn), active}),
      event_on_ready_callback_.version};
}

SERVER_FRAME_API void logic_hpa_policy::remove_event_on_ready(event_callback_on_ready_handle& handle) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_ready_callback_.version) {
    handle = {event_on_ready_callback_.callbacks.end(), event_on_ready_callback_.version};
    return;
  }

  if (handle.iterator == event_on_ready_callback_.callbacks.end()) {
    return;
  }

  event_on_ready_callback_.callbacks.erase(handle.iterator);
  handle = {event_on_ready_callback_.callbacks.end(), event_on_ready_callback_.version};
}

SERVER_FRAME_API void logic_hpa_policy::set_event_on_ready_active(event_callback_on_ready_handle& handle,
                                                                  logic_hpa_event_active_type active) {
  // 版本不一致则是执行过clear，直接重置即可
  if (handle.version != event_on_ready_callback_.version) {
    handle = {event_on_ready_callback_.callbacks.end(), event_on_ready_callback_.version};
    return;
  }

  if (handle.iterator == event_on_ready_callback_.callbacks.end()) {
    return;
  }

  (*handle.iterator).active = active;
}

SERVER_FRAME_API void logic_hpa_policy::clear_event_on_ready() {
  ++event_on_ready_callback_.version;
  event_on_ready_callback_.callbacks.clear();
}

SERVER_FRAME_API bool logic_hpa_policy::is_event_on_ready_handle_valid(const event_callback_on_ready_handle& handle) {
  if (handle.version != event_on_ready_callback_.version) {
    return false;
  }

  return handle.iterator != event_on_ready_callback_.callbacks.end();
}

SERVER_FRAME_API void logic_hpa_policy::set_private_data(void* priv_data) noexcept { private_data_ = priv_data; }

SERVER_FRAME_API void* logic_hpa_policy::get_private_data() const noexcept { return private_data_; }

SERVER_FRAME_API void logic_hpa_policy::schedule_pull() noexcept {
  FWLOGDEBUG("[HPA]: Policy {} schedule pull", metrics_name_);
  next_pull_timepoint_ = util::time::time_utility::sys_now();
}

SERVER_FRAME_API void logic_hpa_policy::trigger_event_on_pull_result(logic_hpa_pull_result& result) noexcept {
  // 错误消息转换
  if (result.is_error()) {
    gsl::string_view error_type = result.get_error_type();
    gsl::string_view error_message = result.get_error_message();
    FWLOGERROR("[HPA]: Policy {} got error query response(query: {}), type: {}, message: {}", metrics_name_,
               pull_query_, error_type, error_message);
    if (event_on_pull_error_callback_) {
      event_on_pull_error_callback_(*this, error_type, error_message);
    }
  }

  // 告警消息转换
  auto warning_messages = result.get_warning_messages();
  if (!warning_messages.empty()) {
    FWLOGWARNING("[HPA]: Policy {} got warning query response(query: {})", metrics_name_, pull_query_);
    for (size_t i = 0; i < warning_messages.size(); ++i) {
      FWLOGWARNING("\t{}/{}: {}", i + 1, warning_messages.size(), warning_messages[i]);
      if (event_on_pull_warning_callback_) {
        event_on_pull_warning_callback_(*this, warning_messages[i]);
      }
    }
  }

  // Instant类型数据回调
  if (result.has_instant_record() && !event_on_pull_instant_callback_list_.callbacks.empty()) {
    gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> param = result.get_instant_records();
    for (auto iter = event_on_pull_instant_callback_list_.callbacks.begin();
         iter != event_on_pull_instant_callback_list_.callbacks.end();) {
      auto cur_iter = iter++;
      if ((*cur_iter).active != logic_hpa_event_active_type::kUnactive && (*cur_iter).callback) {
        (*cur_iter).callback(*this, param);
      }
    }
  }

  // Range类型数据回调
  if (result.has_range_record()) {
    if (!event_on_pull_range_callback_list_.callbacks.empty()) {
      gsl::span<const std::unique_ptr<logic_hpa_pull_range_record>> param = result.get_range_records();
      for (auto iter = event_on_pull_range_callback_list_.callbacks.begin();
           iter != event_on_pull_range_callback_list_.callbacks.end();) {
        auto cur_iter = iter++;
        if ((*cur_iter).active != logic_hpa_event_active_type::kUnactive && (*cur_iter).callback) {
          (*cur_iter).callback(*this, param);
        }
      }
    } else if (!event_on_pull_instant_callback_list_.callbacks.empty()) {
      // 如果注册了Instant类型的回调，但是获取到Range类型的数据，走内置聚合策略做数据转换
      // range reduce to instant
      gsl::span<const std::unique_ptr<logic_hpa_pull_range_record>> param = result.get_range_records();
      std::vector<std::unique_ptr<logic_hpa_pull_instant_record>> reduce_instant_record;
      reduce_instant_record.reserve(param.size());
      for (auto& record : param) {
        if (!record) {
          continue;
        }
        reduce_instant_record.emplace_back(record->reduce(pull_range_reduce_type_));
      }
      // 然后再触发Instant类型的回调
      for (auto iter = event_on_pull_instant_callback_list_.callbacks.begin();
           iter != event_on_pull_instant_callback_list_.callbacks.end();) {
        auto cur_iter = iter++;
        if ((*cur_iter).active != logic_hpa_event_active_type::kUnactive && (*cur_iter).callback) {
          (*cur_iter).callback(*this, reduce_instant_record);
        }
      }
    }
  }

  if (!result.is_error()) {
    if (!ready_) {
      do_ready();
    }
  }
}
