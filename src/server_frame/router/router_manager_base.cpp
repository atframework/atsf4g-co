// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#include "router/router_manager_base.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <std/explicit_declare.h>

#include <config/logic_config.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <log/log_wrapper.h>

#include <utility>

#include "router/router_manager_set.h"
#include "router/router_object_base.h"

SERVER_FRAME_API router_manager_base::router_manager_base(uint32_t type_id)
    : stat_size_(0), type_id_(type_id), is_closing_(false) {
  router_manager_set::me()->register_manager(this);
}

SERVER_FRAME_API router_manager_base::~router_manager_base() { router_manager_set::me()->unregister_manager(this); }

SERVER_FRAME_API rpc::result_code_type router_manager_base::mutable_cache(rpc::context &ctx,
                                                                          std::shared_ptr<router_object_base> &out,
                                                                          const key_t &key, void *priv_data) {
  router_object_base::io_task_guard io_guard;
  auto ret = RPC_AWAIT_CODE_RESULT(mutable_cache(ctx, out, key, priv_data, io_guard));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_manager_base::mutable_object(rpc::context &ctx,
                                                                           std::shared_ptr<router_object_base> &out,
                                                                           const key_t &key, void *priv_data) {
  router_object_base::io_task_guard io_guard;
  auto ret = RPC_AWAIT_CODE_RESULT(mutable_object(ctx, out, key, priv_data, io_guard));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_manager_base::remove_cache(rpc::context &ctx, const key_t &key,
                                                                         std::shared_ptr<router_object_base> cache,
                                                                         void *priv_data) {
  router_object_base::io_task_guard io_guard;
  auto ret = RPC_AWAIT_CODE_RESULT(remove_cache(ctx, key, cache, priv_data, io_guard));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_manager_base::remove_object(rpc::context &ctx, const key_t &key,
                                                                          std::shared_ptr<router_object_base> cache,
                                                                          void *priv_data) {
  router_object_base::io_task_guard io_guard;
  auto ret = RPC_AWAIT_CODE_RESULT(remove_object(ctx, key, cache, priv_data, io_guard));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API bool router_manager_base::is_auto_mutable_object() const { return false; }

SERVER_FRAME_API bool router_manager_base::is_auto_mutable_cache() const { return true; }

SERVER_FRAME_API uint64_t router_manager_base::get_default_router_server_id(const key_t &) const { return 0; }

SERVER_FRAME_API rpc::result_code_type router_manager_base::send_msg(rpc::context &ctx, router_object_base &obj,
                                                                     atframework::SSMsg &&msg, uint64_t &sequence) {
  // 如果正在转移过程中，追加到pending列表
  if (obj.check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
    obj.get_transfer_pending_list().push_back(atframework::SSMsg());
    obj.get_transfer_pending_list().back().Swap(&msg);
  }

  return send_msg_raw(ctx, obj, std::move(msg), sequence);
}

SERVER_FRAME_API rpc::result_code_type router_manager_base::send_msg(rpc::context &ctx, const key_t &key,
                                                                     atframework::SSMsg &&msg, uint64_t &sequence) {
  rpc::result_code_type::value_type res = 0;
  std::shared_ptr<router_object_base> obj;
  router_object_base::io_task_guard io_guard;

  res = RPC_AWAIT_CODE_RESULT(mutable_cache(ctx, obj, key, nullptr, io_guard));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  if (!obj) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_msg(ctx, *obj, std::move(msg), sequence)));
}

SERVER_FRAME_API rpc::result_code_type router_manager_base::send_msg_raw(rpc::context &ctx, router_object_base &obj,
                                                                         atframework::SSMsg &&msg, uint64_t &sequence) {
  // 这里必须直接发送

  atframework::SSRouterHead *router_head = msg.mutable_head()->mutable_router();
  // 源是自己
  router_head->set_router_source_node_id(logic_config::me()->get_local_server_id());
  auto local_server_name = logic_config::me()->get_local_server_name();
  router_head->set_router_source_node_name(local_server_name.data(), local_server_name.size());
  router_head->set_router_version(obj.get_router_version());
  router_head->set_object_type_id(obj.get_key().type_id);
  router_head->set_object_inst_id(obj.get_key().object_id);
  router_head->set_object_zone_id(obj.get_key().zone_id);
  //

  int retry_times = 2;

#if defined(_MSC_VER)
  // Dsiable C4701, it's a flaw of MSVC for uninitialized variable detection
  int ret = 0;
#else
  int ret;
#endif
  while (retry_times-- > 0) {
    // 如果路由节点为0，可能是缓存过期，尝试拉取一次
    if (0 == obj.get_router_server_id()) {
      router_object_base::io_task_guard io_guard;
      RPC_AWAIT_IGNORE_RESULT(obj.internal_pull_cache(ctx, nullptr, io_guard));
    }

    // 如果允许自动路由拉取,则发到默认server上
    uint64_t target_server_id = obj.get_router_server_id();
    if (0 == target_server_id && is_auto_mutable_object()) {
      target_server_id = get_default_router_server_id(obj.get_key());
      if (0 != target_server_id) {
        obj.set_router_server_id(target_server_id, obj.get_router_version());
      }
    }

    if (0 == target_server_id) {
      FWLOGWARNING("router object (type={}) {}:{}:{} has no valid router server", get_type_id(), obj.get_key().type_id,
                   obj.get_key().zone_id, obj.get_key().object_id);
      ret = PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER;
      break;
    }

    ret = ss_msg_dispatcher::me()->send_to_proc(target_server_id, msg);
    sequence = msg.head().sequence();
    if (ret < 0) {
      if (!ss_msg_dispatcher::me()->is_target_server_available(target_server_id)) {
        obj.set_router_server_id(0, obj.get_router_version());
      }
    } else {
      retry_times = 0;
      break;
    }
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API void router_manager_base::on_stop() { is_closing_ = false; }

SERVER_FRAME_API rpc::result_code_type router_manager_base::pull_online_server(rpc::context &, const key_t &,
                                                                               uint64_t &router_svr_id,
                                                                               uint64_t &router_svr_ver) {
  router_svr_id = 0;
  router_svr_ver = 0;
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API std::shared_ptr<router_manager_metrics_data> router_manager_base::mutable_metrics_data() {
  return router_manager_set::me()->mutable_metrics_data(name());
}
