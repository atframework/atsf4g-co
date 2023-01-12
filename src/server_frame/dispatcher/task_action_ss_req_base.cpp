// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_ss_req_base.h"

#include <std/explicit_declare.h>

#include <log/log_wrapper.h>
#include <logic/player_manager.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <config/logic_config.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>

#include <rpc/db/uuid.h>
#include <rpc/router/routerservice.h>

task_action_ss_req_base::task_action_ss_req_base(dispatcher_start_data_type &&start_param) {
  // 必须先设置共享的arena
  if (nullptr != start_param.context) {
    get_shared_context().try_reuse_protobuf_arena(start_param.context->mutable_protobuf_arena());
  }

  msg_type *ss_msg = ss_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
  if (nullptr != ss_msg) {
    get_request().Swap(ss_msg);

    set_user_key(get_request().head().player_user_id(), get_request().head().player_zone_id());
  }

  // 最后设置 caller
  if (nullptr != start_param.context) {
    set_caller_context(*start_param.context);
  }
}

task_action_ss_req_base::~task_action_ss_req_base() {}

task_action_ss_req_base::result_type task_action_ss_req_base::hook_run() {
  // 路由对象系统支持
  router_manager_base *mgr = nullptr;
  std::shared_ptr<router_object_base> obj;
  if (get_request().head().has_router()) {
    std::pair<bool, int> result;
    RPC_AWAIT_IGNORE_RESULT(filter_router_msg(mgr, obj, result));
    if (false == result.first) {
      TASK_ACTION_RETURN_CODE(result.second);
    }

    auto trace_span = get_shared_context().get_trace_span();
    if (trace_span) {
      trace_span->SetAttribute("router_object.type_id", get_request().head().router().object_type_id());
      trace_span->SetAttribute("router_object.zone_id", get_request().head().router().object_zone_id());
      trace_span->SetAttribute("router_object.instance_id", get_request().head().router().object_inst_id());
    }
  }

  // 自动设置快队列保存
  result_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::hook_run());
  if (nullptr != get_dispatcher_start_data().options && get_dispatcher_start_data().options->mark_fast_save()) {
    if (mgr && obj) {
      router_manager_set::me()->mark_fast_save(mgr, obj);
    }
  }

  TASK_ACTION_RETURN_CODE(ret);
}

uint64_t task_action_ss_req_base::get_request_bus_id() const {
  msg_cref_type msg = get_request();
  return msg.head().bus_id();
}

task_action_ss_req_base::msg_ref_type task_action_ss_req_base::add_rsp_msg(uint64_t dst_pd) {
  msg_type *msg = get_shared_context().create<msg_type>();
  if (nullptr == msg) {
    static msg_type empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  response_messages_.push_back(msg);

  msg->mutable_head()->set_error_code(get_response_code());
  dst_pd = 0 == dst_pd ? get_request_bus_id() : dst_pd;

  init_msg(*msg, dst_pd, get_request());
  return *msg;
}

int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd) {
  msg.mutable_head()->set_bus_id(dst_pd);
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());

  return 0;
}

int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd, msg_cref_type req_msg) {
  protobuf_copy_message(*msg.mutable_head(), req_msg.head());
  init_msg(msg, dst_pd);

  // set task information
  if (0 != req_msg.head().src_task_id()) {
    msg.mutable_head()->set_dst_task_id(req_msg.head().src_task_id());
  } else {
    msg.mutable_head()->set_dst_task_id(0);
  }

  if (0 != req_msg.head().dst_task_id()) {
    msg.mutable_head()->set_src_task_id(req_msg.head().dst_task_id());
  } else {
    msg.mutable_head()->set_src_task_id(0);
  }

  msg.mutable_head()->set_sequence(req_msg.head().sequence());
  if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM == req_msg.head().op_type()) {
    msg.mutable_head()->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_STREAM);
  } else {
    msg.mutable_head()->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE);
  }

  return 0;
}

std::shared_ptr<dispatcher_implement> task_action_ss_req_base::get_dispatcher() const {
  return std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
}

const char *task_action_ss_req_base::get_type_name() const { return "inserver"; }

