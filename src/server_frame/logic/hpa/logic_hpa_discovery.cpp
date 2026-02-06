// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/logic_hpa_discovery.h"

#include <config/logic_config.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <atframe/etcdcli/etcd_cluster.h>
#include <atframe/etcdcli/etcd_watcher.h>
#include <atframe/modules/etcd_module.h>

#include <memory/object_allocator.h>

#include <chrono>
#include <string>

#include "logic/hpa/logic_hpa_controller.h"
#include "logic/hpa/logic_hpa_policy.h"

#if !((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
constexpr const char* logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainDefault;
constexpr const char* logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainCustom;
#endif

namespace {
std::chrono::system_clock::duration convert_to_chrono(const ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::Duration& in,
                                                      time_t default_value_ms) {
  if (in.seconds() <= 0 && in.nanos() <= 0) {
    return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(default_value_ms));
  }
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(in.seconds())) +
         std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(in.nanos()));
}
}  // namespace

logic_hpa_discovery_setup_policy_accessor::logic_hpa_discovery_setup_policy_accessor() {}

logic_hpa_discovery_setup_policy_accessor::~logic_hpa_discovery_setup_policy_accessor() {}

SERVER_FRAME_API logic_hpa_discovery_provider::logic_hpa_discovery_provider() {}

SERVER_FRAME_API logic_hpa_discovery_provider::~logic_hpa_discovery_provider() {}

SERVER_FRAME_API bool logic_hpa_discovery_provider::is_ready(const logic_hpa_discovery&) { return true; }

SERVER_FRAME_API int32_t logic_hpa_discovery_provider::get_non_native_cloud_replicas(const logic_hpa_discovery&,
                                                                                     int32_t previous_result) {
  return previous_result;
}

SERVER_FRAME_API int32_t logic_hpa_discovery_provider::get_scaling_up_expect_replicas(const logic_hpa_discovery&,
                                                                                      int32_t previous_result) {
  return previous_result;
}

SERVER_FRAME_API int32_t logic_hpa_discovery_provider::get_scaling_down_expect_replicas(const logic_hpa_discovery&,
                                                                                        int32_t previous_result) {
  return previous_result;
}

struct ATFW_UTIL_SYMBOL_LOCAL logic_hpa_discovery::policy_data {
  std::shared_ptr<logic_hpa_policy> policy;
  logic_hpa_policy::event_callback_on_ready_handle ready_handle;
  logic_hpa_policy::event_on_pull_instant_callback_handle pull_instant_handle;
  int64_t last_scaling_value;
  std::chrono::system_clock::time_point last_update_timepoint;

  policy_data(const std::shared_ptr<logic_hpa_policy>& p, logic_hpa_policy::event_callback_on_ready_handle&& ready_h,
              logic_hpa_policy::event_on_pull_instant_callback_handle&& pull_instant_h)
      : policy(p),
        ready_handle(std::move(ready_h)),
        pull_instant_handle(std::move(pull_instant_h)),
        last_scaling_value(0),
        last_update_timepoint(std::chrono::system_clock::from_time_t(0)) {}
};

struct ATFW_UTIL_SYMBOL_LOCAL logic_hpa_discovery::custom_provider_guard {
  custom_provider_guard(const logic_hpa_discovery& owner) : owner_(nullptr) {
    if (owner.custom_provider_guard_ == nullptr) {
      owner.custom_provider_guard_ = this;
      owner_ = &owner;
    }
  }

  ~custom_provider_guard() {
    if (nullptr == owner_) {
      return;
    }

    for (auto& provider_ptr : add_provider) {
      owner_->custom_provider_[provider_ptr.first] = provider_ptr.second;
    }

    for (auto& provider_ptr : remove_provider) {
      owner_->custom_provider_.erase(provider_ptr);
    }

    owner_->custom_provider_guard_ = nullptr;
  }

  const logic_hpa_discovery* owner_;

  std::unordered_map<logic_hpa_discovery_provider*, std::shared_ptr<logic_hpa_discovery_provider>> add_provider;
  std::unordered_set<logic_hpa_discovery_provider*> remove_provider;
};

