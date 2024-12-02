// Copyright 2022 atframework
// Created by owent on 2021-07-13.
//

#include "rpc/telemetry/rpc_trace.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/util/time_util.h>
#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/trace/span_context.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/trace/noop.h>

#include <atframe/atapp.h>

#include <memory/object_allocator.h>

#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "config/logic_config.h"
#include "gsl/select-gsl.h"
#include "rpc/telemetry/opentelemetry_utility.h"
#include "rpc/telemetry/rpc_global_service.h"
#include "rpc/telemetry/semantic_conventions.h"

namespace rpc {

namespace telemetry {

namespace {
struct UTIL_SYMBOL_LOCAL trace_additional_metric_span {
  std::string span_name;
  std::string kind;
  opentelemetry::sdk::common::AttributeMap attribute;
  std::vector<trace_attribute_pair_type> attribute_vector;

  std::mutex record_lock;

  // pair<last report data, current data> : cumulative
  std::pair<int64_t, int64_t> cost_cpu_time;
  std::pair<int64_t, int64_t> call_count;
  std::unordered_map<int32_t, std::pair<int64_t, int64_t>> result_code_count;
  std::unordered_map<int32_t, std::pair<int64_t, int64_t>> response_code_count;
};

using trace_additional_metric_report = std::unordered_map<std::string, std::shared_ptr<trace_additional_metric_span>>;
struct UTIL_SYMBOL_LOCAL trace_additional_metric_data_set {
  std::mutex pending_to_report_lock;
  trace_additional_metric_report pending_to_report;
  trace_additional_metric_report local_history;