rpc::context::parent_mode task_action_ss_req_base::get_caller_mode() const noexcept {
  auto &req_msg = get_request();
  if (req_msg.has_head() && req_msg.head().has_rpc_request() && 0 != req_msg.head().src_task_id()) {
    return rpc::context::parent_mode::kParent;
  }

  return rpc::context::parent_mode::kLink;
}

void task_action_ss_req_base::send_response() {
  if (response_messages_.empty()) {
    return;
  }

  for (std::list<msg_type *>::iterator iter = response_messages_.begin(); iter != response_messages_.end(); ++iter) {
    if (0 == (*iter)->head().bus_id()) {
      FWLOGERROR("task {} [{}] send message to unknown server", name(), get_task_id());
      continue;
    }
    (*iter)->mutable_head()->set_error_code(get_response_code());

    // send message using ss dispatcher
    int32_t res = ss_msg_dispatcher::me()->send_to_proc((*iter)->head().bus_id(), **iter);
    if (res) {
      FWLOGERROR("task {} [{}] send message to server {:#x} failed, res: {}({})", name(), get_task_id(),
                 (*iter)->head().bus_id(), res, protobuf_mini_dumper_get_error_msg(res));
    }
  }

  response_messages_.clear();
}

namespace detail {
struct filter_router_message_result_type {
  bool is_on_current_server;
  bool enable_retry;

  inline filter_router_message_result_type() : is_on_current_server(false), enable_retry(false) {}
  inline filter_router_message_result_type(bool cur, bool retry) : is_on_current_server(cur), enable_retry(retry) {}
};

static rpc::result_code_type try_fetch_router_cache(rpc::context &ctx, router_manager_base &mgr,
                                                    router_manager_base::key_t key,
                                                    std::shared_ptr<router_object_base> &obj) {
  rpc::result_code_type::value_type res = 0;
  obj = mgr.get_base_cache(key);

  // 如果不存在那么实体一定不在这台机器上，但是可能在其他机器上，需要拉取一次确认
  if (!obj) {
    if (!mgr.is_auto_mutable_cache()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
    }
    res = RPC_AWAIT_CODE_RESULT(mgr.mutable_cache(ctx, obj, key, nullptr));
    if (res < 0 || !obj) {
      FWLOGERROR("router object {}:{}:{} fetch cache failed, res: {}({})", key.type_id, key.zone_id, key.object_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }
  }

  RPC_RETURN_CODE(res);
}

static rpc::result_code_type auto_mutable_router_object(rpc::context &ctx, uint64_t self_bus_id,
                                                        router_manager_base &mgr, router_manager_base::key_t key,
                                                        std::shared_ptr<router_object_base> &obj,
                                                        filter_router_message_result_type &result) {
  // 如果开启了自动拉取object，尝试拉取object
  if (!mgr.is_auto_mutable_object()) {
    FWLOGINFO("router object key={}:{}:{} not found and not auto mutable object", key.type_id, key.zone_id,
              key.object_id);
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
  }

  auto res = RPC_AWAIT_CODE_RESULT(mgr.mutable_object(ctx, obj, key, nullptr));
  if (res < 0) {
    FWLOGERROR("router object {}:{}:{} repair object failed, res: {}({})", key.type_id, key.zone_id, key.object_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    // 失败则删除缓存重试
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));

    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(res);
  }

  // Check log
  if (self_bus_id != obj->get_router_server_id()) {
    FWLOGERROR("router object {}:{}:{} auto mutable object failed, expect server id 0x{:x}, real server id 0x{:x}",
               key.type_id, key.zone_id, key.object_id, self_bus_id, obj->get_router_server_id());
    if (0 == obj->get_router_server_id()) {
      result = filter_router_message_result_type(false, true);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
    } else {
      result = filter_router_message_result_type(false, false);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_IN_OTHER_SERVER);
    }
  }

  result = filter_router_message_result_type(true, true);
  RPC_RETURN_CODE(res);
}