SERVER_FRAME_API std::string logic_hpa_discovery::make_path(gsl::string_view key, gsl::string_view domain) {
  if (domain.empty()) {
    domain = logic_hpa_discovery_semantic_conventions::kLogicHpaDiscoveryDomainDefault;
  }

  auto& hpa_configure = logic_config::me()->get_logic().hpa();

  std::string etcd_path;
  // 预留空间，粗略即可
  etcd_path.reserve(hpa_configure.controller().configure_key().size() +
                    logic_config::me()->get_deployment_environment_name().size() + domain.size() + key.size() + 4);
  etcd_path = hpa_configure.controller().configure_key();
  if (etcd_path.empty()) {
    etcd_path = "/atapp/hpa/";
  }

  gsl::string_view deployment_environment = logic_config::me()->get_deployment_environment_name();
  if (!deployment_environment.empty()) {
    // 如果Target里已经包含了环境信息，则忽略追加环境隔离
    if (key.size() <= deployment_environment.size() ||
        gsl::string_view(key.data(), deployment_environment.size()) != deployment_environment ||
        key[deployment_environment.size()] != '/') {
      if ('/' != *etcd_path.rbegin()) {
        etcd_path += '/';
      }
      etcd_path += static_cast<std::string>(deployment_environment);
    }
  }

  if (!domain.empty()) {
    if ('/' != *etcd_path.rbegin()) {
      etcd_path += '/';
    }
    etcd_path += static_cast<std::string>(domain);
  }

  if (!key.empty()) {
    if ('/' != *etcd_path.rbegin()) {
      etcd_path += '/';
    }
    etcd_path += static_cast<std::string>(key);
  }

  while (etcd_path.size() > 1 && '/' == *etcd_path.rbegin()) {
    etcd_path.pop_back();
  }

  return etcd_path;
}

SERVER_FRAME_API logic_hpa_discovery::logic_hpa_discovery(logic_hpa_controller& controller, gsl::string_view key,
                                                          gsl::string_view domain)
    : controller_(&controller),
      stoping_(false),
      ready_(false),
      pull_policy_active_(logic_hpa_event_active_type::kActive),
      pull_policy_update_timepoint_(std::chrono::system_clock::now()),
      pull_policy_waiting_counter_(0),
      private_data_(nullptr),
      last_tick_(0),
      etcd_watch_mode_(logic_hpa_discovery_watch_mode::kExactly),
      custom_provider_guard_(nullptr) {
  etcd_path_ = make_path(key, domain);
  last_tick_ = util::time::time_utility::get_sys_now();
}

SERVER_FRAME_API logic_hpa_discovery::~logic_hpa_discovery() {
  if (etcd_watcher_) {
    clear_etcd_watcher();
  }

  if (etcd_set_value_) {
    etcd_set_value_->stop();
    clear_etcd_set_value_rpc();
  }

  clear_event_on_changed();
  clear_event_on_ready();
}

SERVER_FRAME_API int logic_hpa_discovery::tick() {
  time_t now = util::time::time_utility::get_sys_now();
  if (now == last_tick_) {
    return 0;
  }
  last_tick_ = now;

  // 尝试转ready状态
  if (!ready_ && should_ready()) {
    do_ready();
  }

  if (etcd_set_value_ && !etcd_set_value_->is_running()) {
    clear_etcd_set_value_rpc();
  }

  return 0;
}

SERVER_FRAME_API void logic_hpa_discovery::stop() {
  stoping_ = true;

  if (etcd_watcher_) {
    clear_etcd_watcher();
  }

  if (etcd_set_value_) {
    etcd_set_value_->stop();

    if (!etcd_set_value_->is_running()) {
      clear_etcd_set_value_rpc();
    }
  }
}

SERVER_FRAME_API bool logic_hpa_discovery::is_stopped() const noexcept {
  if (!stoping_) {
    return false;
  }

  if (etcd_watcher_) {
    return false;
  }

  if (etcd_set_value_ && etcd_set_value_->is_running()) {
    return false;
  }

  return true;
}

