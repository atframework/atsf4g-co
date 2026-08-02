// Copyright 2021 atframework
// Created by owent on 2016/10/4.
//

#pragma once

#include <config/compile_optimize.h>

#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <design_pattern/noncopyable.h>
#include <gsl/select-gsl.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/arena.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <std/explicit_declare.h>

#include <memory/object_stl_unordered_map.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_type_traits.h"
#include "rpc/rpc_context.h"

PROJECT_NAMESPACE_BEGIN
class table_all_message;
PROJECT_NAMESPACE_END

struct db_message_t;

namespace rpc {

ATFW_UTIL_FORCEINLINE std::chrono::system_clock::duration make_duration(
    const std::chrono::system_clock::duration &value) noexcept {
  return value;
}

template <class Rep, class Period>
ATFW_UTIL_FORCEINLINE std::chrono::system_clock::duration make_duration(
    const std::chrono::duration<Rep, Period> &value) noexcept {
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(value);
}

ATFW_UTIL_FORCEINLINE std::chrono::system_clock::duration make_duration(
    const google::protobuf::Duration &value) noexcept {
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{value.seconds()}) +
         std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds{value.nanos()});
}

template <class Rep, class Period>
ATFW_UTIL_FORCEINLINE std::chrono::system_clock::duration make_duration_or_default(
    const google::protobuf::Duration &value, const std::chrono::duration<Rep, Period> &default_value) noexcept {
  if (value.seconds() > 0 || value.nanos() > 0) {
    return make_duration(value);
  }

  return make_duration(default_value);
}

/**
 * @brief sleep and wait a moment
 *
 * @param timeout
 * @return future of 0 or error code
 */
SERVER_FRAME_API result_code_type wait(context &ctx, std::chrono::system_clock::duration timeout);

/**
 * @brief sleep and wait a moment
 *
 * @tparam Rep
 * @tparam Period
 * @param timeout
 * @return future of 0 or error code
 */
template <class Rep, class Period>
ATFW_UTIL_FORCEINLINE result_code_type wait(context &ctx, std::chrono::duration<Rep, Period> timeout) {
  return wait(ctx, make_duration(timeout));
}

SERVER_FRAME_API result_code_type wait(context &ctx, atframework::SSMsg &msg, const dispatcher_await_options &options);
SERVER_FRAME_API result_code_type wait(context &ctx, db_message_t &msg, const dispatcher_await_options &options);

/**
 * @brief wait for multiple messages
 *
 * @param waiters sequences of waiting messages
 * @param received received messages
 * @param wakeup_count wakeup and return after got this count of messages(0 means wait all)
 * @return future of 0 or error code
 */
SERVER_FRAME_API result_code_type wait(context &ctx, const std::unordered_set<dispatcher_await_options> &waiters,
                                       std::unordered_map<uint64_t, atframework::SSMsg> &received,
                                       size_t wakeup_count = 0);

/**
 * @brief wait for multiple messages
 *
 * @param waiters sequences of waiting messages
 * @param received received messages
 * @param wakeup_count wakeup and return after got this count of messages(0 means wait all)
 * @return future of 0 or error code
 */
SERVER_FRAME_API result_code_type wait(context &ctx, const std::unordered_set<dispatcher_await_options> &waiters,
                                       std::unordered_map<uint64_t, atframework::SSMsg *> &received,
                                       size_t wakeup_count = 0);

/**
 * @brief foreach received multiple messages
 *
 * @param ctx context
 * @param received received messages
 * @param rpc_name name of the RPC call
 * @param callback callback to process each message
 * @return future of 0 or error code
 */

template <class TResponseMessage, class = atfw::util::nostd::enable_if_t<
                                      std::is_base_of<google::protobuf::MessageLite, TResponseMessage>::value>>
ATFW_UTIL_SYMBOL_VISIBLE void foreach_received_message(
    context &ctx, const std::unordered_map<uint64_t, atframework::SSMsg *> &received, gsl::string_view rpc_name,
    atfw::util::nostd::function_ref<void(const atfw::SSMsgHead &head, TResponseMessage &msg)> callback) {
  // 回包合并
  TResponseMessage *shared_empty_message = nullptr;
  for (const auto &waiter : received) {
    if (nullptr == waiter.second) {
      continue;
    }

    if (!waiter.second->has_head()) {
      continue;
    }

    if (waiter.second->head().rpc_response().type_url() != TResponseMessage::descriptor()->full_name()) {
      FCTXLOGERROR(ctx, "{} expect response message {}, but got {}", rpc_name,
                   TResponseMessage::descriptor()->full_name(), waiter.second->head().rpc_response().type_url());
      continue;
    }

    if (waiter.second->body_bin().empty()) {
      if (shared_empty_message == nullptr) {
        shared_empty_message = ctx.create<TResponseMessage>();
        if (nullptr == shared_empty_message) {
          FCTXLOGERROR(ctx, "{} malloc {} failed", rpc_name, waiter.second->head().rpc_response().type_url());
          continue;
        }
      }

      callback(waiter.second->head(), *shared_empty_message);
      continue;
    }

    TResponseMessage *rsp_body = ctx.create<TResponseMessage>();
    if (nullptr == rsp_body) {
      FCTXLOGERROR(ctx, "{} malloc {} failed", rpc_name, waiter.second->head().rpc_response().type_url());
      continue;
    }

    if (false == rsp_body->ParseFromString(waiter.second->body_bin())) {
      FCTXLOGERROR(ctx, "{} parse message {} failed, msg: {}", rpc_name, TResponseMessage::descriptor()->full_name(),
                   rsp_body->InitializationErrorString());
      continue;
    }

    callback(waiter.second->head(), *rsp_body);
  }
}

