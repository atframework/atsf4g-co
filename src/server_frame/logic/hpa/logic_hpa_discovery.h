// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <network/http_request.h>

#include <atframe/etcdcli/etcd_watcher.h>

#include <chrono>
#include <ctime>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "logic/hpa/logic_hpa_data_type.h"

class logic_hpa_controller;

class logic_hpa_policy;

enum class logic_hpa_discovery_watch_mode : int8_t {
  kDirectory = 0,
  kExactly = 1,
};

struct logic_hpa_discovery_semantic_conventions {
  static constexpr const char* kLogicHpaDiscoveryDomainDefault = "default";
  static constexpr const char* kLogicHpaDiscoveryDomainCustom = "custom";
};

class logic_hpa_discovery;

class SERVER_FRAME_API logic_hpa_discovery_provider {
 protected:
  logic_hpa_discovery_provider();

 public:
  virtual ~logic_hpa_discovery_provider();

  virtual bool is_ready(const logic_hpa_discovery&);

  virtual int32_t get_non_native_cloud_replicas(const logic_hpa_discovery&, int32_t previous_result);

  virtual int32_t get_scaling_up_expect_replicas(const logic_hpa_discovery&, int32_t previous_result);

  virtual int32_t get_scaling_down_expect_replicas(const logic_hpa_discovery&, int32_t previous_result);
};

class logic_hpa_discovery_setup_policy_accessor {
 private:
  logic_hpa_discovery_setup_policy_accessor();
  ~logic_hpa_discovery_setup_policy_accessor();

  friend class logic_hpa_controller;
};

class logic_hpa_discovery {
 public:
  struct ATFW_UTIL_SYMBOL_VISIBLE data_header {
    gsl::string_view subkey;
    int64_t create_revision;
    int64_t mod_revision;
    int64_t version;
  };

  using event_callback_on_ready = std::function<void(logic_hpa_discovery&)>;
  using event_callback_on_changed = std::function<void(logic_hpa_discovery&, const data_header&, const std::string&)>;

  struct ATFW_UTIL_SYMBOL_VISIBLE data_change_listener {
    event_callback_on_changed callback;
    std::string subkey;
  };

  template <class CallbackType>
  struct ATFW_UTIL_SYMBOL_VISIBLE event_callback_data {
    CallbackType callback;
    logic_hpa_event_active_type active;

    template <class FactorType>
    inline event_callback_data(FactorType&& fn, logic_hpa_event_active_type input_active)
        : callback(std::forward<FactorType>(fn)), active(input_active) {}
  };

  template <class CallbackDataType>
  struct ATFW_UTIL_SYMBOL_VISIBLE event_callback_handle {
    typename std::list<event_callback_data<CallbackDataType>>::iterator iterator;
    int64_t version;

    ATFW_UTIL_FORCEINLINE event_callback_handle(typename std::list<event_callback_data<CallbackDataType>>::iterator it,
                                                int64_t v)
        : iterator(it), version(v) {}

    event_callback_handle(const event_callback_handle&) = delete;
    ATFW_UTIL_FORCEINLINE event_callback_handle(event_callback_handle&& other) noexcept
        : iterator(other.iterator), version(other.version) {
      other.version = 0;
    }

    event_callback_handle& operator=(const event_callback_handle&) = delete;
    ATFW_UTIL_FORCEINLINE event_callback_handle& operator=(event_callback_handle&& other) noexcept {
      iterator = other.iterator;
      version = other.version;
      other.version = 0;
      return *this;
    }
  };

  using event_callback_on_ready_handle = event_callback_handle<event_callback_on_ready>;
  using event_callback_on_changed_handle = event_callback_handle<data_change_listener>;

 private:
  struct policy_data;

  UTIL_DESIGN_PATTERN_NOCOPYABLE(logic_hpa_discovery);
  UTIL_DESIGN_PATTERN_NOMOVABLE(logic_hpa_discovery);

  struct custom_provider_guard;

 public:
  SERVER_FRAME_API static std::string make_path(gsl::string_view key, gsl::string_view domain = {});

  SERVER_FRAME_API logic_hpa_discovery(logic_hpa_controller& controller, gsl::string_view key, gsl::string_view domain);

  SERVER_FRAME_API ~logic_hpa_discovery();

  SERVER_FRAME_API int tick();

  SERVER_FRAME_API void stop();