SERVER_FRAME_API void logic_hpa_discovery::add_pull_policy(std::shared_ptr<logic_hpa_policy> policy,
                                                           logic_hpa_discovery_setup_policy_accessor&) {
  if (!policy) {
    return;
  }

  if (policy->get_query().empty()) {
    FWLOGWARNING("[HPA]: Discovery {} can not add policy {} because it do not pull anything", etcd_path_,
                 policy->get_metrics_name());
    return;
  }

  {
    auto iter = policy_data_.find(policy->get_metrics_name());
    if (iter != policy_data_.end() && iter->second) {
      if (iter->second->policy == policy) {
        FWLOGWARNING("[HPA]: Discovery {} try to add policy {} more than one times and will be ignored.", etcd_path_,
                     policy->get_metrics_name());
      } else {
        FWLOGWARNING("[HPA]: Discovery {} try to add different policies with the same name {} will be ignored.",
                     etcd_path_, policy->get_metrics_name());
      }

      return;
    }
  }

  auto ready_handle = policy->add_event_on_ready(
      [this](logic_hpa_policy&) {
        if (!this->ready_ && this->should_ready()) {
          this->do_ready();
        }
      },
      pull_policy_active_);

  auto pull_instant_handle = policy->add_event_on_pull_instant(
      [this](logic_hpa_policy& sender, gsl::span<const std::unique_ptr<logic_hpa_pull_instant_record>> records) {
        auto policy_iter = this->policy_data_.find(sender.get_metrics_name());
        if (policy_iter == this->policy_data_.end()) {
          return;
        }

        if (!policy_iter->second) {
          return;
        }

        std::chrono::system_clock::time_point select_tp = std::chrono::system_clock::from_time_t(0);
        int64_t select_value = 0;
        for (auto& record : records) {
          if (!record) {
            continue;
          }

          // 提交间隔至少是秒级(默认15秒)，这里处理一下忽略抖动（虽然大概率不会有抖动）
          // 理论上应该i通过query保证返回的数据只有一个，如果出现多维度，可能是抖动或query错误，此时取最大的
          if (record->get_time_point() + std::chrono::seconds{1} < select_tp) {
            continue;
          } else if (record->get_time_point() > select_tp + std::chrono::seconds{1}) {
            select_value = record->get_value_as_int64();
            select_tp = record->get_time_point();
          } else {
            if (record->get_value_as_int64() > select_value) {
              select_value = record->get_value_as_int64();
            }
          }
        }

        if (select_tp > std::chrono::system_clock::from_time_t(0)) {
          policy_iter->second->last_scaling_value = select_value;
          policy_iter->second->last_update_timepoint = select_tp;

          this->decrease_pull_policy_waiting_counter();
        }
      },
      pull_policy_active_);

  if (policy->can_pulling_available() && policy->is_event_on_pull_instant_handle_valid(pull_instant_handle)) {
    increase_pull_policy_waiting_counter();
  }

  policy_data_[policy->get_metrics_name()] =
      atfw::memory::stl::make_shared<policy_data>(policy, std::move(ready_handle), std::move(pull_instant_handle));

  if (!policy->is_ready()) {
    ready_ = false;
  }
}

SERVER_FRAME_API void logic_hpa_discovery::reset_policy(logic_hpa_discovery_setup_policy_accessor&) {
  ready_ = false;

  std::unordered_map<std::string, std::shared_ptr<policy_data>> policy_datas;
  policy_datas.swap(policy_data_);

  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    policy_item.second->policy->remove_event_on_ready(policy_item.second->ready_handle);
    policy_item.second->policy->remove_event_on_pull_instant(policy_item.second->pull_instant_handle);
  }
}

SERVER_FRAME_API logic_hpa_event_active_type logic_hpa_discovery::get_policy_active() const noexcept {
  return pull_policy_active_;
}

SERVER_FRAME_API void logic_hpa_discovery::set_policy_active(logic_hpa_event_active_type active) noexcept {
  if (pull_policy_active_ == active) {
    return;
  }

  pull_policy_active_ = active;
  pull_policy_update_timepoint_ = std::chrono::system_clock::now();

  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    policy_item.second->policy->set_event_on_ready_active(policy_item.second->ready_handle, pull_policy_active_);
    policy_item.second->policy->set_event_on_pull_instant_active(policy_item.second->pull_instant_handle,
                                                                 pull_policy_active_);
  }

  // 重置待拉取计数
  reset_pull_policy_waiting_counter();
}

SERVER_FRAME_API void logic_hpa_discovery::foreach_policy(
    util::nostd::function_ref<bool(const logic_hpa_policy&, int64_t last_value, std::chrono::system_clock::time_point)>
        fn) {
  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    if (!fn(*policy_item.second->policy, policy_item.second->last_scaling_value,
            policy_item.second->last_update_timepoint)) {
      break;
    }
  }
}

