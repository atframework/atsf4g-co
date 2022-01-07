// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#include "router/router_manager_base.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <std/explicit_declare.h>

#include <config/logic_config.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <log/log_wrapper.h>

#include "router/router_manager_set.h"
#include "router/router_object_base.h"

router_manager_base::router_manager_base(uint32_t type_id) : stat_size_(0), type_id_(type_id), is_closing_(false) {
  router_manager_set::me()->register_manager(this);
}
router_manager_base::~router_manager_base() { router_manager_set::me()->unregister_manager(this); }

bool router_manager_base::is_auto_mutable_object() const { return false; }

bool router_manager_base::is_auto_mutable_cache() const { return true; }

uint64_t router_manager_base::get_default_router_server_id(const key_t &key) const { return 0; }

int router_manager_base::send_msg(router_object_base &obj, PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg &&msg,
                                  uint64_t &sequence) {
  // 如果正在转移过程中，追加到pending列表
  if (obj.check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
    obj.get_transfer_pending_list().push_back(PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg());
    obj.get_transfer_pending_list().back().Swap(&msg);
  }

  return send_msg_raw(obj, std::move(msg), sequence);
}

int router_manager_base::send_msg(const key_t &key, PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg &&msg,
                                  uint64_t &sequence) {
  int res = 0;
  std::shared_ptr<router_object_base> obj;
  res = mutable_cache(obj, key, nullptr);
  if (res < 0) {
    return res;
  }

  if (!obj) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND;
  }

  return send_msg(*obj, std::move(msg), sequence);
}

int router_manager_base::send_msg_raw(router_object_base &obj, PROJECT_SERVER_FRAME_NAMESPACE_ID::SSMsg &&msg,
                                      uint64_t &sequence) {
  // 这里必须直接发送

  PROJECT_SERVER_FRAME_NAMESPACE_ID::SSRouterHead *router_head = msg.mutable_head()->mutable_router();
  router_head->set_router_src_bus_id(logic_config::me()->get_local_server_id());  // 源BUS ID是自己
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
      obj.pull_cache_inner(nullptr);
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
      FWLOGERROR("router object (type={}) {}:{}:{} has no valid router server", get_type_id(), obj.get_key().type_id,
                 obj.get_key().zone_id, obj.get_key().object_id);
      ret = PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER;
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

  return ret;
}

void router_manager_base::on_stop() { is_closing_ = false; }

int router_manager_base::pull_online_server(const key_t &, uint64_t &router_svr_id, uint64_t &router_svr_ver) {
  router_svr_id = 0;
  router_svr_ver = 0;
  return 0;
}