  bool cost_cpu_time_need_report;
  bool call_count_need_report;
  bool result_code_count_need_report;
  bool response_code_count_need_report;
  std::shared_ptr<trace_additional_metric_report> current_report;
};

static std::pair<trace_additional_metric_data_set &, std::mutex &> get_trace_additional_metric_global_data_set() {
  static std::mutex data_set_lock;
  static trace_additional_metric_data_set data_set;

  return {data_set, data_set_lock};
};

static std::shared_ptr<trace_additional_metric_span> mutable_trace_additional_metric_span(
    const std::string &span_name, opentelemetry::nostd::string_view kind, trace_attributes_type additional_attributes,
    bool &has_response_code, int32_t &response_code) {
  std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
      get_trace_additional_metric_global_data_set();

  std::shared_ptr<trace_additional_metric_span> ret;
  {
    std::lock_guard<std::mutex> guard{data_set_ref.second};

    auto iter = data_set_ref.first.local_history.find(span_name);
    if (iter != data_set_ref.first.local_history.end() && iter->second) {
      ret = iter->second;
    }
  }

  if (ret) {
    std::lock_guard<std::mutex> guard{data_set_ref.first.pending_to_report_lock};
    data_set_ref.first.pending_to_report[span_name] = ret;
    return ret;
  }

  ret = atfw::memory::stl::make_shared<trace_additional_metric_span>();
  if (!ret) {
    return ret;
  }

  ret->span_name = span_name;
  ret->kind = static_cast<std::string>(kind);
  ret->attribute.reserve(additional_attributes.size());
  for (auto &attribute_pair : additional_attributes) {
    if (attribute_pair.first.size() == 7 && (attribute_pair.first == "user_id" || attribute_pair.first == "zone_id")) {
      continue;
    }
    if (attribute_pair.first == rpc::telemetry::semantic_conventions::kAtRpcResponseCode) {
      if (opentelemetry::nostd::holds_alternative<int32_t>(attribute_pair.second)) {
        has_response_code = true;
        response_code = static_cast<int32_t>(opentelemetry::nostd::get<int32_t>(attribute_pair.second));
      } else if (opentelemetry::nostd::holds_alternative<int64_t>(attribute_pair.second)) {
        has_response_code = true;
        response_code = static_cast<int32_t>(opentelemetry::nostd::get<int64_t>(attribute_pair.second));
      } else if (opentelemetry::nostd::holds_alternative<uint32_t>(attribute_pair.second)) {
        has_response_code = true;
        response_code = static_cast<int32_t>(opentelemetry::nostd::get<uint32_t>(attribute_pair.second));
      } else if (opentelemetry::nostd::holds_alternative<uint64_t>(attribute_pair.second)) {
        has_response_code = true;
        response_code = static_cast<int32_t>(opentelemetry::nostd::get<uint64_t>(attribute_pair.second));
      }
      continue;
    }
    ret->attribute.SetAttribute(attribute_pair.first, attribute_pair.second);
  }
  ret->attribute_vector.reserve(ret->attribute.size());
  for (auto &attribute_pair : ret->attribute) {
    ret->attribute_vector.push_back(trace_attribute_pair_type{
        attribute_pair.first, opentelemetry_utility::convert_attribute_value_wihtout_array(attribute_pair.second)});
  }
  ret->call_count = {0, 0};
  ret->cost_cpu_time = {0, 0};

  {
    std::lock_guard<std::mutex> guard{data_set_ref.second};
    data_set_ref.first.local_history[span_name] = ret;
  }
  {
    std::lock_guard<std::mutex> guard{data_set_ref.first.pending_to_report_lock};
    data_set_ref.first.pending_to_report[span_name] = ret;
  }

  return ret;
}

static std::shared_ptr<trace_additional_metric_report> prepare_trace_additional_metric_report(
    trace_additional_metric_data_set &data_set, std::shared_ptr<trace_additional_metric_report> &previous) {
  if (data_set.call_count_need_report && data_set.current_report) {
    return data_set.current_report;
  }
  if (data_set.cost_cpu_time_need_report && data_set.current_report) {
    return data_set.current_report;
  }
  if (data_set.response_code_count_need_report && data_set.current_report) {
    return data_set.current_report;
  }
  if (data_set.result_code_count_need_report && data_set.current_report) {
    return data_set.current_report;
  }

  data_set.cost_cpu_time_need_report = true;
  data_set.call_count_need_report = true;
  data_set.result_code_count_need_report = true;
  data_set.response_code_count_need_report = true;
  data_set.current_report.swap(previous);
  data_set.current_report = atfw::memory::stl::make_shared<trace_additional_metric_report>();

  if (data_set.current_report) {
    std::lock_guard<std::mutex> guard{data_set.pending_to_report_lock};
    data_set.current_report->swap(data_set.pending_to_report);
  }

  return data_set.current_report;
}

static void setup_trace_additional_metric(std::string additional_metrics_name) {
  {
    std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
        get_trace_additional_metric_global_data_set();

    std::lock_guard<std::mutex> guard{data_set_ref.second};

    data_set_ref.first.cost_cpu_time_need_report = false;
    data_set_ref.first.call_count_need_report = false;
    data_set_ref.first.result_code_count_need_report = false;
    data_set_ref.first.response_code_count_need_report = false;
  }

  rpc::telemetry::global_service::add_on_ready([additional_metrics_name]() {
    // CPU time
    do {
      auto metric = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(
          additional_metrics_name, {additional_metrics_name + "_cost_cpu_time", "", "us"});
      if (!metric) {
        break;
      }

      metric->AddCallback(
          [](opentelemetry::metrics::ObserverResult result, void * /*callback*/) {
            std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
                get_trace_additional_metric_global_data_set();

            std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<trace_additional_metric_span>>>
                current_report;
            std::shared_ptr<trace_additional_metric_report> gc_report;
            {
              std::lock_guard<std::mutex> guard{data_set_ref.second};
              current_report = prepare_trace_additional_metric_report(data_set_ref.first, gc_report);
              if (!current_report) {
                return;
              }

              data_set_ref.first.cost_cpu_time_need_report = false;
            }
            gc_report.reset();

            std::shared_ptr<::rpc::telemetry::group_type> lifetime;
            if (opentelemetry::nostd::holds_alternative<
                    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
              auto observer = opentelemetry::nostd::get<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
              if (observer) {
                for (auto &report : *current_report) {
                  if (!report.second) {
                    continue;
                  }

                  int64_t record_value = 0;
                  {
                    std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                    record_value = report.second->cost_cpu_time.second;
                    report.second->cost_cpu_time.first = report.second->cost_cpu_time.second;
                  }

                  trace_attribute_pair_type internal_attributes[] = {
                      {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                      {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                  trace_attributes_type attributes_array[] = {
                      rpc::telemetry::global_service::get_metrics_labels_view(lifetime),
                      report.second->attribute_vector, internal_attributes};
                  multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                      opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                  observer->Observe(record_value, concat_attributes);
                }
              }
            } else if (opentelemetry::nostd::holds_alternative<
                           opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
              auto observer = opentelemetry::nostd::get<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
              if (observer) {
                for (auto &report : *current_report) {
                  if (!report.second) {
                    continue;
                  }

                  double record_value = 0;
                  {
                    std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                    record_value =
                        static_cast<double>(report.second->cost_cpu_time.second - report.second->cost_cpu_time.first);
                    report.second->cost_cpu_time.first = report.second->cost_cpu_time.second;
                  }

                  trace_attribute_pair_type internal_attributes[] = {
                      {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                      {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                  trace_attributes_type attributes_array[] = {
                      rpc::telemetry::global_service::get_metrics_labels_view(lifetime),
                      report.second->attribute_vector, internal_attributes};
                  multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                      opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                  observer->Observe(record_value, concat_attributes);
                }
              }
            }
          },
          nullptr);
    } while (false);

    // count
    do {
      auto metric = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(
          additional_metrics_name, {additional_metrics_name + "_count", "", ""});
      if (!metric) {
        break;
      }

      metric->AddCallback(
          [](opentelemetry::metrics::ObserverResult result, void * /*callback*/) {
            std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
                get_trace_additional_metric_global_data_set();

            std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<trace_additional_metric_span>>>
                current_report;
            std::shared_ptr<trace_additional_metric_report> gc_report;
            {
              std::lock_guard<std::mutex> guard{data_set_ref.second};
              current_report = prepare_trace_additional_metric_report(data_set_ref.first, gc_report);
              if (!current_report) {
                return;
              }

              data_set_ref.first.call_count_need_report = false;
            }
            gc_report.reset();

            std::shared_ptr<::rpc::telemetry::group_type> lifetime;
            if (opentelemetry::nostd::holds_alternative<
                    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
              auto observer = opentelemetry::nostd::get<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
              if (observer) {
                for (auto &report : *current_report) {
                  if (!report.second) {
                    continue;
                  }

                  int64_t record_value = 0;
                  {
                    std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                    record_value = report.second->call_count.second;
                    report.second->call_count.first = report.second->call_count.second;
                  }

                  trace_attribute_pair_type internal_attributes[] = {
                      {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                      {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                  trace_attributes_type attributes_array[] = {
                      rpc::telemetry::global_service::get_metrics_labels_view(lifetime),
                      report.second->attribute_vector, internal_attributes};
                  multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                      opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                  observer->Observe(record_value, concat_attributes);
                }
              }
            } else if (opentelemetry::nostd::holds_alternative<
                           opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
              auto observer = opentelemetry::nostd::get<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
              if (observer) {
                for (auto &report : *current_report) {
                  if (!report.second) {
                    continue;
                  }

                  double record_value = 0;
                  {
                    std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                    record_value =
                        static_cast<double>(report.second->call_count.second - report.second->call_count.first);
                    report.second->call_count.first = report.second->call_count.second;
                  }

                  trace_attribute_pair_type internal_attributes[] = {
                      {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                      {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                  trace_attributes_type attributes_array[] = {
                      rpc::telemetry::global_service::get_metrics_labels_view(lifetime),
                      report.second->attribute_vector, internal_attributes};
                  multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                      opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                  observer->Observe(record_value, concat_attributes);
                }
              }
            }
          },
          nullptr);
    } while (false);
  });

  // result code
  do {
    auto metric = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(
        additional_metrics_name, {additional_metrics_name + "_result_code", "", ""});
    if (!metric) {
      break;
    }

    metric->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void * /*callback*/) {
          std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
              get_trace_additional_metric_global_data_set();

          std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<trace_additional_metric_span>>>
              current_report;
          std::shared_ptr<trace_additional_metric_report> gc_report;
          {
            std::lock_guard<std::mutex> guard{data_set_ref.second};
            current_report = prepare_trace_additional_metric_report(data_set_ref.first, gc_report);
            if (!current_report) {
              return;
            }

            data_set_ref.first.result_code_count_need_report = false;
          }
          gc_report.reset();

          std::shared_ptr<::rpc::telemetry::group_type> lifetime;
          if (opentelemetry::nostd::holds_alternative<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
            auto observer = opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
            if (observer) {
              for (auto &report : *current_report) {
                if (!report.second) {
                  continue;
                }

                std::unordered_map<int32_t, int64_t> record_value;
                {
                  std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                  record_value.reserve(report.second->result_code_count.size());
                  for (auto &code_pair : report.second->result_code_count) {
                    record_value[code_pair.first] = code_pair.second.second;
                    code_pair.second.first = code_pair.second.second;
                  }
                }

                trace_attribute_pair_type internal_attributes[] = {
                    {rpc::telemetry::semantic_conventions::kAtRpcResultCode, 0},
                    {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                    {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                trace_attributes_type attributes_array[] = {
                    rpc::telemetry::global_service::get_metrics_labels_view(lifetime), report.second->attribute_vector,
                    internal_attributes};
                multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                    opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                for (auto &code_pair : record_value) {
                  internal_attributes[0].second = code_pair.first;
                  observer->Observe(code_pair.second, concat_attributes);
                }
              }
            }
          } else if (opentelemetry::nostd::holds_alternative<
                         opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
            auto observer = opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
            if (observer) {
              for (auto &report : *current_report) {
                if (!report.second) {
                  continue;
                }

                std::unordered_map<int32_t, int64_t> record_value;
                {
                  std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                  record_value.reserve(report.second->result_code_count.size());
                  for (auto &code_pair : report.second->result_code_count) {
                    record_value[code_pair.first] = code_pair.second.second;
                    code_pair.second.first = code_pair.second.second;
                  }
                }

                trace_attribute_pair_type internal_attributes[] = {
                    {rpc::telemetry::semantic_conventions::kAtRpcResultCode, 0},
                    {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                    {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                trace_attributes_type attributes_array[] = {
                    rpc::telemetry::global_service::get_metrics_labels_view(lifetime), report.second->attribute_vector,
                    internal_attributes};
                multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                    opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                for (auto &code_pair : record_value) {
                  internal_attributes[0].second = code_pair.first;
                  observer->Observe(static_cast<double>(code_pair.second), concat_attributes);
                }
              }
            }
          }
        },
        nullptr);
  } while (false);

  // response code
  do {
    auto metric = rpc::telemetry::global_service::mutable_metrics_observable_counter_int64(
        additional_metrics_name, {additional_metrics_name + "_response_code", "", ""});
    if (!metric) {
      break;
    }

    metric->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void * /*callback*/) {
          std::pair<trace_additional_metric_data_set &, std::mutex &> data_set_ref =
              get_trace_additional_metric_global_data_set();

          std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<trace_additional_metric_span>>>
              current_report;
          std::shared_ptr<trace_additional_metric_report> gc_report;
          {
            std::lock_guard<std::mutex> guard{data_set_ref.second};
            current_report = prepare_trace_additional_metric_report(data_set_ref.first, gc_report);
            if (!current_report) {
              return;
            }

            data_set_ref.first.response_code_count_need_report = false;
          }
          gc_report.reset();

          std::shared_ptr<::rpc::telemetry::group_type> lifetime;
          if (opentelemetry::nostd::holds_alternative<
                  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result)) {
            auto observer = opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(result);
            if (observer) {
              for (auto &report : *current_report) {
                if (!report.second) {
                  continue;
                }

                std::unordered_map<int32_t, int64_t> record_value;
                {
                  std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                  record_value.reserve(report.second->response_code_count.size());
                  for (auto &code_pair : report.second->response_code_count) {
                    record_value[code_pair.first] = code_pair.second.second;
                    code_pair.second.first = code_pair.second.second;
                  }
                }

                trace_attribute_pair_type internal_attributes[] = {
                    {rpc::telemetry::semantic_conventions::kAtRpcResponseCode, 0},
                    {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                    {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                trace_attributes_type attributes_array[] = {
                    rpc::telemetry::global_service::get_metrics_labels_view(lifetime), report.second->attribute_vector,
                    internal_attributes};
                multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                    opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                for (auto &code_pair : record_value) {
                  internal_attributes[0].second = code_pair.first;
                  observer->Observe(code_pair.second, concat_attributes);
                }
              }
            }
          } else if (opentelemetry::nostd::holds_alternative<
                         opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)) {
            auto observer = opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
            if (observer) {
              for (auto &report : *current_report) {
                if (!report.second) {
                  continue;
                }

                std::unordered_map<int32_t, int64_t> record_value;
                {
                  std::lock_guard<std::mutex> value_guard{report.second->record_lock};
                  record_value.reserve(report.second->response_code_count.size());
                  for (auto &code_pair : report.second->response_code_count) {
                    record_value[code_pair.first] = code_pair.second.second;
                    code_pair.second.first = code_pair.second.second;
                  }
                }

                trace_attribute_pair_type internal_attributes[] = {
                    {rpc::telemetry::semantic_conventions::kAtRpcResponseCode, 0},
                    {rpc::telemetry::semantic_conventions::kAtRpcKind, report.second->kind},
                    {rpc::telemetry::semantic_conventions::kAtRpcSpanName, report.second->span_name}};
                trace_attributes_type attributes_array[] = {
                    rpc::telemetry::global_service::get_metrics_labels_view(lifetime), report.second->attribute_vector,
                    internal_attributes};
                multiple_key_value_iterable_view<trace_attributes_type> concat_attributes{
                    opentelemetry::nostd::span<const trace_attributes_type>{attributes_array}};
                for (auto &code_pair : record_value) {
                  internal_attributes[0].second = code_pair.first;
                  observer->Observe(static_cast<double>(code_pair.second), concat_attributes);
                }
              }
            }
          }
        },
        nullptr);
  } while (false);
}

static void record_trace_additional_metric_span(const std::string &span_name, opentelemetry::nostd::string_view kind,
                                                int32_t result_code, trace_attributes_type additional_attributes,
                                                std::chrono::microseconds cost_cpu_time,
                                                const std::string &additional_metrics_name) {
  bool has_response_code = false;
  int32_t response_code = 0;
  std::shared_ptr<trace_additional_metric_span> span_metric =
      mutable_trace_additional_metric_span(span_name, kind, additional_attributes, has_response_code, response_code);
  if (!span_metric) {
    return;
  }

  static std::once_flag setup_function;
  std::call_once(setup_function, setup_trace_additional_metric, additional_metrics_name);

  std::lock_guard<std::mutex> guard{span_metric->record_lock};
  span_metric->cost_cpu_time.second += static_cast<int64_t>(cost_cpu_time.count());
  ++span_metric->call_count.second;
  {
    auto code_iter = span_metric->result_code_count.find(result_code);
    if (code_iter == span_metric->result_code_count.end()) {
      span_metric->result_code_count[result_code] = {0, 1};
    } else {
      ++code_iter->second.second;
    }
  }

  if (has_response_code) {
    auto code_iter = span_metric->response_code_count.find(response_code);
    if (code_iter == span_metric->response_code_count.end()) {
      span_metric->response_code_count[response_code] = {0, 1};
    } else {
      ++code_iter->second.second;
    }
  }
}
}  // namespace

SERVER_FRAME_API tracer::tracer() : trace_span_(nullptr), span_kind_("unset") {}

SERVER_FRAME_API tracer::~tracer() {
  // Additional metrics is not available when without finish call
  if (trace_span_) {
    finish({0, {}});
  }
}

SERVER_FRAME_API tracer::tracer(tracer &&other)
    : start_system_timepoint_(other.start_system_timepoint_),
      start_steady_timepoint_(other.start_steady_timepoint_),
      trace_span_(std::move(other.trace_span_)),
      trace_span_name_(std::move(other.trace_span_name_)),
      span_kind_(std::move(other.span_kind_)),
      dispatcher_(std::move(other.dispatcher_)) {
  other.trace_span_ = span_ptr_type();
  other.dispatcher_.reset();
  other.trace_span_name_.clear();
}

SERVER_FRAME_API tracer &tracer::operator=(tracer &&other) noexcept {
  start_system_timepoint_ = other.start_system_timepoint_;
  start_steady_timepoint_ = other.start_steady_timepoint_;
  trace_span_ = std::move(other.trace_span_);
  trace_span_name_ = std::move(other.trace_span_name_);
  span_kind_ = std::move(other.span_kind_);
  dispatcher_ = std::move(other.dispatcher_);

  other.trace_span_ = span_ptr_type();
  other.dispatcher_.reset();
  other.trace_span_name_.clear();
  return *this;
}

SERVER_FRAME_API bool tracer::start(string_view name, trace_start_option &&options) {
  if (trace_span_) {
    return true;
  }

  if (options.parent_memory_span) {
    if (!options.parent_memory_span->IsRecording()) {
      return false;
    }
  } else if (options.parent_network_span) {
    if (options.parent_network_span->dynamic_ignore()) {
      return false;
    }
  }

  auto tracer_obj = global_service::get_current_default_tracer();
  if (!tracer_obj) {
    return false;
  }

  opentelemetry::trace::StartSpanOptions span_options;
  start_system_timepoint_ = std::chrono::system_clock::now();
  start_steady_timepoint_ = std::chrono::steady_clock::now();
  span_options.start_system_time = opentelemetry::common::SystemTimestamp(start_system_timepoint_);
  span_options.start_steady_time = opentelemetry::common::SteadyTimestamp(start_steady_timepoint_);

  switch (options.kind) {
    case ::atframework::RpcTraceSpan::SPAN_KIND_INTERNAL:
      span_options.kind = opentelemetry::trace::SpanKind::kInternal;
      span_kind_ = "internal";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_SERVER:
      span_options.kind = opentelemetry::trace::SpanKind::kServer;
      span_kind_ = "server";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT:
      span_options.kind = opentelemetry::trace::SpanKind::kClient;
      span_kind_ = "client";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_PRODUCER:
      span_options.kind = opentelemetry::trace::SpanKind::kProducer;
      span_kind_ = "producer";
      break;
    case ::atframework::RpcTraceSpan::SPAN_KIND_CONSUMER:
      span_options.kind = opentelemetry::trace::SpanKind::kConsumer;
      span_kind_ = "consumer";
      break;
    default:
      span_kind_ = "unknown";
      break;
  }

  if (nullptr != options.parent_network_span &&
      options.parent_network_span->trace_id().size() == trace_id_span::extent &&
      options.parent_network_span->span_id().size() == span_id_span::extent) {
    const uint8_t *parent_trace_id = reinterpret_cast<const uint8_t *>(options.parent_network_span->trace_id().c_str());
    const uint8_t *parent_span_id = reinterpret_cast<const uint8_t *>(options.parent_network_span->span_id().c_str());
    uint8_t trace_flags = opentelemetry::trace::TraceFlags::kIsRandom;
    if (!options.parent_network_span->dynamic_ignore()) {
      trace_flags |= opentelemetry::trace::TraceFlags::kIsSampled;
    }
    span_options.parent = opentelemetry::trace::SpanContext{
        opentelemetry::trace::TraceId{trace_id_span{parent_trace_id, trace_id_span::extent}},
        opentelemetry::trace::SpanId{span_id_span{parent_span_id, span_id_span::extent}},
        opentelemetry::trace::TraceFlags{trace_flags}, options.is_remote};
  } else if (options.parent_memory_span) {
    if (!options.parent_memory_span->IsRecording()) {
      return false;
    }
    span_options.parent = options.parent_memory_span->GetContext();
  }

  if (options.attributes.empty() && options.links.empty()) {
    trace_span_ = tracer_obj->StartSpan(name, span_options);
  } else if (options.links.empty()) {
    trace_span_ = tracer_obj->StartSpan(name, options.attributes, span_options);
  } else {
    trace_span_ = tracer_obj->StartSpan(name, options.attributes, options.links, span_options);
  }

  dispatcher_ = std::move(options.dispatcher);
  trace_span_name_ = static_cast<std::string>(name);
  return !!trace_span_;
}

SERVER_FRAME_API int32_t tracer::finish(trace_finish_option &&options) {
  if (!trace_span_) {
    return options.result_code;
  }

  switch (options.result_code) {
    case PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND:
    case PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND:
    case PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION: {
      gsl::string_view error_msg = protobuf_mini_dumper_get_error_msg(options.result_code);
      trace_span_->SetStatus(opentelemetry::trace::StatusCode::kUnset, {error_msg.data(), error_msg.size()});
      break;
    }
    default: {
      if (options.result_code < 0) {
        gsl::string_view error_msg = protobuf_mini_dumper_get_error_msg(options.result_code);
        trace_span_->SetStatus(opentelemetry::trace::StatusCode::kError, {error_msg.data(), error_msg.size()});
      } else {
        trace_span_->SetStatus(opentelemetry::trace::StatusCode::kOk, "success");
      }
      break;
    }
  }
  trace_span_->SetAttribute(rpc::telemetry::semantic_conventions::kAtRpcResultCode, options.result_code);

  std::chrono::steady_clock::time_point end_steady_timepoint = std::chrono::steady_clock::now();
  opentelemetry::trace::EndSpanOptions end_options;
  end_options.end_steady_time = opentelemetry::common::SteadyTimestamp(end_steady_timepoint);

  // trace_span_->End will destroy recording status, so get it before End()
  bool is_span_recording = trace_span_->IsRecording();

  trace_span_->End(end_options);

  auto &trace_configure = logic_config::me()->get_logic().telemetry().opentelemetry().trace();
  const std::string &additional_metrics_name = trace_configure.additional_metrics_name();
  if (is_span_recording && !additional_metrics_name.empty() && !trace_span_name_.empty() &&
      trace_configure.enable_additional_metrics()) {
    record_trace_additional_metric_span(
        trace_span_name_, span_kind_, options.result_code, options.attributes,
        std::chrono::duration_cast<std::chrono::microseconds>(end_steady_timepoint - start_steady_timepoint_),
        additional_metrics_name);
  }

  trace_span_ = span_ptr_type();
  return options.result_code;
}

SERVER_FRAME_API bool tracer::is_recording() const noexcept { return trace_span_ && trace_span_->IsRecording(); }

SERVER_FRAME_API void tracer::update_trace_name(string_view name) {
  if (trace_span_) {
    trace_span_->UpdateName(name);
    trace_span_name_ = static_cast<std::string>(name);
  }
}

SERVER_FRAME_API const tracer::span_ptr_type &tracer::get_shared_noop_trace_span() {
  static tracer::span_ptr_type ret;
  if (!ret) {
    static auto noop_tracer = std::make_shared<opentelemetry::trace::NoopTracer>();
    ret = static_cast<opentelemetry::trace::Tracer *>(noop_tracer.get())->StartSpan("noop");
  }

  return ret;
}

}  // namespace telemetry

}  // namespace rpc