SERVER_FRAME_API bool logic_hpa_discovery::watch(logic_hpa_discovery_watch_mode mode) {
  if (is_stoping()) {
    FWLOGDEBUG("[HPA]: Discovery {} ignore watching directory because is stoping", etcd_path_);
    return false;
  }

  if (mode == logic_hpa_discovery_watch_mode::kDirectory) {
    FWLOGDEBUG("[HPA]: Discovery {} try to watch directory", etcd_path_);
  } else {
    FWLOGDEBUG("[HPA]: Discovery {} try to watch exactly path", etcd_path_);
  }
  if (etcd_watcher_ && mode == etcd_watch_mode_) {
    return true;
  }

  if (nullptr == controller_->get_app()) {
    FWLOGERROR("[HPA]: Discovery {} can not create watcher without atapp", etcd_path_);
    return false;
  }

  auto etcd_mod = controller_->get_app()->get_etcd_module();
  if (!etcd_mod) {
    FWLOGERROR("[HPA]: Discovery {} can not create watcher without etcd module", etcd_path_);
    return false;
  }

  clear_etcd_watcher();

  if (mode == logic_hpa_discovery_watch_mode::kDirectory) {
    // @see https://etcd.io/docs/v3.5/learning/api/#key-ranges
    etcd_watcher_ = atfw::atapp::etcd_watcher::create(etcd_mod->get_raw_etcd_ctx(), etcd_path_, "+1");
  } else {
    etcd_watcher_ = atfw::atapp::etcd_watcher::create(etcd_mod->get_raw_etcd_ctx(), etcd_path_, "");
  }
  if (!etcd_watcher_) {
    FWLOGERROR("[HPA]: Discovery {} can create watcher failed", etcd_path_);
    return false;
  }

  // Watch走long polling，所以这里设置的超时时间应该比较长
  etcd_watcher_->set_conf_request_timeout(
      convert_to_chrono(etcd_mod->get_configure().watcher().request_timeout(), 3600000));
  etcd_watcher_->set_conf_retry_interval(
      convert_to_chrono(etcd_mod->get_configure().watcher().retry_interval(), 15000));
  etcd_watcher_->set_conf_get_request_timeout(
      convert_to_chrono(etcd_mod->get_configure().watcher().get_request_timeout(), 180000));
  etcd_watcher_->set_conf_startup_random_delay_min(
      convert_to_chrono(etcd_mod->get_configure().watcher().startup_random_delay_min(), 0));
  etcd_watcher_->set_conf_startup_random_delay_max(
      convert_to_chrono(etcd_mod->get_configure().watcher().startup_random_delay_max(), 0));

  // setup callback
  etcd_watcher_->set_evt_handle(
      [this](const atfw::atapp::etcd_response_header&, const atfw::atapp::etcd_watcher::response_t& evt_data) {
        for (auto& evt_item : evt_data.events) {
          data_header evt_header;

          gsl::string_view path;
          const std::string* value_ptr;
          if (!evt_item.kv.key.empty()) {
            path = evt_item.kv.key;
            evt_header.create_revision = evt_item.kv.create_revision;
            evt_header.mod_revision = evt_item.kv.mod_revision;
            evt_header.version = evt_item.kv.version;
            value_ptr = &evt_item.kv.value;
          } else {
            path = evt_item.prev_kv.key;
            evt_header.create_revision = evt_item.prev_kv.create_revision;
            evt_header.mod_revision = evt_item.prev_kv.mod_revision;
            evt_header.version = evt_item.prev_kv.version;
            value_ptr = &evt_item.prev_kv.value;
          }

          // Filter suffix
          if (path.size() < this->etcd_path_.size()) {
            FWLOGWARNING("[HPA]: Discovery {} got invalid event path {}", this->etcd_path_, path);
            continue;
          }
          if (path.substr(0, this->etcd_path_.size()) != this->etcd_path_) {
            FWLOGWARNING("[HPA]: Discovery {} got invalid event path {}", this->etcd_path_, path);
            continue;
          }
          evt_header.subkey = path.substr(this->etcd_path_.size());
          if (!evt_header.subkey.empty() && evt_header.subkey[0] == '/') {
            evt_header.subkey = evt_header.subkey.substr(1);
          }

          // Handle event
          if (evt_item.evt_type == atfw::atapp::etcd_watch_event::kPut) {
            this->do_changed_put(evt_header, *value_ptr);
          } else {
            this->do_changed_delete(evt_header);
          }
        }
      });

  etcd_mod->get_raw_etcd_ctx().add_watcher(etcd_watcher_);
  return true;
}