  ATFW_UTIL_FORCEINLINE bool is_stoping() const noexcept { return stoping_; }

  SERVER_FRAME_API bool is_stopped() const noexcept;

  ATFW_UTIL_FORCEINLINE bool is_ready() const noexcept { return ready_; }

  // 内联函数，检查是否所有的策略都拉取到过数据
  ATFW_UTIL_FORCEINLINE bool is_all_policies_pulled() const noexcept { return pull_policy_waiting_counter_ <= 0; }

  // 内部接口: 添加一个拉取策略，由配置生成，不需要手动调用
  SERVER_FRAME_API void add_pull_policy(std::shared_ptr<logic_hpa_policy> policy,
                                        logic_hpa_discovery_setup_policy_accessor& accessor);

  // 内部接口: 重置策略设置，由内部事件触发，不需要手动调用
  SERVER_FRAME_API void reset_policy(logic_hpa_discovery_setup_policy_accessor& accessor);

  // 获取当前策略的活跃状态
  SERVER_FRAME_API logic_hpa_event_active_type get_policy_active() const noexcept;

  // 设置策略的活跃状态
  SERVER_FRAME_API void set_policy_active(logic_hpa_event_active_type active) noexcept;

  // 遍历并处理每个策略，提供一个函数引用作为回调，包含策略本身、最后的值和时间点
  SERVER_FRAME_API void foreach_policy(util::nostd::function_ref<bool(const logic_hpa_policy&, int64_t last_value,
                                                                      std::chrono::system_clock::time_point)>
                                           fn);

  /**
   * @brief 设置下发策略的监听模式，目录监听或精确Key监听
   *
   * @param mode 监听模式
   * @return 成功则返回true
   */
  SERVER_FRAME_API bool watch(logic_hpa_discovery_watch_mode mode);

  /**
   * @brief 设置策略上报的的内容
   *
   * @param value 值
   * @param subkey 是否在子目录里（子目录路径）
   * @return 成功则返回true
   */
  SERVER_FRAME_API bool set_value(std::string&& value, gsl::string_view subkey = {});

  /**
   * @brief 是否上报策略的请求正在进行中
   *
   * @return 正在上报策略并且未完成返回true
   */
  SERVER_FRAME_API bool is_setting_value() const noexcept;

  /**
   * @brief 设置私有数据
   *
   * @param priv_data 私有数据
   */
  SERVER_FRAME_API void set_private_data(void* priv_data) noexcept;

  /**
   * @brief 获取私有数据
   *
   * @param priv_data 私有数据
   */
  SERVER_FRAME_API void* get_private_data() const noexcept;

  /**
   * @brief 获取最终生成的策略上报路径PATH(etcd托管)
   *
   * @return 上报路径PATH
   */
  ATFW_UTIL_FORCEINLINE const std::string& get_etcd_path() const noexcept { return etcd_path_; }

  ATFW_UTIL_FORCEINLINE const logic_hpa_controller& get_controller() const noexcept { return *controller_; }
  ATFW_UTIL_FORCEINLINE logic_hpa_controller& get_controller() noexcept { return *controller_; }

  // =================== 事件监听接口 - begin ===================

  SERVER_FRAME_API event_callback_on_ready_handle add_event_on_ready(
      event_callback_on_ready fn, logic_hpa_event_active_type active = logic_hpa_event_active_type::kActive);
  SERVER_FRAME_API void remove_event_on_ready(event_callback_on_ready_handle& handle);
  SERVER_FRAME_API void set_event_on_ready_active(event_callback_on_ready_handle& handle,
                                                  logic_hpa_event_active_type active);
  SERVER_FRAME_API void clear_event_on_ready();
  SERVER_FRAME_API bool is_event_on_ready_handle_valid(const event_callback_on_ready_handle& handle);

  SERVER_FRAME_API event_callback_on_changed_handle
  add_event_on_changed(event_callback_on_changed fn, gsl::string_view subkey = {},
                       logic_hpa_event_active_type active = logic_hpa_event_active_type::kActive);
  SERVER_FRAME_API void remove_event_on_changed(event_callback_on_changed_handle& handle);
  SERVER_FRAME_API void set_event_on_changed_active(event_callback_on_changed_handle& handle,
                                                    logic_hpa_event_active_type active);

