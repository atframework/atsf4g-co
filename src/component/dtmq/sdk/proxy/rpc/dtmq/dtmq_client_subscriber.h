// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <memory/rc_ptr.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <rpc/rpc_common_types.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "rpc/dtmq/dtmq_algorithm.h"
#include "rpc/dtmq/dtmq_client_api.h"

namespace google {
namespace protobuf {
class Any;
}  // namespace protobuf
}  // namespace google

namespace atframework {
namespace dtmq {
class channel_page_info;
class channel_lock_checker;

class DChannelIdKey;
class DChannelMessage;
class DChannelMessageDetail;
class DChannelSnapshot;
class DChannelOptimisticLock;
class DChannelConfigure;

class SSChannelEventSync;
}  // namespace dtmq
}  // namespace atframework

namespace rpc {
class context;

namespace dtmq {

/**
 * @brief 分布式消息队列客户端订阅者接口包装类
 * @note 注意：由于底层有共享层IO优化，所以被类型的所有接口都只允许在统一线程下执行，非线程安全
 */
class client_subscriber : public atfw::util::memory::enable_shared_rc_from_this<client_subscriber> {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<client_subscriber>;

  struct event_callback_set_t;
  using event_callback_set_ptr_t = atfw::util::memory::strong_rc_ptr<event_callback_set_t>;

  struct ATFW_UTIL_SYMBOL_VISIBLE subscriber_options {
    DTMQ_PROXY_SDK_API subscriber_options(std::string&& input_subscriber_key);
    DTMQ_PROXY_SDK_API subscriber_options(const std::string& input_subscriber_key);
    DTMQ_PROXY_SDK_API ~subscriber_options();

    DTMQ_PROXY_SDK_API subscriber_options(const subscriber_options& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options(subscriber_options&& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options& operator=(const subscriber_options& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options& operator=(subscriber_options&& other) = default;

    // 底层有优化层多个订阅者订阅同一频道时合并底层订阅，所以这个仅用于标识当前client_subscriber
    // 不影响底层实际发送订阅的 subscriber_key
    std::string subscriber_key;

    // 频道不存在时自动创建，默认为 true
    bool auto_create_channel;

    // 是否附带私有数据通知，默认为 false
    bool with_private_data;

    // 可以设置 event_callback_set 来让多个 client_subscriber 共享同一组事件回调函数，避免浪费
    event_callback_set_ptr_t event_callback_set;
  };

  struct ATFW_UTIL_SYMBOL_VISIBLE query_options {
    int64_t start_sequence = 0;  // include
    int64_t end_sequence = 0;    // exclude,0表示不限制
    int64_t max_count = 0;       // 最大数量,0表示不限制

    ATFW_UTIL_FORCEINLINE query_options() {}

    ATFW_UTIL_FORCEINLINE query_options(const query_options&) = default;
    ATFW_UTIL_FORCEINLINE query_options(query_options&&) = default;
    ATFW_UTIL_FORCEINLINE query_options& operator=(const query_options&) = default;
    ATFW_UTIL_FORCEINLINE query_options& operator=(query_options&&) = default;
  };

  using event_callback_on_ready_t = std::function<void(rpc::context& ctx, const ptr_t& subscriber)>;

  using event_callback_on_destroy_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence,
                         std::chrono::system_clock::time_point destroy_time)>;

  using event_callback_on_update_custom_data_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence, const ::google::protobuf::Any& data)>;

  using event_callback_on_update_private_data_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence, const ::google::protobuf::Any& data)>;

  using event_callback_on_update_optimistic_lock_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelOptimisticLock& from,
                         const ::atfw::dtmq::DChannelOptimisticLock& to)>;

  using event_callback_on_compact_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, int64_t compact_log_sequence)>;

