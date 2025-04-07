// Copyright 2021 atframework
// Created by owent on 2016/10/4.
//

#pragma once

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

#include <stdint.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
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

namespace atframework {
class SSMsg;
}

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
SERVER_FRAME_API result_code_type wait(context &ctx, PROJECT_NAMESPACE_ID::table_all_message &msg,
                                       const dispatcher_await_options &options);

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
ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(TPTR &&input) {
  return reinterpret_cast<void *>(input);
}

ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(const void *input) { return input; }

ATFW_UTIL_FORCEINLINE const void *custom_wait_convert_ptr(void *input) { return const_cast<void *>(input); }

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
ATFW_UTIL_SYMBOL_VISIBLE result_code_type custom_wait(context &ctx, TPTR &&type_address,
                                                      const dispatcher_await_options &options,
                                                      TCALLBACK &&real_callback, TPRIVATE_DATA &&real_private_data) {
  auto callback_data = std::make_pair(real_callback, &real_private_data);
  using callback_date_type = decltype(callback_data);

  dispatcher_receive_resume_data_callback receive_callback = [](const dispatcher_resume_data_type *resume_data,
                                                                void *stack_data) {
    callback_date_type *restore_callback_data = reinterpret_cast<callback_date_type *>(stack_data);
    if (nullptr != restore_callback_data && restore_callback_data->first && nullptr != restore_callback_data->second) {
      (restore_callback_data->first)(resume_data, std::forward<TPRIVATE_DATA>(*restore_callback_data->second));
    }
  };

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(custom_wait(ctx, custom_wait_convert_ptr(type_address), options,
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

}  // namespace rpc