SERVER_FRAME_API bool logic_hpa_discovery::set_value(std::string&& value, gsl::string_view subkey) {
  if (is_stoping()) {
    FWLOGDEBUG("[HPA]: Discovery {} ignore setting value because is stoping(subkey=\"{}\"): {}", etcd_path_, subkey,
               value);
    return false;
  }

  FWLOGDEBUG("[HPA]: Discovery {} try to set value(subkey=\"{}\"): {}", etcd_path_, subkey, value);
  if (is_setting_value()) {
    FWLOGWARNING("[HPA]: Discovery {} can not set value(subkey={}) when previous request is running", etcd_path_,
                 subkey);
    return false;
  }

  if (nullptr == controller_->get_app()) {
    FWLOGERROR("[HPA]: Discovery {} can not set value(subkey={}) without atapp", etcd_path_, subkey);
    return false;
  }

  auto etcd_mod = controller_->get_app()->get_etcd_module();
  if (!etcd_mod) {
    FWLOGERROR("[HPA]: Discovery {} can not set value(subkey={}) without etcd module", etcd_path_, subkey);
    return false;
  }

  clear_etcd_set_value_rpc();
  if (subkey.empty()) {
    etcd_set_value_ = etcd_mod->get_raw_etcd_ctx().create_request_kv_set(etcd_path_, value, false);
  } else {
    etcd_set_value_ =
        etcd_mod->get_raw_etcd_ctx().create_request_kv_set(util::log::format("{}/{}", etcd_path_, subkey), value);
  }

  if (!etcd_set_value_) {
    FWLOGERROR("[HPA]: Discovery {} can not set value(subkey={}) because create request failed", etcd_path_, subkey);
    return false;
  } else {
    etcd_set_value_->set_priv_data(this);
    etcd_set_value_->set_on_complete([](util::network::http_request& request) -> int {
      logic_hpa_discovery* self = reinterpret_cast<logic_hpa_discovery*>(request.get_priv_data());
      if (self == nullptr) {
        return 0;
      }

      if (self->etcd_set_value_.get() == &request) {
        self->clear_etcd_set_value_rpc();
      }

      // 服务器错误则忽略
      if (0 != request.get_error_code() ||
          util::network::http_request::status_code_t::EN_ECG_SUCCESS !=
              util::network::http_request::get_status_code_group(request.get_response_code())) {
        FWLOGERROR("[HPA]: Discovery {} set value request({}) failed(error code={}, http code={}): {}",
                   self->etcd_path_, reinterpret_cast<const void*>(&request), request.get_error_code(),
                   request.get_response_code(), request.get_response_stream().str());
      } else {
        FWLOGINFO("[HPA]: Discovery {} set value request({}) success", self->etcd_path_,
                  reinterpret_cast<const void*>(&request));
      }

      return 0;
    });
    etcd_set_value_->set_on_error([](util::network::http_request& request) -> int {
      logic_hpa_discovery* self = reinterpret_cast<logic_hpa_discovery*>(request.get_priv_data());
      if (self == nullptr) {
        FWLOGERROR("[HPA]: Discovery {} set value request({}) with error: {}", "[UNKNOWN]",
                   reinterpret_cast<const void*>(&request), request.get_error_msg());
        return 0;
      }

      FWLOGERROR("[HPA]: Discovery {} set value request({}) with error: {}", self->etcd_path_,
                 reinterpret_cast<const void*>(&request), request.get_error_msg());
      return 0;
    });

    int res = etcd_set_value_->start(util::network::http_request::method_t::EN_MT_POST, false);
    if (res != 0) {
      FWLOGERROR("[HPA]: Discovery {} can not set value(subkey={}) because start request failed, code: {}", etcd_path_,
                 subkey, res);

      clear_etcd_set_value_rpc();
      return false;
    }

    FWLOGINFO("[HPA]: Discovery {} set value(subkey={}) request({}) start", etcd_path_, subkey,
              reinterpret_cast<const void*>(etcd_set_value_.get()));

    return true;
  }
}

SERVER_FRAME_API bool logic_hpa_discovery::is_setting_value() const noexcept {
  return etcd_set_value_ && etcd_set_value_->is_running();
}

SERVER_FRAME_API void logic_hpa_discovery::set_private_data(void* priv_data) noexcept { private_data_ = priv_data; }

SERVER_FRAME_API void* logic_hpa_discovery::get_private_data() const noexcept { return private_data_; }

SERVER_FRAME_API logic_hpa_discovery::event_callback_on_ready_handle logic_hpa_discovery::add_event_on_ready(
    event_callback_on_ready fn, logic_hpa_event_active_type active) {
  if (!fn) {
    return {event_on_ready_callback_.callbacks.end(), 0};
  }

  auto ret = event_on_ready_callback_.callbacks.emplace(
      event_on_ready_callback_.callbacks.end(), event_callback_data<event_callback_on_ready>{std::move(fn), active});
  if (ready_ && (*ret).callback) {
    (*ret).callback(*this);
  }

  return {ret, event_on_ready_callback_.version};
}