static rpc::result_code_type check_local_router_object(rpc::context &ctx, uint64_t self_bus_id,
                                                       router_manager_base &mgr, router_manager_base::key_t key,
                                                       std::shared_ptr<router_object_base> &obj,
                                                       filter_router_message_result_type &result) {
  // 路由对象命中当前节点，要开始执行任务逻辑
  if (obj->is_writable()) {
    result = filter_router_message_result_type(true, true);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 这里可能是服务器崩溃过，导致数据库记录对象在本机上，但实际上没有。所以这里升级一次做个数据修复
  auto res = RPC_AWAIT_CODE_RESULT(mgr.mutable_object(ctx, obj, key, nullptr));
  if (res < 0) {
    FWLOGERROR("router object {}:{}:{} repair object failed, res: {}({})", key.type_id, key.zone_id, key.object_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    // 失败则删除缓存重试
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));

    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(res);
  }

  // Check log
  if (self_bus_id != obj->get_router_server_id()) {
    FWLOGERROR("router object {}:{}:{} repair object failed, expect server id 0x{:x}, real server id 0x{:x}",
               key.type_id, key.zone_id, key.object_id, self_bus_id, obj->get_router_server_id());
  }

  // 恢复成功，直接开始执行任务逻辑
  result = filter_router_message_result_type(true, true);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

static rpc::result_code_type try_filter_router_msg(rpc::context &ctx, EXPLICIT_UNUSED_ATTR int retry_times,
                                                   uint64_t request_bus_id, atframework::SSMsg &request_msg,
                                                   router_manager_base &mgr, router_manager_base::key_t key,
                                                   std::shared_ptr<router_object_base> &obj,
                                                   filter_router_message_result_type &result) {
  obj.reset();

  const atframework::SSRouterHead &router = request_msg.head().router();
  int32_t res = RPC_AWAIT_CODE_RESULT(try_fetch_router_cache(ctx, mgr, key, obj));
  if (res == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND) {
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  }

  if (!obj) {
    if (res >= 0) {
      res = PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND;
    }
    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  }

  // 如果正在迁移，追加到pending队列，本task直接退出
  if (obj->check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
    obj->get_transfer_pending_list().push_back(atframework::SSMsg());
    obj->get_transfer_pending_list().back().Swap(&request_msg);

    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 如果本地版本号低于来源服务器，刷新一次路由表。正常情况下这里不可能走到，如果走到了。需要删除缓存再来一次
  if (obj->get_router_version() < router.router_version()) {
    FWLOGERROR("router object {}:{}:{} has invalid router version, refresh cache", key.type_id, key.zone_id,
               key.object_id);
    RPC_AWAIT_IGNORE_RESULT(mgr.remove_cache(ctx, key, obj, nullptr));
    result = filter_router_message_result_type(false, true);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  uint64_t self_bus_id = logic_config::me()->get_local_server_id();
  // 可能本地缓存的路由信息过期，路由节点返回0的话说明最后一次登记时对象离线了，这时候只能尝试去数据库获取一次新的信息
  if (0 == obj->get_router_server_id() && !mgr.is_auto_mutable_object() && !obj->is_writable()) {
    uint64_t renew_router_server_id = 0;
    uint64_t renew_router_version = 0;
    res = RPC_AWAIT_CODE_RESULT(mgr.pull_online_server(ctx, key, renew_router_server_id, renew_router_version));
    if (res < 0) {
      result = filter_router_message_result_type(false, true);
      RPC_RETURN_CODE(res);
    }

    if (0 == renew_router_server_id) {
      result = filter_router_message_result_type(false, false);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
    }

    if (!obj->is_writable() && 0 == obj->get_router_server_id() && renew_router_version > obj->get_router_version()) {
      obj->set_router_server_id(renew_router_server_id, renew_router_version);
    }
  }

  if (0 == obj->get_router_server_id()) {
    filter_router_message_result_type auto_res;
    res = RPC_AWAIT_CODE_RESULT(auto_mutable_router_object(ctx, self_bus_id, mgr, key, obj, auto_res));
    if (res < 0) {
      result = auto_res;
      RPC_RETURN_CODE(res);
    }
  }

  // 如果和本地的路由缓存匹配则break直接开始消息处理
  if (obj && self_bus_id == obj->get_router_server_id()) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(check_local_router_object(ctx, self_bus_id, mgr, key, obj, result)));
  }

  // 路由消息转发
  if (obj && 0 != obj->get_router_server_id()) {
    uint64_t rpc_sequence;
    if (request_msg.head().router().router_transfer_ttl() < logic_config::me()->get_cfg_router().transfer_max_ttl()) {
      request_msg.mutable_head()->mutable_router()->set_router_transfer_ttl(
          request_msg.head().router().router_transfer_ttl() + 1);
      res = RPC_AWAIT_CODE_RESULT(mgr.send_msg(ctx, *obj, std::move(request_msg), rpc_sequence));
    } else {
      res = PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND;
    }

    // 如果路由转发成功，需要禁用掉回包和通知事件，也不需要走逻辑处理了
    if (res < 0) {
      FWLOGERROR("try to transfer router object {}:{}:{} to 0x{:x} failed, res: {}({})", key.type_id, key.zone_id,
                 key.object_id, obj->get_router_server_id(), res, protobuf_mini_dumper_get_error_msg(res));
    }

    result = filter_router_message_result_type(false, false);
    RPC_RETURN_CODE(res);
  }

  // 这个分支理论上也不会跑到，前面已经枚举了所有流程分支了
  FWLOGERROR("miss router object {}:{}:{} prediction code", key.type_id, key.zone_id, key.object_id);
  result = filter_router_message_result_type(false, true);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER);
}
}  // namespace detail

