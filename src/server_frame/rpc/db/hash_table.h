// Copyright 2021 atframework
// Created by owent on 2022-03-17.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <dispatcher/db_msg_dispatcher.h>
#include <rpc/rpc_shared_message.h>
#include "rpc/db/db_utils.h"

struct db_message_t;

namespace rpc {
class context;

namespace db {
namespace hash_table {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// Synchronous unit-test seam. When a hook is installed it is evaluated by every primitive operation
// after all local validation and before any Redis request is constructed. When the hook reports the
// operation handled, the public entry returns the supplied result code immediately and never touches
// the DB dispatcher. Batch operations are composed from the primitives and therefore covered
// automatically. Only available in builds with PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS.
struct ATFW_UTIL_SYMBOL_VISIBLE unit_test_request {
  enum class op_type : int32_t {
    kv_get_all = 0,
    kv_partly_get,
    kv_set,
    // Insert-only write (kInsertHashTable); reported separately from kv_set because the two scripts
    // disagree on an existing record: kv_set with version 0 forces the overwrite, kv_insert fails.
    kv_insert,
    kv_inc_field,
    kl_get_all,
    kl_get_by_indexs,
    kl_update_by_index,
    kl_add_index,
    kl_remove_by_index,
    remove_all,
    set_ttl,
    remove_ttl,
  };

  op_type op = op_type::kv_get_all;
  rpc::context *ctx = nullptr;
  uint32_t channel = 0;
  gsl::string_view key;

  // Optional inputs (empty/nullptr when unused by the op).
  const gsl::string_view *partly_get_fields = nullptr;
  int32_t partly_get_field_count = 0;
  const google::protobuf::Message *store = nullptr;
  gsl::string_view inc_field;
  gsl::span<const uint64_t> list_index;
  uint32_t max_list_length = 0;
  uint64_t ttl_second = 0;

  // Outputs (nullptr when unused by the op). The hook fills them exactly like the real wait/unpack
  // path would: kv_output->message/version, kl_output entries, *version for CAS results, and the
  // mutated inc_message field for kv_inc_field.
  db_key_value_message_result_t *kv_output = nullptr;
  std::vector<db_key_list_message_result_t> *kl_output = nullptr;
  uint64_t *version = nullptr;
  google::protobuf::Message *inc_message = nullptr;
};

// Returns true when the operation is handled; result_code then carries the function-level result.
using unit_test_hook_t = std::function<bool(const unit_test_request &, int32_t &result_code)>;
SERVER_FRAME_API void set_hash_table_hook_for_unit_test(unit_test_hook_t hook);
SERVER_FRAME_API const unit_test_hook_t &get_hash_table_hook_for_unit_test() noexcept;
#endif

namespace key_value {
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(
    rpc::context &ctx, uint32_t channel, gsl::string_view key,
    atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output, db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
partly_get(rpc::context &ctx, uint32_t channel, gsl::string_view key, gsl::string_view *partly_get_fields,
           int32_t partly_get_field_count, atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output,
           db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
batch_get_all(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key,
              std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
              db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
batch_partly_get(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key, gsl::string_view *partly_get_fields,
                 int32_t partly_get_field_count,
                 std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
                 db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
set(rpc::context &ctx, uint32_t channel, gsl::string_view key,
    shared_abstract_message<google::protobuf::Message> &&store, uint64_t *version);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
insert(rpc::context &ctx, uint32_t channel, gsl::string_view key,
       shared_abstract_message<google::protobuf::Message> &&store, uint64_t &version);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
inc_field(rpc::context &ctx, uint32_t channel, gsl::string_view key, gsl::string_view inc_field,
          shared_abstract_message<google::protobuf::Message> &message, db_msg_dispatcher::unpack_fn_t unpack_fn);
}  // namespace key_value

namespace key_list {
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx, uint32_t channel,
                                                                  gsl::string_view key,
                                                                  std::vector<db_key_list_message_result_t> &output,
                                                                  db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
get_by_indexs(rpc::context &ctx, uint32_t channel, gsl::string_view key, gsl::span<uint64_t> list_index,
              std::vector<db_key_list_message_result_t> &output, db_msg_dispatcher::unpack_fn_t unpack_fn);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
update_by_index(rpc::context &ctx, uint32_t channel, gsl::string_view key, uint64_t list_index,
                shared_abstract_message<google::protobuf::Message> &&store);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
add_index(rpc::context &ctx, uint32_t channel, gsl::string_view key, uint32_t max_list_length,
          shared_abstract_message<google::protobuf::Message> &&store);
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel,
                                                                          gsl::string_view key,
                                                                          gsl::span<uint64_t> list_index);
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel,
                                                                          gsl::string_view key,
                                                                          gsl::span<const uint64_t> list_index);
}  // namespace key_list
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_all(rpc::context &ctx, uint32_t channel,
                                                                     gsl::string_view key);
ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type set_ttl(rpc::context &ctx, uint32_t channel,
                                                                  gsl::string_view key, uint64_t ttl_second);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_ttl(rpc::context &ctx, uint32_t channel,
                                                                     gsl::string_view key);

}  // namespace hash_table
}  // namespace db
}  // namespace rpc