SERVER_FRAME_API void logic_hpa_discovery::remove_event_on_ready(event_callback_on_ready_handle& handle) {
  // 版本号不匹配说明调用过clear接口，直接重置迭代器即可
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

SERVER_FRAME_API void logic_hpa_discovery::set_event_on_ready_active(event_callback_on_ready_handle& handle,
                                                                     logic_hpa_event_active_type active) {
  // 版本号不匹配说明调用过clear接口，直接重置迭代器即可
  if (handle.version != event_on_ready_callback_.version) {
    handle = {event_on_ready_callback_.callbacks.end(), event_on_ready_callback_.version};
    return;
  }

  if (handle.iterator == event_on_ready_callback_.callbacks.end()) {
    return;
  }

  (*handle.iterator).active = active;
}

SERVER_FRAME_API void logic_hpa_discovery::clear_event_on_ready() {
  // 清空回调直接增加版本号即可，handle操作接口会先保证版号有效
  ++event_on_ready_callback_.version;
  event_on_ready_callback_.callbacks.clear();
}

SERVER_FRAME_API bool logic_hpa_discovery::is_event_on_ready_handle_valid(
    const event_callback_on_ready_handle& handle) {
  if (handle.version != event_on_ready_callback_.version) {
    return false;
  }

  return handle.iterator != event_on_ready_callback_.callbacks.end();
}

SERVER_FRAME_API logic_hpa_discovery::event_callback_on_changed_handle logic_hpa_discovery::add_event_on_changed(
    event_callback_on_changed fn, gsl::string_view subkey, logic_hpa_event_active_type active) {
  if (!fn) {
    return {event_on_changed_callback_.callbacks.end(), 0};
  }

  return {event_on_changed_callback_.callbacks.emplace(
              event_on_changed_callback_.callbacks.end(),
              event_callback_data<data_change_listener>{
                  data_change_listener{std::move(fn), static_cast<std::string>(subkey)}, active}),
          event_on_changed_callback_.version};
}

SERVER_FRAME_API void logic_hpa_discovery::remove_event_on_changed(event_callback_on_changed_handle& handle) {
  // 版本号不匹配说明调用过clear接口，直接重置迭代器即可
  if (handle.version != event_on_changed_callback_.version) {
    handle = {event_on_changed_callback_.callbacks.end(), event_on_changed_callback_.version};
    return;
  }

  if (handle.iterator == event_on_changed_callback_.callbacks.end()) {
    return;
  }

  event_on_changed_callback_.callbacks.erase(handle.iterator);
  handle = {event_on_changed_callback_.callbacks.end(), event_on_changed_callback_.version};
}

SERVER_FRAME_API void logic_hpa_discovery::set_event_on_changed_active(event_callback_on_changed_handle& handle,
                                                                       logic_hpa_event_active_type active) {
  // 版本号不匹配说明调用过clear接口，直接重置迭代器即可
  if (handle.version != event_on_changed_callback_.version) {
    handle = {event_on_changed_callback_.callbacks.end(), event_on_changed_callback_.version};
    return;
  }

  if (handle.iterator == event_on_changed_callback_.callbacks.end()) {
    return;
  }

  (*handle.iterator).active = active;
}

SERVER_FRAME_API void logic_hpa_discovery::clear_event_on_changed() {
  // 清空回调直接增加版本号即可，handle操作接口会先保证版号有效
  ++event_on_changed_callback_.version;
  event_on_changed_callback_.callbacks.clear();
}

SERVER_FRAME_API bool logic_hpa_discovery::is_event_on_changed_handle_valid(
    const event_callback_on_changed_handle& handle) {
  // 版本号不匹配说明调用过clear接口，迭代器一定无效
  if (handle.version != event_on_changed_callback_.version) {
    return false;
  }

  return handle.iterator != event_on_changed_callback_.callbacks.end();
}

SERVER_FRAME_API int32_t logic_hpa_discovery::get_non_native_cloud_replicas(int32_t previous_result) const {
  {
    custom_provider_guard guard{*this};
    for (auto& provider_ptr : custom_provider_) {
      if (!provider_ptr.second) {
        continue;
      }

      previous_result = provider_ptr.second->get_non_native_cloud_replicas(*this, previous_result);
    }
  }

  return previous_result;
}

SERVER_FRAME_API int32_t logic_hpa_discovery::get_scaling_up_expect_replicas() const {
  int32_t result = 0;
  // 指标策略的扩容计算
  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    if (policy_item.second->policy->get_configure_scaling_up_value() <= 0) {
      continue;
    }

    int32_t policy_value = static_cast<int32_t>((policy_item.second->last_scaling_value - 1) /
                                                policy_item.second->policy->get_configure_scaling_up_value()) +
                           1;
    if (policy_value > result) {
      result = policy_value;
    }
  }

  // 自定义扩容计算规则
  {
    custom_provider_guard guard{*this};
    for (auto& provider_ptr : custom_provider_) {
      if (!provider_ptr.second) {
        continue;
      }

      result = provider_ptr.second->get_scaling_up_expect_replicas(*this, result);
    }
  }

  return result;
}

