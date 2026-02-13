// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrClientService, please don't edit it

#include "lobbysvrclientservice.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <data/session.h>
#include <dispatcher/cs_msg_dispatcher.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
namespace {
template <class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT& input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}
template <class TBodyType>
inline static int __pack_body(TBodyType& body, std::string* output, atfw::util::nostd::string_view rpc_full_name,
                              atfw::util::nostd::string_view type_full_name) {
  if (false == body.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               body.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(body));
    return hello::err::EN_SUCCESS;
  }
}

inline static int __setup_rpc_stream_header(atframework::CSMsgHead& head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return hello::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("hello.LobbysvrClientService");
  stream_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());

  return hello::err::EN_SUCCESS;
}
}  // namespace

namespace lobbysvrclientservice {

// ============ hello.LobbysvrClientService.player_dirty_chg_sync ============
GAMECLIENT_SERVICE_API rpc::always_ready_code_type send_player_dirty_chg_sync(context& __ctx,
                                                                              hello::SCPlayerDirtyChgSync& __body,
                                                                              session& __session) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "hello.LobbysvrClientService.player_dirty_chg_sync", __session.get_key().node_id,
               __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(hello::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(*msg_ptr->mutable_head(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                                      __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(__body, msg_ptr->mutable_body_bin(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                    __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "hello.LobbysvrClientService.player_dirty_chg_sync", __session.get_key().node_id,
               __session.get_key().session_id, res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

GAMECLIENT_SERVICE_API rpc::always_ready_code_type send_player_dirty_chg_sync(context& __ctx,
                                                                              hello::SCPlayerDirtyChgSync& __body,
                                                                              session& __session,
                                                                              uint64_t server_sequence) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "hello.LobbysvrClientService.player_dirty_chg_sync", __session.get_key().node_id,
               __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(hello::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(*msg_ptr->mutable_head(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                                      __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(__body, msg_ptr->mutable_body_bin(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                    __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr, server_sequence);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "hello.LobbysvrClientService.player_dirty_chg_sync", __session.get_key().node_id,
               __session.get_key().session_id, res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

GAMECLIENT_SERVICE_API rpc::always_ready_code_type broadcast_player_dirty_chg_sync(context& __ctx,
                                                                                   hello::SCPlayerDirtyChgSync& __body,
                                                                                   uint64_t service_id) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message to broadcast failed",
               "hello.LobbysvrClientService.player_dirty_chg_sync");
    return {static_cast<rpc::always_ready_code_type::value_type>(hello::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(*msg_ptr->mutable_head(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                                      __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(__body, msg_ptr->mutable_body_bin(), "hello.LobbysvrClientService.player_dirty_chg_sync",
                    __to_string_view(hello::SCPlayerDirtyChgSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  res = session::broadcast_msg_to_client(service_id, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} broadcast message  failed, result: {}({})", "hello.LobbysvrClientService.player_dirty_chg_sync",
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

}  // namespace lobbysvrclientservice
}  // namespace rpc
