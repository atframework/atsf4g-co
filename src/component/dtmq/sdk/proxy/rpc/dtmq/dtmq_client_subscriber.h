// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <nostd/function_ref.h>
#include <nostd/nullability.h>
#include <nostd/type_traits.h>
#include <std/explicit_declare.h>

#include <memory/rc_ptr.h>
#include <time/time_utility.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <rpc/rpc_common_types.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "rpc/dtmq/dtmq_algorithm.h"
#include "rpc/dtmq/dtmq_client_api.h"

namespace google {
namespace protobuf {
class Any;
class Message;
class Descriptor;
}  // namespace protobuf
}  // namespace google

namespace atframework {
namespace dtmq {
class channel_page_info;
class channel_lock_checker;
class channel_subscriber;

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
 * @note 注意：由于底层有共享层IO优化，所以此类型的所有接口都只允许在同一线程下执行，非线程安全
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

  /**
   * @brief 查询本地缓存消息的选项
   */
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

  /// 订阅者就绪事件回调：首次同步到频道数据(快照加载完成)后触发
  using event_callback_on_ready_t = std::function<void(rpc::context& ctx, const ptr_t& subscriber)>;

  /// 频道销毁事件回调：收到服务器下发的销毁通知时触发；另外当 auto_create_channel=false 且服务器心跳
  /// 返回频道不存在(not_found)时，即使订阅者从未就绪也会触发一次。log_sequence 为订阅者本地已知的
  /// 最后一条消息 sequence(从未同步到数据时为0)，destroy_time 为销毁时间点
  using event_callback_on_destroy_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence,
                         std::chrono::system_clock::time_point destroy_time)>;