  using event_callback_on_receive_text_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  using event_callback_on_receive_event_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  using event_callback_on_receive_raw_message_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  using event_callback_on_receive_snapshot_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelSnapshot& data, int32_t result_code)>;

 private:
  struct ctor_guard;

 public:
  client_subscriber(const client_subscriber&) = delete;
  client_subscriber(client_subscriber&&) = delete;
  client_subscriber& operator=(const client_subscriber&) = delete;
  client_subscriber& operator=(client_subscriber&&) = delete;

  explicit client_subscriber(ctor_guard&);

  DTMQ_PROXY_SDK_API ~client_subscriber();

  /**
   * @brief 创建一组空的事件回调函数集合，供多个 client_subscriber 共享使用，避免重复创建
   *
   * @return 事件回调函数集合
   */
  static DTMQ_PROXY_SDK_API event_callback_set_ptr_t create_event_callback_set();

  /**
   * @brief 创建频道订阅者，如果参数错误返回空指针
   *
   * @param channel_key 频道Key
   * @param options 订阅者信息
   * @note 请注意，由于底层有共享层IO优化，创建成功后要通过 is_ready() 接口判定是否已经就绪，如果已经就绪则不会再触发
   *       on_ready 事件
   * @return 成功则返回订阅对象智能指针
   */
  static DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<ptr_t> create(const atfw::dtmq::DChannelIdKey& channel_key,
                                                                      const subscriber_options& options);

  /**
   * @brief 使用订阅者的服务必须接入处理接收到的频道事件
   *
   * @param ctx RPC上下文
   * @param from_server_id 来源服务ID
   * @param event_sync 收到的事件同步消息
   * @note 此接口都不可重入
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API static rpc::result_code_type global_receive_channel_event(
      rpc::context& ctx, uint64_t from_server_id, const atfw::dtmq::SSChannelEventSync& event_sync);

  /**
   * @brief 使用订阅者的服务必须接入定时器Tick调用
   *
   * @param ctx RPC上下文
   * @note 此接口都不可重入
   * @return 0表示无任何定时器事件触发，< 0表示错误码，> 0表示触发的定时器事件数量
   */
  DTMQ_PROXY_SDK_API static int32_t global_tick(rpc::context& ctx);

  /**
   * @brief 等待一轮心跳发送完成，主要用于刚创建完订阅者，如果需要等待第一次心跳事件完成以便获取完第一轮数据的场景
   *
   * @param ctx RPC上下文
   * @return 0或错误码
   */
  DTMQ_PROXY_SDK_API static rpc::result_code_type global_await_pending_heartbeat(rpc::context& ctx);

  /**
   * @brief 是否有等待的心跳发送
   *
   * @return 是否有等待的心跳发送
   */
  DTMQ_PROXY_SDK_API static bool global_has_pending_heartbeat() noexcept;

  /**
   * @brief 是否正在发送心跳
   *
   * @return 是否正在发送心跳
   */
  DTMQ_PROXY_SDK_API static bool global_is_sending_heartbeat() noexcept;

  /**
   * @brief Get the channel key object
   *
   * @return channel key
   */
  DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept;

  /**
   * @brief Get the subscriber key

   * @return subscriber key
   */
  DTMQ_PROXY_SDK_API const std::string& get_subscriber_key() const noexcept;

  /**
   * @brief Get the last heartbeat timepoint

   * @return last heartbeat timepoint
   */
  DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point get_last_heartbeat_timepoint() const noexcept;

  /**
   * @brief Get the last heartbeat sequence

   * @return last heartbeat sequence
   */
  DTMQ_PROXY_SDK_API int64_t get_last_heartbeat_sequence() const noexcept;

  DTMQ_PROXY_SDK_API int64_t get_custom_data_sequence() const noexcept;

  DTMQ_PROXY_SDK_API const ::google::protobuf::Any& get_custom_data_content() const noexcept;

  DTMQ_PROXY_SDK_API int64_t get_private_data_sequence() const noexcept;

  DTMQ_PROXY_SDK_API const ::google::protobuf::Any& get_private_data_content() const noexcept;

  DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelConfigure& get_configure() const noexcept;

  DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept;

  DTMQ_PROXY_SDK_API bool is_ready() const noexcept;

  DTMQ_PROXY_SDK_API bool is_destroyed() const noexcept;

  DTMQ_PROXY_SDK_API gsl::span<const uintptr_t> get_local_private_data() const noexcept;

  DTMQ_PROXY_SDK_API void set_local_private_data(gsl::span<uintptr_t> local_private_data);

  DTMQ_PROXY_SDK_API void append_local_private_data(uintptr_t local_private_data);

  DTMQ_PROXY_SDK_API int64_t get_create_sequence() const noexcept;

  DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point get_create_timepoint() const noexcept;

  DTMQ_PROXY_SDK_API int64_t get_destroy_sequence() const noexcept;

  DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point get_destroy_timepoint() const noexcept;

  DTMQ_PROXY_SDK_API int64_t get_last_message_sequence() const noexcept;

  DTMQ_PROXY_SDK_API int64_t get_last_removed_sequence() const noexcept;

  DTMQ_PROXY_SDK_API bool get_option_auto_create_channel() const noexcept;

  DTMQ_PROXY_SDK_API bool get_option_with_private_data() const noexcept;

  /**
   * @brief 获取频道数据共享层标识，可以认为返回相同标识的频道共享同一底层数据。可以依据这个做一个合包操作
   *
   * @return 频道数据共享层标识
   */
  DTMQ_PROXY_SDK_API uint64_t get_shared_channel_identify() const noexcept;

  /**
   * @brief 设置共享的回调函数组
   *
   * @param event_callbacl_set 整个回调函数组
   */
  DTMQ_PROXY_SDK_API void set_shared_event_callback_set(const event_callback_set_ptr_t& event_callbacl_set);

  /**
   * @brief 获取共享的回调函数组
   *
   * @return 当前绑定的整个回调函数组
   */
  DTMQ_PROXY_SDK_API const atfw::util::nostd::nullable<event_callback_set_ptr_t>& get_shared_event_callback_set()
      const noexcept;

  /**
   * @brief Set the event callback on ready object
   *
   * @param on_ready callback
   * @note 请注意，由于底层有共享层IO优化，创建成功后要通过 is_ready() 接口判定是否已经就绪，如果已经就绪则不会再触发
   *       on_ready 事件
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_ready(event_callback_on_ready_t&& on_ready);

  /**
   * @brief Set the event callback on ready object
   *
   * @param on_ready callback
   * @note 请注意，由于底层有共享层IO优化，创建成功后要通过 is_ready() 接口判定是否已经就绪，如果已经就绪则不会再触发
   *       on_ready 事件
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_ready(const event_callback_on_ready_t& on_ready);

  DTMQ_PROXY_SDK_API const event_callback_on_ready_t& get_event_callback_on_ready() const noexcept;

  /**
   * @brief Set the event callback on ready object
   *
   * @param on_ready callback
   * @note 请注意，由于底层有共享层IO优化，创建成功后要通过 is_ready() 接口判定是否已经就绪，如果已经就绪则不会再触发
   *       on_ready 事件
   */
  static DTMQ_PROXY_SDK_API void set_event_callback_on_ready(event_callback_set_t& event_callback_set,
                                                             event_callback_on_ready_t&& on_ready);

  /**
   * @brief Set the event callback on ready object
   *
   * @param on_ready callback
   * @note 请注意，由于底层有共享层IO优化，创建成功后要通过 is_ready() 接口判定是否已经就绪，如果已经就绪则不会再触发
   *       on_ready 事件
   */
  static DTMQ_PROXY_SDK_API void set_event_callback_on_ready(event_callback_set_t& event_callback_set,
                                                             const event_callback_on_ready_t& on_ready);
  static DTMQ_PROXY_SDK_API const event_callback_on_ready_t& get_event_callback_on_ready(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_on_destroy_t&& on_destroy);
  DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(const event_callback_on_destroy_t& on_destroy);
  DTMQ_PROXY_SDK_API const event_callback_on_destroy_t& get_event_callback_on_destroyed() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_set_t& event_callback_set,
                                                                 event_callback_on_destroy_t&& on_destroy);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_set_t& event_callback_set,
                                                                 const event_callback_on_destroy_t& on_destroy);
  static DTMQ_PROXY_SDK_API const event_callback_on_destroy_t& get_event_callback_on_destroyed(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_update_custom_data(
      event_callback_on_update_custom_data_t&& on_update_custom_data);
  DTMQ_PROXY_SDK_API void set_event_callback_on_update_custom_data(
      const event_callback_on_update_custom_data_t& on_update_custom_data);
  DTMQ_PROXY_SDK_API const event_callback_on_update_custom_data_t& get_event_callback_on_update_custom_data()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_custom_data(
      event_callback_set_t& event_callback_set, event_callback_on_update_custom_data_t&& on_update_custom_data);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_custom_data(
      event_callback_set_t& event_callback_set, const event_callback_on_update_custom_data_t& on_update_custom_data);
  static DTMQ_PROXY_SDK_API const event_callback_on_update_custom_data_t& get_event_callback_on_update_custom_data(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_update_private_data(
      event_callback_on_update_private_data_t&& on_update_private_data);
  DTMQ_PROXY_SDK_API void set_event_callback_on_update_private_data(
      const event_callback_on_update_private_data_t& on_update_private_data);
  DTMQ_PROXY_SDK_API const event_callback_on_update_private_data_t& get_event_callback_on_update_private_data()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_private_data(
      event_callback_set_t& event_callback_set, event_callback_on_update_private_data_t&& on_update_private_data);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_private_data(
      event_callback_set_t& event_callback_set, const event_callback_on_update_private_data_t& on_update_private_data);
  static DTMQ_PROXY_SDK_API const event_callback_on_update_private_data_t& get_event_callback_on_update_private_data(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_update_optimistic_lock(
      event_callback_on_update_optimistic_lock_t&& on_update_optimistic_lock);
  DTMQ_PROXY_SDK_API void set_event_callback_on_update_optimistic_lock(
      const event_callback_on_update_optimistic_lock_t& on_update_optimistic_lock);
  DTMQ_PROXY_SDK_API const event_callback_on_update_optimistic_lock_t& get_event_callback_on_update_optimistic_lock()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_optimistic_lock(
      event_callback_set_t& event_callback_set, event_callback_on_update_optimistic_lock_t&& on_update_optimistic_lock);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_update_optimistic_lock(
      event_callback_set_t& event_callback_set,
      const event_callback_on_update_optimistic_lock_t& on_update_optimistic_lock);
  static DTMQ_PROXY_SDK_API const event_callback_on_update_optimistic_lock_t&
  get_event_callback_on_update_optimistic_lock(const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_on_compact_t&& on_compact);
  DTMQ_PROXY_SDK_API void set_event_callback_on_compact(const event_callback_on_compact_t& on_compact);
  DTMQ_PROXY_SDK_API const event_callback_on_compact_t& get_event_callback_on_compact() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_set_t& event_callback_set,
                                                               event_callback_on_compact_t&& on_compact);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_set_t& event_callback_set,
                                                               const event_callback_on_compact_t& on_compact);
  static DTMQ_PROXY_SDK_API const event_callback_on_compact_t& get_event_callback_on_compact(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(event_callback_on_receive_text_t&& on_receive_text);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(const event_callback_on_receive_text_t& on_receive_text);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_text_t& get_event_callback_on_receive_text() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(event_callback_set_t& event_callback_set,
                                                                    event_callback_on_receive_text_t&& on_receive_text);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(
      event_callback_set_t& event_callback_set, const event_callback_on_receive_text_t& on_receive_text);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_text_t& get_event_callback_on_receive_text(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event(event_callback_on_receive_event_t&& on_receive_event);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event(
      const event_callback_on_receive_event_t& on_receive_event);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event(
      event_callback_set_t& event_callback_set, event_callback_on_receive_event_t&& on_receive_event);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event(
      event_callback_set_t& event_callback_set, const event_callback_on_receive_event_t& on_receive_event);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      const std::string& type_url, event_callback_on_receive_event_t&& on_receive_event);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      const std::string& type_url, const event_callback_on_receive_event_t& on_receive_event);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event_by_type_url(
      const std::string& type_url) const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      event_callback_set_t& event_callback_set, const std::string& type_url,
      event_callback_on_receive_event_t&& on_receive_event);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      event_callback_set_t& event_callback_set, const std::string& type_url,
      const event_callback_on_receive_event_t& on_receive_event);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event_by_type_url(
      const event_callback_set_t& event_callback_set, const std::string& type_url) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_raw_message(
      event_callback_on_receive_raw_message_t&& on_receive_raw_message);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_raw_message(
      const event_callback_on_receive_raw_message_t& on_receive_raw_message);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_raw_message_t& get_event_callback_on_receive_raw_message()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_raw_message(
      event_callback_set_t& event_callback_set, event_callback_on_receive_raw_message_t&& on_receive_raw_message);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_raw_message(
      event_callback_set_t& event_callback_set, const event_callback_on_receive_raw_message_t& on_receive_raw_message);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_raw_message_t& get_event_callback_on_receive_raw_message(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_start(
      event_callback_on_receive_snapshot_t&& on_receive_snapshot_start);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_start(
      const event_callback_on_receive_snapshot_t& on_receive_snapshot_start);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_snapshot_t& get_event_callback_on_receive_snapshot_start()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_start(
      event_callback_set_t& event_callback_set, event_callback_on_receive_snapshot_t&& on_receive_snapshot_start);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_start(
      event_callback_set_t& event_callback_set, const event_callback_on_receive_snapshot_t& on_receive_snapshot_start);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_snapshot_t& get_event_callback_on_receive_snapshot_start(
      const event_callback_set_t& event_callback_set) noexcept;

  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_finished(
      event_callback_on_receive_snapshot_t&& on_receive_snapshot_finished);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_finished(
      const event_callback_on_receive_snapshot_t& on_receive_snapshot_finished);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_snapshot_t& get_event_callback_on_receive_snapshot_finished()
      const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_finished(
      event_callback_set_t& event_callback_set, event_callback_on_receive_snapshot_t&& on_receive_snapshot_finished);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_snapshot_finished(
      event_callback_set_t& event_callback_set,
      const event_callback_on_receive_snapshot_t& on_receive_snapshot_finished);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_snapshot_t& get_event_callback_on_receive_snapshot_finished(
      const event_callback_set_t& event_callback_set) noexcept;

  /**
   * @brief 发送消息
   *
   * @param ctx RPC上下文
   * @param detail 消息详情
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
      rpc::context& ctx, atfw::dtmq::DChannelMessageDetail&& detail,
      std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr = nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 查找本地消息缓存
   *
   * @param ctx RPC上下文
   * @param fn 如果查找到消息，则会调用此回调函数，传入消息对象
   * @return 如果查找到消息，返回 true， 否则返回 false
   */
  DTMQ_PROXY_SDK_API bool find_cached_message(
      rpc::context& ctx, int64_t sequence,
      atfw::util::nostd::function_ref<void(const atfw::dtmq::DChannelMessage&)> fn) const noexcept;

  /**
   * @brief 查找消息，如果本地缓存未准备好，会去服务器拉取
   *
   * @param ctx RPC上下文
   * @param sequence 消息序列号
   * @param msg 消息对象
   * @return rpc::result_code_type 查找结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type find_message(rpc::context& ctx,
                                                                                     int64_t sequence,
                                                                                     atfw::dtmq::DChannelMessage& msg);

  /**
   * @brief 查询消息
   *
   * @param ctx RPC上下文
   * @param fn 如果查找到消息，则会调用此回调函数，传入消息对象
   * @param options 查询数据的选项
   * @return 如果还有后续消息，返回true，否则返回false
   */
  DTMQ_PROXY_SDK_API bool query_message(rpc::context& ctx,
                                        atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)> fn,
                                        query_options options = {}) const noexcept;

  /**
   * @brief 分页查询消息
   *
   * @param ctx RPC上下文
   * @param page_info 分页信息
   * @param msgs 消息列表
   * @return rpc::result_code_type 查询结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type page_query_message(
      rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
      google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs);

 private:
  struct subscriber_internal_data;
  atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<subscriber_internal_data>> internal_data_;
};

}  // namespace dtmq
}  // namespace rpc