rpc::result_code_type task_action_ss_req_base::filter_router_msg(router_manager_base *&mgr,
                                                                 std::shared_ptr<router_object_base> &obj,
                                                                 std::pair<bool, int> &filter_result) {
  // request 可能会被move走，所以这里copy一份
  atframework::SSRouterHead router;
  protobuf_copy_message(router, get_request().head().router());

  // find router manager in router set
  mgr = router_manager_set::me()->get_manager(router.object_type_id());
  if (nullptr == mgr) {
    FWLOGERROR("router manager {} not found", router.object_type_id());
    filter_result = std::make_pair(false, PROJECT_NAMESPACE_ID::err::EN_ROUTER_TYPE_INVALID);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TYPE_INVALID);
  }

  router_manager_base::key_t key(router.object_type_id(), router.object_zone_id(), router.object_inst_id());
  router_object_base::trace_router(get_shared_context(), key);

  int retry_times = 0;
  rpc::result_code_type::value_type last_result = 0;

  // 最多重试3次，故障恢复过程中可能发生抢占，这时候正常情况下第二次就应该会成功
  while ((++retry_times) <= 3) {
    detail::filter_router_message_result_type internal_filter_result;
    last_result =
        RPC_AWAIT_CODE_RESULT(detail::try_filter_router_msg(get_shared_context(), retry_times, get_request_bus_id(),
                                                            get_request(), *mgr, key, obj, internal_filter_result));
    if (internal_filter_result.is_on_current_server) {
      filter_result = std::make_pair(true, last_result);
      RPC_RETURN_CODE(last_result);
    }

    // 如果路由转发成功或者路由转移期间待处理的消息队列添加成功
    // 需要禁用掉回包和通知事件，也不需要走逻辑处理了
    if (last_result >= 0) {
      disable_response_message();
      disable_finish_event();
      break;
    }

    // 某些情况下不需要重试
    if (!internal_filter_result.enable_retry) {
      break;
    }
  }

  // 如果本地路由版本号大于来源，通知来源更新路由表
  if (last_result >= 0 && obj && obj->get_router_version() > router.router_version()) {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRouterUpdateSync> sync_msg{get_shared_context()};
    atframework::SSRouterHead *router_head = sync_msg->mutable_object();
    if (nullptr != router_head) {
      router_head->set_router_src_bus_id(obj->get_router_server_id());
      router_head->set_router_version(obj->get_router_version());
      router_head->set_object_type_id(key.type_id);
      router_head->set_object_inst_id(key.object_id);
      router_head->set_object_zone_id(key.zone_id);
    }

    // 只通知直接来源
    RPC_AWAIT_IGNORE_RESULT(rpc::router::router_update_sync(get_shared_context(), get_request_bus_id(), *sync_msg));
  }

  // 失败则要回发转发失败
  set_response_code(last_result);

  // 如果忽略路由节点不在线,直接返回0即可
  if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER == last_result && is_router_offline_ignored()) {
    last_result = 0;
  }

  if (obj && last_result < 0) {
    obj->send_transfer_msg_failed(COPP_MACRO_STD_MOVE(get_request()));
  }
  filter_result = std::make_pair(false, last_result);
  RPC_RETURN_CODE(last_result);
}

bool task_action_ss_req_base::is_router_offline_ignored() const { return false; }