SERVER_FRAME_API int32_t logic_hpa_discovery::get_scaling_down_expect_replicas() const {
  int32_t result = 0;
  // 指标策略的缩容计算
  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    if (policy_item.second->policy->get_configure_scaling_down_value() <= 0) {
      continue;
    }

    int32_t policy_value = static_cast<int32_t>((policy_item.second->last_scaling_value - 1) /
                                                policy_item.second->policy->get_configure_scaling_down_value()) +
                           1;
    if (policy_value > result) {
      result = policy_value;
    }
  }

  // 自定义缩容计算规则
  {
    custom_provider_guard guard{*this};
    for (auto& provider_ptr : custom_provider_) {
      if (!provider_ptr.second) {
        continue;
      }

      result = provider_ptr.second->get_scaling_down_expect_replicas(*this, result);
    }
  }

  return result;
}

SERVER_FRAME_API void logic_hpa_discovery::add_custom_provider(
    const std::shared_ptr<logic_hpa_discovery_provider>& ptr) {
  if (!ptr) {
    return;
  }

  if (nullptr != custom_provider_guard_) {
    custom_provider_guard_->add_provider[ptr.get()] = ptr;
    custom_provider_guard_->remove_provider.erase(ptr.get());
    return;
  }

  custom_provider_[ptr.get()] = ptr;
}

SERVER_FRAME_API void logic_hpa_discovery::remove_custom_provider(
    const std::shared_ptr<logic_hpa_discovery_provider>& ptr) {
  if (!ptr) {
    return;
  }

  if (nullptr != custom_provider_guard_) {
    custom_provider_guard_->add_provider.erase(ptr.get());
    custom_provider_guard_->remove_provider.insert(ptr.get());
    return;
  }

  custom_provider_.erase(ptr.get());
}

void logic_hpa_discovery::clear_etcd_watcher() {
  if (!etcd_watcher_) {
    return;
  }

  do {
    if (nullptr == controller_->get_app()) {
      FWLOGERROR("[HPA]: Discovery {} can not remove watcher without atapp", etcd_path_);
      break;
    }

    auto etcd_mod = controller_->get_app()->get_etcd_module();
    if (!etcd_mod) {
      FWLOGERROR("[HPA]: Discovery {} can not remove watcher without etcd module", etcd_path_);
      break;
    }

    etcd_mod->get_raw_etcd_ctx().remove_watcher(etcd_watcher_);
  } while (false);

  etcd_watcher_->set_evt_handle(atfw::atapp::etcd_watcher::watch_event_fn_t());
  etcd_watcher_->close();
  etcd_watcher_.reset();
}

void logic_hpa_discovery::clear_etcd_set_value_rpc() {
  if (!etcd_set_value_) {
    return;
  }

  etcd_set_value_->set_priv_data(nullptr);
  etcd_set_value_->set_on_complete(nullptr);
  etcd_set_value_->set_on_error(nullptr);
  etcd_set_value_.reset();
}

bool logic_hpa_discovery::should_ready() {
  if (stoping_) {
    return false;
  }

  if (ready_) {
    return true;
  }

  if (!is_all_policies_pulled()) {
    return false;
  }

  // 任意策略非ready则不能进入ready状态。
  // ready之后才会开始扩缩容策略计算
  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    if (!policy_item.second->policy->is_ready()) {
      return false;
    }
  }

  {
    custom_provider_guard guard{*this};
    for (auto& provider_ptr : custom_provider_) {
      if (!provider_ptr.second) {
        continue;
      }

      if (!provider_ptr.second->is_ready(*this)) {
        return false;
      }
    }
  }

  return true;
}

void logic_hpa_discovery::do_ready() {
  if (ready_) {
    return;
  }
  ready_ = true;

  FWLOGINFO("[HPA]: Discovery {} get ready", etcd_path_);

  for (auto iter = event_on_ready_callback_.callbacks.begin(); iter != event_on_ready_callback_.callbacks.end();) {
    auto cur = iter++;
    if ((*cur).active != logic_hpa_event_active_type::kUnactive && (*cur).callback) {
      (*cur).callback(*this);
    }
  }
}