  SERVER_FRAME_API void clear_event_on_changed();
  SERVER_FRAME_API bool is_event_on_changed_handle_valid(const event_callback_on_changed_handle& handle);

  // ------------------- 事件监听接口 - end -------------------

  /**
   * @brief 计算非云原生模式的当前节点对应副本数
   *
   * @param previous_result 上一个值
   * @return 非云原生模式的当前节点对应副本数
   */
  SERVER_FRAME_API int32_t get_non_native_cloud_replicas(int32_t previous_result) const;

  /**
   * @brief 获取扩容策略预期的副本数
   *
   * @return 扩容策略预期的副本数
   */
  SERVER_FRAME_API int32_t get_scaling_up_expect_replicas() const;

  /**
   * @brief 获取缩容策略预期的副本数
   *
   * @return 扩容策略预期的副本数
   */
  SERVER_FRAME_API int32_t get_scaling_down_expect_replicas() const;

  /**
   * @brief 注册自定义HPA副本数算法
   *
   * @param ptr 算法模块指针
   */
  SERVER_FRAME_API void add_custom_provider(const std::shared_ptr<logic_hpa_discovery_provider>& ptr);

  /**
   * @brief 注册自定义HPA副本数算法
   *
   * @param ptr 算法模块指针
   */
  template <class Provider, class = atfw::util::nostd::enable_if_t<std::is_base_of<
                                logic_hpa_discovery_provider, atfw::util::nostd::decay_t<Provider>>::value>>
  ATFW_UTIL_FORCEINLINE void add_custom_provider(const std::shared_ptr<Provider>& ptr) {
    add_custom_provider(std::static_pointer_cast<logic_hpa_discovery_provider>(ptr));
  }

  /**
   * @brief 移除自定义HPA副本数算法
   *
   * @param ptr 算法模块指针
   */
  SERVER_FRAME_API void remove_custom_provider(const std::shared_ptr<logic_hpa_discovery_provider>& ptr);

  /**
   * @brief 移除自定义HPA副本数算法
   *
   * @param ptr 算法模块指针
   */
  template <class Provider, class = atfw::util::nostd::enable_if_t<std::is_base_of<
                                logic_hpa_discovery_provider, atfw::util::nostd::decay_t<Provider>>::value>>
  ATFW_UTIL_FORCEINLINE void remove_custom_provider(const std::shared_ptr<Provider>& ptr) {
    remove_custom_provider(std::static_pointer_cast<logic_hpa_discovery_provider>(ptr));
  }

 private:
  void clear_etcd_watcher();
  void clear_etcd_set_value_rpc();

  bool should_ready();
  void do_ready();

  void do_changed_put(data_header& header, const std::string& value);

  void do_changed_delete(const data_header& header);

  void reset_pull_policy_waiting_counter();
  void decrease_pull_policy_waiting_counter();
  void increase_pull_policy_waiting_counter();

 private:
  gsl::not_null<logic_hpa_controller*> controller_;
  bool stoping_;
  bool ready_;
  logic_hpa_event_active_type pull_policy_active_;
  std::chrono::system_clock::time_point pull_policy_update_timepoint_;
  int64_t pull_policy_waiting_counter_;
  void* private_data_;
  time_t last_tick_;

  std::string etcd_path_;
  atfw::atapp::etcd_watcher::ptr_t etcd_watcher_;
  logic_hpa_discovery_watch_mode etcd_watch_mode_;
  atfw::util::network::http_request::ptr_t etcd_set_value_;

  struct data_cache {
    data_header header;
    std::string value;
  };
  std::unordered_map<std::string, data_cache> etcd_last_values_;

  std::unordered_map<std::string, std::shared_ptr<policy_data>> policy_data_;

  mutable std::unordered_map<logic_hpa_discovery_provider*, std::shared_ptr<logic_hpa_discovery_provider>>
      custom_provider_;
  mutable custom_provider_guard* custom_provider_guard_;

  template <class CallbackType>
  struct event_callback_set {
    std::list<event_callback_data<CallbackType>> callbacks;
    int64_t version = 0;

    ATFW_UTIL_FORCEINLINE event_callback_set() {}
  };
  using event_on_ready_callback_set = event_callback_set<event_callback_on_ready>;

  event_on_ready_callback_set event_on_ready_callback_;

  using event_on_changed_callback_set = event_callback_set<data_change_listener>;
  event_on_changed_callback_set event_on_changed_callback_;
};