  /// 频道自定义数据(custom_data)更新事件回调
  using event_callback_on_update_custom_data_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence, const ::google::protobuf::Any& data)>;

  /// 订阅者私有数据(private_data)更新事件回调，仅在 with_private_data=true 时才会收到下发
  using event_callback_on_update_private_data_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, int64_t log_sequence, const ::google::protobuf::Any& data)>;

  /// 频道乐观锁变更事件回调，from 为变更前的锁，to 为变更后的锁
  using event_callback_on_update_optimistic_lock_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelOptimisticLock& from,
                         const ::atfw::dtmq::DChannelOptimisticLock& to)>;

  /// 日志压缩事件回调：服务器压缩移除日志后触发，compact_log_sequence 为被压缩移除到的 sequence
  using event_callback_on_compact_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, int64_t compact_log_sequence)>;

  /// 文本消息接收回调：收到文本消息时触发(仅就绪后触发，就绪前的消息通过快照事件处理)
  using event_callback_on_receive_text_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  /// 事件消息接收回调：收到事件消息时触发(仅就绪后触发)；与按 type_url 注册的回调不互斥，都会触发
  using event_callback_on_receive_event_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  /// 原始消息接收回调：每条原始日志(含文本/事件等所有类型)都会先触发(仅就绪后触发)
  using event_callback_on_receive_raw_message_t =
      std::function<void(rpc::context& ctx, const ptr_t& subscriber, const ::atfw::dtmq::DChannelMessage& data)>;

  /// 批量接收消息后回调：在一次接收完一批消息后触发
  using event_callback_on_receive_batch_message_finished_t = std::function<void(
      rpc::context& ctx, const ptr_t& subscriber, int64_t first_log_sequence, int64_t last_log_sequence)>;

  /// 快照接收回调：分别在开始加载快照和快照加载完成时触发；完成回调的 result_code 为加载结果(0表示成功)
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

  /**
   * @brief 获取频道自定义数据(custom_data)当前已同步的 sequence
   *
   * @return custom_data 的 sequence，未同步过时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_custom_data_sequence() const noexcept;

  /**
   * @brief 获取频道自定义数据(custom_data)当前已同步的内容
   *
   * @return custom_data 内容，未同步过时为默认实例
   */
  DTMQ_PROXY_SDK_API const ::google::protobuf::Any& get_custom_data_content() const noexcept;

  /**
   * @brief 获取订阅者私有数据(private_data)当前已同步的 sequence
   * @note 仅在创建时 with_private_data=true 才会订阅私有数据，否则恒返回0
   *
   * @return private_data 的 sequence，未同步过或未订阅时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_private_data_sequence() const noexcept;

  /**
   * @brief 获取订阅者私有数据(private_data)当前已同步的内容
   * @note 仅在创建时 with_private_data=true 才会订阅私有数据，否则恒返回默认实例
   *
   * @return private_data 内容，未同步过或未订阅时为默认实例
   */
  DTMQ_PROXY_SDK_API const ::google::protobuf::Any& get_private_data_content() const noexcept;

  /**
   * @brief 获取当前频道配置(随服务器下发同步更新)
   *
   * @return 频道配置
   */
  DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelConfigure& get_configure() const noexcept;

  /**
   * @brief 获取当前频道乐观锁
   *
   * @return 乐观锁
   */
  DTMQ_PROXY_SDK_API const atfw::dtmq::DChannelOptimisticLock& get_lock() const noexcept;

  /**
   * @brief 是否已就绪(已同步到频道数据)
   * @note 创建成功后若此接口已返回true，则不会再触发 on_ready 事件
   *
   * @return 是否已就绪
   */
  DTMQ_PROXY_SDK_API bool is_ready() const noexcept;

  /**
   * @brief 频道是否已销毁(destroy_sequence > 0 且不早于 create_sequence)
   *
   * @return 频道是否已销毁
   */
  DTMQ_PROXY_SDK_API bool is_destroyed() const noexcept;

  /**
   * @brief 获取本地私有数据槽位
   * @note 仅保存在本进程内存中，不参与网络同步；可用于上层业务挂载任意本地上下文
   *
   * @return 本地私有数据槽位
   */
  DTMQ_PROXY_SDK_API gsl::span<const uintptr_t> get_local_private_data() const noexcept;

  /**
   * @brief 整体覆盖设置本地私有数据槽位
   * @note 仅保存在本进程内存中，不参与网络同步
   *
   * @param local_private_data 新的槽位内容
   */
  DTMQ_PROXY_SDK_API void set_local_private_data(gsl::span<uintptr_t> local_private_data);

  /**
   * @brief 追加一个本地私有数据槽位
   * @note 仅保存在本进程内存中，不参与网络同步
   *
   * @param local_private_data 要追加的值
   */
  DTMQ_PROXY_SDK_API void append_local_private_data(uintptr_t local_private_data);

  /**
   * @brief 获取频道创建时的 sequence(首个WAL日志的 sequence)
   *
   * @return 创建 sequence，尚未创建时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_create_sequence() const noexcept;

  /**
   * @brief 获取频道创建时间点
   *
   * @return 创建时间点，尚未创建时为零值
   */
  DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point get_create_timepoint() const noexcept;

  /**
   * @brief 获取频道销毁时的 sequence
   *
   * @return 销毁 sequence，未销毁时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_destroy_sequence() const noexcept;

  /**
   * @brief 获取频道销毁时间点
   *
   * @return 销毁时间点，未销毁时为零值
   */
  DTMQ_PROXY_SDK_API std::chrono::system_clock::time_point get_destroy_timepoint() const noexcept;

  /**
   * @brief 获取本地已同步的最后一条消息(日志)的 sequence
   *
   * @return 最后一条消息的 sequence，未同步过时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_last_message_sequence() const noexcept;

  /**
   * @brief 获取本地已被移除(压缩/GC)的最大日志 sequence
   *
   * @return 已移除的最大 sequence，未移除过时为0
   */
  DTMQ_PROXY_SDK_API int64_t get_last_removed_sequence() const noexcept;

  /**
   * @brief 获取本地缓存中 sequence 不早于 start_sequence 的日志数量
   * @note 日志 sequence 只保证递增不保证连续，日志数量不能用 sequence 差值估算
   *
   * @param start_sequence 起始 sequence(包含)，不大于 0 时返回全部缓存日志数量
   * @return 本地缓存中满足条件的日志数量
   */
  DTMQ_PROXY_SDK_API size_t get_cached_log_count(int64_t start_sequence) const noexcept;

  /**
   * @brief 获取创建订阅者时传入的 auto_create_channel 选项
   * @note auto_create_channel=false 时，若服务器确认频道不存在(心跳返回not_found)，会触发 on_destroyed 回调
   *
   * @return auto_create_channel 选项值
   */
  DTMQ_PROXY_SDK_API bool get_option_auto_create_channel() const noexcept;

  /**
   * @brief 获取创建订阅者时传入的 with_private_data 选项
   *
   * @return with_private_data 选项值
   */
  DTMQ_PROXY_SDK_API bool get_option_with_private_data() const noexcept;

  /**
   * @brief 获取频道数据共享层标识，可以认为返回相同标识的频道共享同一底层数据。可以依据这个做一个合包操作
   *
   * @return 频道数据共享层标识
   */
  DTMQ_PROXY_SDK_API uint64_t get_shared_channel_identify() const noexcept;

  /**
   * @brief 获取频道数据共享层订阅者信息，用于传给下面send_update接口
   *
   * @return 频道数据共享层订阅者信息
   */
  DTMQ_PROXY_SDK_API const ::atfw::dtmq::channel_subscriber& get_shared_subscriber_info() const noexcept;

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

  /**
   * @brief 设置/获取频道销毁事件回调
   * @note 触发时机见 event_callback_on_destroy_t 的说明；static 重载用于直接操作共享的 event_callback_set
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_on_destroy_t&& on_destroy);
  DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(const event_callback_on_destroy_t& on_destroy);
  DTMQ_PROXY_SDK_API const event_callback_on_destroy_t& get_event_callback_on_destroyed() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_set_t& event_callback_set,
                                                                 event_callback_on_destroy_t&& on_destroy);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_destroyed(event_callback_set_t& event_callback_set,
                                                                 const event_callback_on_destroy_t& on_destroy);
  static DTMQ_PROXY_SDK_API const event_callback_on_destroy_t& get_event_callback_on_destroyed(
      const event_callback_set_t& event_callback_set) noexcept;

  /**
   * @brief 设置/获取频道自定义数据(custom_data)更新事件回调
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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

  /**
   * @brief 设置/获取订阅者私有数据(private_data)更新事件回调
   * @note 仅在创建时 with_private_data=true 才会收到私有数据下发；static 重载用于直接操作共享的
   *       event_callback_set
   */
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

  /**
   * @brief 设置/获取频道乐观锁变更事件回调
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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

  /**
   * @brief 设置/获取日志压缩事件回调(服务器压缩移除日志后触发)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_on_compact_t&& on_compact);
  DTMQ_PROXY_SDK_API void set_event_callback_on_compact(const event_callback_on_compact_t& on_compact);
  DTMQ_PROXY_SDK_API const event_callback_on_compact_t& get_event_callback_on_compact() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_set_t& event_callback_set,
                                                               event_callback_on_compact_t&& on_compact);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_compact(event_callback_set_t& event_callback_set,
                                                               const event_callback_on_compact_t& on_compact);
  static DTMQ_PROXY_SDK_API const event_callback_on_compact_t& get_event_callback_on_compact(
      const event_callback_set_t& event_callback_set) noexcept;

  /**
   * @brief 设置/获取文本消息接收回调(仅就绪后触发)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(event_callback_on_receive_text_t&& on_receive_text);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(const event_callback_on_receive_text_t& on_receive_text);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_text_t& get_event_callback_on_receive_text() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(event_callback_set_t& event_callback_set,
                                                                    event_callback_on_receive_text_t&& on_receive_text);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_text(
      event_callback_set_t& event_callback_set, const event_callback_on_receive_text_t& on_receive_text);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_text_t& get_event_callback_on_receive_text(
      const event_callback_set_t& event_callback_set) noexcept;

  /**
   * @brief 设置/获取事件消息接收回调(仅就绪后触发；与按 type_url 注册的回调不互斥，都会触发)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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

  /**
   * @brief 按消息类型注册事件回调，收到该类型(protobuf Message)的事件消息时触发
   *
   * @tparam T 事件消息的 protobuf 类型
   * @param on_receive_event 事件回调
   * @note 与 set_event_callback_on_receive_event 设置的通用回调不互斥，都会触发
   */
  template <class T, class FuncType,
            class = atfw::util::nostd::enable_if_t<std::is_base_of<::google::protobuf::Message, T>::value>>
  ATFW_UTIL_SYMBOL_VISIBLE void set_event_callback_on_receive_event_by_message_type(FuncType&& on_receive_event) {
    return set_event_callback_on_receive_event_by_type_url(get_any_type_url(T::descriptor(),
                                                                            []() -> std::string {
                                                                              T m{};
                                                                              return parse_any_type_url(m);
                                                                            }),
                                                           std::forward<FuncType>(on_receive_event));
  }

  /**
   * @brief 按 Any type_url 注册/获取事件回调，收到该 type_url 的事件消息时触发
   *
   * @param type_url 事件消息 Any 的 type_url
   * @param on_receive_event 事件回调
   * @note 与 set_event_callback_on_receive_event 设置的通用回调不互斥，都会触发；static 重载用于直接操作共享的
   *       event_callback_set
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      const std::string& type_url, event_callback_on_receive_event_t&& on_receive_event);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      const std::string& type_url, const event_callback_on_receive_event_t& on_receive_event);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event_by_type_url(
      const std::string& type_url) const noexcept;

  /**
   * @brief 按消息类型注册事件回调的共享集版本(直接操作共享的 event_callback_set)
   *
   * @tparam T 事件消息的 protobuf 类型
   * @param event_callback_set 共享的回调函数组
   * @param on_receive_event 事件回调
   */
  template <class T, class FuncType,
            class = atfw::util::nostd::enable_if_t<std::is_base_of<::google::protobuf::Message, T>::value>>
  static ATFW_UTIL_SYMBOL_VISIBLE void set_event_callback_on_receive_event_by_message_type(
      event_callback_set_t& event_callback_set, FuncType&& on_receive_event) {
    return set_event_callback_on_receive_event_by_type_url(event_callback_set,
                                                           get_any_type_url(T::descriptor(),
                                                                            []() -> std::string {
                                                                              T m{};
                                                                              return parse_any_type_url(m);
                                                                            }),
                                                           std::forward<FuncType>(on_receive_event));
  }

  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      event_callback_set_t& event_callback_set, const std::string& type_url,
      event_callback_on_receive_event_t&& on_receive_event);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_event_by_type_url(
      event_callback_set_t& event_callback_set, const std::string& type_url,
      const event_callback_on_receive_event_t& on_receive_event);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_event_t& get_event_callback_on_receive_event_by_type_url(
      const event_callback_set_t& event_callback_set, const std::string& type_url) noexcept;

  /**
   * @brief 设置/获取批量接收消息回调：每条一批消息接收完之后触发一次
   * @note static 重载用于直接操作共享的 event_callback_set
   */
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_batch_message_finished(
      event_callback_on_receive_batch_message_finished_t&& on_receive_batch_message_finished);
  DTMQ_PROXY_SDK_API void set_event_callback_on_receive_batch_message_finished(
      const event_callback_on_receive_batch_message_finished_t& on_receive_batch_message_finished);
  DTMQ_PROXY_SDK_API const event_callback_on_receive_batch_message_finished_t&
  get_event_callback_on_receive_batch_message_finished() const noexcept;
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_batch_message_finished(
      event_callback_set_t& event_callback_set,
      event_callback_on_receive_batch_message_finished_t&& on_receive_batch_message_finished);
  static DTMQ_PROXY_SDK_API void set_event_callback_on_receive_batch_message_finished(
      event_callback_set_t& event_callback_set,
      const event_callback_on_receive_batch_message_finished_t& on_receive_batch_message_finished);
  static DTMQ_PROXY_SDK_API const event_callback_on_receive_batch_message_finished_t&
  get_event_callback_on_receive_batch_message_finished(const event_callback_set_t& event_callback_set) noexcept;

  /**
   * @brief 设置/获取原始消息接收回调：每条原始日志(含文本/事件等所有类型)都会先触发(仅就绪后触发)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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

  /**
   * @brief 设置/获取快照开始加载事件回调(收到服务器下发的频道快照、尚未写入本地缓存前触发)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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

  /**
   * @brief 设置/获取快照加载完成事件回调(快照写入本地缓存完成后触发，result_code 为加载结果，0表示成功)
   * @note static 重载用于直接操作共享的 event_callback_set
   */
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
   * @brief 发送文本消息
   *
   * @param ctx RPC上下文
   * @param text 消息内容
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_text(
      rpc::context& ctx, gsl::string_view text,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 批量发送文本消息(一次请求按顺序追加多条消息)
   *
   * @param ctx RPC上下文
   * @param texts 消息内容列表(不能为空)
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_text(
      rpc::context& ctx, gsl::span<const gsl::string_view> texts,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 发送事件消息
   *
   * @param ctx RPC上下文
   * @param event_data 消息内容
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_event(
      rpc::context& ctx, ::google::protobuf::Any&& event_data,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 批量发送事件消息(一次请求按顺序追加多条消息)
   *
   * @param ctx RPC上下文
   * @param events 消息内容列表(不能为空)
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_event(
      rpc::context& ctx, google::protobuf::RepeatedPtrField<::google::protobuf::Any>&& events,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 发送销毁频道消息(不是立即销毁，实际销毁生效要等待收到下发通知)
   *
   * @param ctx RPC上下文
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_destroy(
      rpc::context& ctx,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      bool no_wait = false);

  /**
   * @brief 发送乐观锁解锁
   *
   * @param ctx RPC上下文
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_reset_lock(
      rpc::context& ctx,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  struct ATFW_UTIL_SYMBOL_VISIBLE update_option {
    // 是否执行DB保存数据
    bool save = false;
    // 主动压缩数据
    int64_t compact_sequence = 0;
    // 设置状态日志sequence，下发snapshot时会跳过低于这个sequence的stateful日志
    int64_t stateful_sequence = 0;
    // 强制更新其他订阅者信息（注意如果订阅者由client_subscriber管理，这里要传共享层的订阅者信息），走Merge策略
    gsl::span<const ::atfw::dtmq::channel_subscriber*> force_update_subscribers;
    // 如果要更新custom_data，这里传入指针
    const ::google::protobuf::Message* custom_data = nullptr;
    // 如果更新了Custom data，是否忽略主动通知
    bool custom_data_skip_notify = false;
    // 如果要更新private_data，这里传入指针
    const ::google::protobuf::Message* private_data = nullptr;

    ATFW_UTIL_FORCEINLINE update_option() noexcept {}
    ATFW_UTIL_FORCEINLINE update_option(const update_option&) = default;
    ATFW_UTIL_FORCEINLINE update_option(update_option&&) = default;
    ATFW_UTIL_FORCEINLINE update_option& operator=(const update_option&) = default;
    ATFW_UTIL_FORCEINLINE update_option& operator=(update_option&&) = default;
  };

  /**
   * @brief 发送更新数据
   *
   * @param ctx RPC上下文
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_update(
      rpc::context& ctx, update_option options = {},
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 发送原始消息
   *
   * @param ctx RPC上下文
   * @param detail 原始消息详情
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
      rpc::context& ctx, atfw::dtmq::DChannelMessageDetail&& detail,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 批量发送原始消息(一次请求按顺序追加多条消息)
   *
   * @param ctx RPC上下文
   * @param details 原始消息详情列表(不能为空)
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
      rpc::context& ctx, google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessageDetail>&& details,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr =
          nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 查找本地消息缓存
   *
   * @param ctx RPC上下文
   * @param sequence 消息序列号
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
  DTMQ_PROXY_SDK_API bool query_cached_message(
      rpc::context& ctx, atfw::util::nostd::function_ref<bool(const atfw::dtmq::DChannelMessage&)> fn,
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
  using _get_any_type_url_fn = atfw::util::nostd::function_ref<std::string()>;
  static DTMQ_PROXY_SDK_API const std::string& get_any_type_url(const ::google::protobuf::Descriptor* desc,
                                                                _get_any_type_url_fn parse_fn);
  static DTMQ_PROXY_SDK_API std::string parse_any_type_url(const ::google::protobuf::Message& m);

 private:
  struct subscriber_internal_data;
  atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<subscriber_internal_data>> internal_data_;
};

}  // namespace dtmq
}  // namespace rpc