void logic_hpa_discovery::do_changed_put(data_header& header, const std::string& value) {
  std::string key = static_cast<std::string>(header.subkey);

  do {
    auto iter = etcd_last_values_.find(key);
    if (iter == etcd_last_values_.end()) {
      break;
    }
    if (iter->second.header.create_revision == header.create_revision &&
        iter->second.header.mod_revision == header.mod_revision && iter->second.header.version == header.version) {
      return;
    }

  } while (false);

  data_cache& cache = etcd_last_values_[key];
  {
    auto iter = etcd_last_values_.find(key);
    if (iter != etcd_last_values_.end()) {
      header.subkey = iter->first;
    }
  }

  // 刷新数据
  cache.header.create_revision = header.create_revision;
  cache.header.mod_revision = header.mod_revision;
  cache.header.version = header.version;
  cache.value = value;

  FWLOGDEBUG("[HPA]: Discovery {} update data (subkey={}, create_revision={}, mod_revision={}, version={}): {}",
             this->etcd_path_, header.subkey, header.create_revision, header.mod_revision, header.version, value);

  // 触发回调
  for (auto callback_iter = event_on_changed_callback_.callbacks.begin();
       callback_iter != event_on_changed_callback_.callbacks.end();) {
    auto iter = callback_iter++;
    if (iter->active == logic_hpa_event_active_type::kUnactive) {
      continue;
    }

    if (!iter->callback.callback) {
      continue;
    }

    if (!iter->callback.subkey.empty() && iter->callback.subkey != "/" && !header.subkey.empty()) {
      if (header.subkey.size() < iter->callback.subkey.size()) {
        continue;
      }
      // We only allow directory like subkey watcher
      if (header.subkey[iter->callback.subkey.size() - 1] != '/') {
        continue;
      }

      if (header.subkey.substr(0, iter->callback.subkey.size()) != iter->callback.subkey) {
        continue;
      }
    }

    iter->callback.callback(*this, header, value);
  }
}

void logic_hpa_discovery::do_changed_delete(const data_header& header) {
  std::string key = static_cast<std::string>(header.subkey);

  // 刷新数据
  {
    auto iter = etcd_last_values_.find(key);
    if (iter == etcd_last_values_.end()) {
      return;
    }

    etcd_last_values_.erase(iter);
  }

  FWLOGDEBUG("[HPA]: Discovery {} remove data (subkey={}, create_revision={}, mod_revision={}, version={})",
             this->etcd_path_, header.subkey, header.create_revision, header.mod_revision, header.version);

  // 触发回调
  for (auto callback_iter = event_on_changed_callback_.callbacks.begin();
       callback_iter != event_on_changed_callback_.callbacks.end();) {
    auto iter = callback_iter++;
    if (iter->active == logic_hpa_event_active_type::kUnactive) {
      continue;
    }

    if (!iter->callback.callback) {
      continue;
    }

    if (!iter->callback.subkey.empty() && iter->callback.subkey != "/" && !header.subkey.empty()) {
      if (header.subkey.size() < iter->callback.subkey.size()) {
        continue;
      }
      // We only allow directory like subkey watcher
      if (header.subkey[iter->callback.subkey.size() - 1] != '/') {
        continue;
      }

      if (header.subkey.substr(0, iter->callback.subkey.size()) != iter->callback.subkey) {
        continue;
      }
    }

    iter->callback.callback(*this, header, std::string());
  }
}

void logic_hpa_discovery::reset_pull_policy_waiting_counter() {
  // 重新变为主节点后，需要所有策略都重新拉取成功一次之后才能开始策略值计算。所以这里要重置计数
  pull_policy_waiting_counter_ = 0;
  for (auto& policy_item : policy_data_) {
    if (!policy_item.second) {
      continue;
    }

    if (!policy_item.second->policy) {
      continue;
    }

    if (!policy_item.second->policy->is_event_on_pull_instant_handle_valid(policy_item.second->pull_instant_handle)) {
      continue;
    }

    if (!policy_item.second->policy->can_pulling_available()) {
      continue;
    }

    if (policy_item.second->last_update_timepoint < pull_policy_update_timepoint_) {
      ++pull_policy_waiting_counter_;
    }
  }
}

void logic_hpa_discovery::decrease_pull_policy_waiting_counter() {
  if (pull_policy_waiting_counter_ <= 0) {
    return;
  }

  // 重新变为主节点后，需要所有策略都重新拉取成功一次之后才能开始策略值计算
  --pull_policy_waiting_counter_;
  if (pull_policy_waiting_counter_ > 0) {
    return;
  }

  reset_pull_policy_waiting_counter();

  if (!this->ready_ && this->should_ready()) {
    this->do_ready();
  }
}

void logic_hpa_discovery::increase_pull_policy_waiting_counter() { ++pull_policy_waiting_counter_; }
