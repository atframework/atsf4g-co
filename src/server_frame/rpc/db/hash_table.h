// Copyright 2021 atframework
// Created by owent on 2022-03-17.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <cstddef>
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
namespace key_value {
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(
    rpc::context &ctx, uint32_t channel, gsl::string_view key,
    atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output, db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
partly_get(rpc::context &ctx, uint32_t channel, gsl::string_view key, gsl::string_view *partly_get_fields,
           int32_t partly_get_field_count, atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t> output,
           db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
batch_get_all(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key,
              std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
              db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
batch_partly_get(rpc::context &ctx, uint32_t channel, gsl::span<std::string> key, gsl::string_view *partly_get_fields,
                 int32_t partly_get_field_count,
                 std::vector<atfw::util::memory::strong_rc_ptr<db_key_value_message_result_t>> &output,
                 db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type set(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                                         shared_abstract_message<google::protobuf::Message> &&store,
                                                         uint64_t *version);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
inc_field(rpc::context &ctx, uint32_t channel, gsl::string_view key, gsl::string_view inc_field,
          shared_abstract_message<google::protobuf::Message> &message, db_msg_dispatcher::unpack_fn_t unpack_fn);
}  // namespace key_value

namespace key_list {
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_all(rpc::context &ctx, uint32_t channel, gsl::string_view key,
                                                             std::vector<db_key_list_message_result_t> &output,
                                                             db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type get_by_indexs(rpc::context &ctx, uint32_t channel,
                                                                   gsl::string_view key, gsl::span<uint64_t> list_index,
                                                                   bool enable_cas,
                                                                   std::vector<db_key_list_message_result_t> &output,
                                                                   db_msg_dispatcher::unpack_fn_t unpack_fn);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type
set_by_index(rpc::context &ctx, uint32_t channel, gsl::string_view key, uint64_t list_index,
             shared_abstract_message<google::protobuf::Message> &&store, uint64_t *version);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel,
                                                                     gsl::string_view key,
                                                                     gsl::span<uint64_t> list_index, bool enable_cas);
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_by_index(rpc::context &ctx, uint32_t channel,
                                                                     gsl::string_view key,
                                                                     gsl::span<const uint64_t> list_index, bool enable_cas);
}  // namespace key_list
EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_type remove_all(rpc::context &ctx, uint32_t channel,
                                                                gsl::string_view key);

}  // namespace hash_table
}  // namespace db
}  // namespace rpc