/**
 * @brief foreach received multiple messages
 *
 * @param ctx context
 * @param received received messages
 * @param rpc_name name of the RPC call
 * @param callback callback to process each message
 * @return future of 0 or error code
 */

template <class TResponseMessage, class = atfw::util::nostd::enable_if_t<
                                      std::is_base_of<google::protobuf::MessageLite, TResponseMessage>::value>>
ATFW_UTIL_SYMBOL_VISIBLE void foreach_received_message(
    context &ctx, const std::unordered_map<uint64_t, atframework::SSMsg *> &received, gsl::string_view rpc_name,
    atfw::util::nostd::function_ref<void(const atfw::SSMsgHead &head, const TResponseMessage &msg)> callback) {
  // 类型转换
  foreach_received_message<TResponseMessage>(
      ctx, received, rpc_name, [callback](const atfw::SSMsgHead &head, TResponseMessage &msg) { callback(head, msg); });
}

/**
 * @brief Custom wait for a message or resume
 *
 * @param type_address type object address, user should keep it unique for each message type
 * @param options await options
 * @param receive_callback callback to read data from dispatcher_resume_data_type
 * @param receive_callback_private_data private data of receive_callback, should be alive before resumed
 * @return future of 0 or error code
 */
SERVER_FRAME_API result_code_type custom_wait(context &ctx, const void *type_address,
                                              const dispatcher_await_options &options,
                                              dispatcher_receive_resume_data_callback receive_callback = nullptr,
                                              void *receive_callback_private_data = nullptr);

template <class TPTR>
ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(
    TPTR &&input) {  // NOLINT(cppcoreguidelines-missing-std-forward)
  return reinterpret_cast<void *>(input);
}

ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(const void *input) { return input; }

ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(void *input) { return input; }

/**
 * @brief Custom wait for a message or resume
 *
 * @param type_address type object address, user should keep it unique for each message type
 * @param options await options
 * @param real_callback callback to read data from dispatcher_resume_data_type
 * @param real_private_data private data of receive_callback, should be alive before resumed
 * @return future of 0 or error code
 */
template <class TPRIVATE_DATA, class TCALLBACK, class TPTR,
          class = typename std::enable_if<std::is_pointer<typename std::remove_reference<TPTR>::type>::value>::type>
ATFW_UTIL_SYMBOL_VISIBLE result_code_type
custom_wait(context &ctx, TPTR &&type_address, const dispatcher_await_options &options, TCALLBACK &&real_callback,
            TPRIVATE_DATA &&real_private_data  // NOLINT(cppcoreguidelines-missing-std-forward)
) {
  auto callback_data = std::make_pair(std::forward<TCALLBACK>(real_callback), &real_private_data);
  using callback_date_type = decltype(callback_data);

  dispatcher_receive_resume_data_callback receive_callback = [](const dispatcher_resume_data_type *resume_data,
                                                                void *stack_data) {
    callback_date_type *restore_callback_data = reinterpret_cast<callback_date_type *>(stack_data);
    if (nullptr != restore_callback_data && restore_callback_data->first && nullptr != restore_callback_data->second) {
      (restore_callback_data->first)(resume_data, std::forward<TPRIVATE_DATA>(*restore_callback_data->second));
    }
  };

  RPC_RETURN_CODE(
      RPC_AWAIT_CODE_RESULT(custom_wait(ctx, custom_wait_convert_ptr(std::forward<TPTR>(type_address)), options,
                                        receive_callback, reinterpret_cast<void *>(&callback_data))));
}

/**
 * @brief Custom resume a waiter
 *
 * @param task task to resume
 * @param resume_data resume data
 * @return 0 or error code
 */
SERVER_FRAME_API int32_t custom_resume(const task_type_trait::task_type &task,
                                       dispatcher_resume_data_type &resume_data);

/**
 * @brief Custom resume a waiter
 *
 * @param task_id id of task to resume
 * @param resume_data resume data
 * @return 0 or error code
 */
SERVER_FRAME_API int32_t custom_resume(task_type_trait::id_type task_id, dispatcher_resume_data_type &resume_data);

SERVER_FRAME_API rpc::telemetry::tracer::span_ptr_type setup_rpc_tracer(
    rpc::context &__child_ctx, rpc::telemetry::tracer &__tracer, atframework::SSMsgHead &head,
    atfw::util::nostd::string_view rpc_full_name, rpc::telemetry::trace_attributes_type attributes);

SERVER_FRAME_API int setup_rpc_stream_header(atframework::RpcStreamMeta &stream_meta,
                                             atfw::util::nostd::string_view service_name,
                                             atfw::util::nostd::string_view rpc_full_name,
                                             atfw::util::nostd::string_view type_full_name);

}  // namespace rpc
