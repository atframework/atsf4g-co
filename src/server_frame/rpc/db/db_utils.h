// Copyright atframework
// Created by owent on 2016/10/5.
//

#pragma once

#include <google/protobuf/message.h>

#include <libcopp/future/poller.h>

#include <std/explicit_declare.h>

#include <inttypes.h>
#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include "dispatcher/db_msg_dispatcher.h"
#include "rpc/rpc_common_types.h"

extern "C" struct redisReply;

#define RPC_DB_VERSION_NAME "CAS_VERSION"
#define RPC_DB_VERSION_LENGTH 11

namespace rpc {
namespace db {

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE

using result_type = result_code_type;

#else

/**
 * @brief 数据库接口统一返回结构
 * @note 这里不使用原始int类型是为了位以后扩展更高级的设计模式做预留。（比武无栈协程）
 */
class result_type {
 public:
  using value_type = int32_t;

 public:
  SERVER_FRAME_API result_type();

  template <class TINPUT>
  ATFW_UTIL_SYMBOL_VISIBLE result_type(rpc_result_guard<TINPUT>&& guard)
      : result_data_(guard.get()),
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
        ,
        awaited_(false)
#  endif
  {
  }

#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  SERVER_FRAME_API result_type(result_type&&);
  SERVER_FRAME_API result_type& operator=(result_type&&);

  ATFW_UTIL_FORCEINLINE void _internal_set_awaited() noexcept { awaited_ = true; }
#  endif
  SERVER_FRAME_API ~result_type();

  SERVER_FRAME_API explicit result_type(value_type code);
  SERVER_FRAME_API explicit operator value_type() const noexcept;

  SERVER_FRAME_API bool is_success() const noexcept;
  SERVER_FRAME_API bool is_error() const noexcept;

  ATFW_UTIL_FORCEINLINE bool is_ready() const noexcept { return result_data_.is_ready(); }

 private:
  copp::future::poller<value_type> result_data_;
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  bool awaited_;
#  endif
};
#endif

/**
 * allocate a buffer in specify buffer block and align address to type Ty
 * @note it's useful in allocate args when using redis to store data and using reflect to pack message.
 *       because object's address must be align to type size on x86 or ARM architecture, such as size_t, uint32_t,
 * uint64_t and etc.
 * @param buf_addr input available buffer block, output left available address
 * @param buf_len input available buffer length, output left available length
 * @return allocated buffer address, nullptr if failed
 */
template <typename Ty>
ATFW_UTIL_SYMBOL_VISIBLE void* align_alloc(void*& buf_addr, size_t& buf_len) {
  if (nullptr == buf_addr) {
    return nullptr;
  }

  uintptr_t align_sz = sizeof(Ty);
  uintptr_t in_addr = (uintptr_t)buf_addr;
  uintptr_t padding_offset = in_addr % align_sz;
  if (0 != padding_offset) {
    padding_offset = align_sz - padding_offset;
    in_addr += padding_offset;
  }

  // buffer not enough
  if (buf_len < padding_offset) {
    return nullptr;
  }

  buf_len -= padding_offset;
  buf_addr = (void*)(in_addr);
  return buf_addr;
}

class redis_args {
 public:
  redis_args(size_t argc);
  ~redis_args();

  char* alloc(size_t sz);
  void dealloc();
  bool empty() const;
  size_t size() const;

  /**
   * get start pointer of all arguments
   * @note although const char* const * is better,
   *       but hiredis use const char** as input argumeng type, so we also use it
   * @return
   */
  const char** get_args_values();
  const size_t* get_args_lengths() const;

  bool push(const char* str, size_t len = 0);
  bool push(const std::string& str);
  bool push(uint8_t);
  bool push(int8_t);
  bool push(uint16_t);
  bool push(int16_t);
  bool push(uint32_t);
  bool push(int32_t);
  bool push(uint64_t);
  bool push(int64_t);

 private:
  std::vector<const char*> segment_value_;
  std::vector<size_t> segment_length_;
  size_t used_;
  char* free_buffer_;
};

int unpack_message(::google::protobuf::Message& msg, const redisReply* reply, uint64_t& version, bool& record_existed);

int unpack_message_with_field(::google::protobuf::Message& msg, const redisReply* reply, std::string_view* fields,
                              int32_t length, uint64_t& version, bool& record_existed);

std::string get_list_value_field(uint64_t index);

int unpack_list_message(
    rpc::context* ctx, const redisReply* reply, std::vector<db_key_list_message_result_t>& results,
    std::function<
        atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>>(rpc::context*)>
        msg_factory);

int unpack_list_message_with_index(
    rpc::context* ctx, const redisReply* reply, std::vector<db_key_list_message_result_t>& results,
    std::function<
        atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>>(rpc::context*)>
        msg_factory);
/**
 * package message into redis args, each message field will take two segment in args
 * @param msg message
 * @param args where to store arguments
 * @param fds which fields will be packed
 * @param version version if need
 * @param debug_message debug message if need
 * @return 0 or error code
 */
int pack_message(const ::google::protobuf::Message& msg, redis_args& args,
                 std::vector<const ::google::protobuf::FieldDescriptor*> fds, uint64_t* version,
                 std::ostream* debug_message);
}  // namespace db
}  // namespace rpc

#define RPC_DB_RETURN_CODE(x) RPC_RETURN_TYPE(x)
