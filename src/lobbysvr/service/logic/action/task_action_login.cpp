// Copyright 2026 atframework

#include "logic/action/task_action_login.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atgateway/protocol/libatgw_protocol_api.h>

#include <data/session.h>
#include <data/user.h>
#include <logic/session_manager.h>
#include <logic/user_manager.h>

#include <config/logic_config.h>
#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>

#include <router/router_user_manager.h>

#include <dispatcher/task_manager.h>
#include <utility/protobuf_mini_dumper.h>

#include <memory>
#include <string>

#include "logic/action/task_action_user_async_jobs.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_context.h"

GAMECLIENT_RPC_API task_action_login::task_action_login(dispatcher_start_data_type&& param)
    : base_type(std::move(param)), is_new_user_(false) {}

GAMECLIENT_RPC_API task_action_login::~task_action_login() {}

GAMECLIENT_RPC_API const char* task_action_login::name() const { return "task_action_login"; }

GAMECLIENT_RPC_API task_action_login::result_type task_action_login::operator()() {
  const rpc_request_type& req_body = get_request_body();

  is_new_user_ = false;
  uint32_t zone_id = req_body.zone_id();
  if (zone_id == 0) {
    zone_id = logic_config::me()->get_local_zone_id();
  }
  set_user_key(req_body.user_id(), zone_id);

  rpc::result_code_type::value_type res = 0;

  // 先查找用户缓存，使用缓存。如果缓存正确则不需要拉取login表和user表
  user::ptr_t user_inst = user_manager::me()->find_as<user>(req_body.user_id(), zone_id);

  // 正在登出则要等登出结束重新获取
  if (user_inst && user_inst->is_writable()) {
    res = RPC_AWAIT_CODE_RESULT(await_logout_io_task(get_shared_context(), user_inst));
    if (res < 0) {
      if (res == PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT) {
        set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT);
      } else {
        set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
      }
      TASK_ACTION_RETURN_CODE(res);
    }

    user_inst = user_manager::me()->find_as<user>(req_body.user_id(), zone_id);
  }

  // 如果先前的login还在执行中，需要等待路由系统的io任务完成，否则前一个登入尚未设置router对象为writable
  // 后面的remove不会等待IO事件，而本次的login写表时会冲突。最终导致两个登入都失败
  if (user_inst && !user_inst->is_writable()) {
    res = RPC_AWAIT_CODE_RESULT(await_login_io_task(get_shared_context(), user_inst));
    if (res < 0) {
      set_response_code(res);
      TASK_ACTION_RETURN_CODE(res);
    }
  }

  if (user_inst && user_inst->has_initialization_task_id()) {
    res = RPC_AWAIT_CODE_RESULT(user_inst->await_initialization_task(get_shared_context()));
    if (res < 0) {
      TASK_ACTION_RETURN_CODE(res);
    }
  }

  if (user_inst && user_inst->get_login_lock().access_token_code() == req_body.access_token_code() &&
      atfw::util::time::time_utility::sys_now() <=
          protobuf_to_system_clock(user_inst->get_login_lock().access_token_expired()) &&
      user_inst->is_writable()) {
    RPC_AWAIT_IGNORE_RESULT(replace_session(user_inst));
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (user_manager::me()->has_create_user_lock(req_body.user_id(), zone_id)) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 如果有缓存要强制失效，因为可能其他地方登入了，这时候也不能复用缓存
  RPC_AWAIT_IGNORE_RESULT(user_manager::me()->remove(get_shared_context(), req_body.user_id(), zone_id, true));
  user_inst.reset();

  // 拉取或login_auth表，查询授权信息
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> login_auth_tb{get_shared_context()};
  uint64_t login_auth_cas_version = 0;

  res = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_auth::get_all(get_shared_context(), req_body.open_id(), *login_auth_tb, login_auth_cas_version));
  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res) {
      FCTXLOGERROR(get_shared_context(), "user {} try to get login auth table failed when login", req_body.user_id());
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    }
    RPC_RETURN_CODE(res);
  }
  if (login_auth_tb->user_id() != req_body.user_id()) {
    FCTXLOGWARNING(get_shared_context(), "user {} open_id {} login auth table user_id mismatch, maybe open_id reused",
                   req_body.user_id(), req_body.open_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 拉取或创建last login表，查询禁止登入和踢出之前的session
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> login_lock_tb{get_shared_context()};
  uint64_t login_lock_cas_version = 0;

  res = RPC_AWAIT_CODE_RESULT(kickoff_other_session(req_body.user_id(), login_lock_tb, login_lock_cas_version));
  if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    login_lock_tb->set_user_id(req_body.user_id());

    res = RPC_AWAIT_CODE_RESULT(
        rpc::db::login_lock::replace(get_shared_context(), login_lock_tb, login_lock_cas_version));
    FCTXLOGDEBUG(get_shared_context(), "create last login user {}, res: {}", req_body.user_id(), res);
    if (res < 0) {
      FCTXLOGERROR(get_shared_context(), "user {}:{} try to add last login table failed, errcode {}", zone_id,
                   req_body.user_id(), res);
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_OPERATE_DB_FAILED);
      TASK_ACTION_RETURN_CODE(res);
    }
  } else if (res != PROJECT_NAMESPACE_ID::err::EN_SUCCESS) {
    FCTXLOGERROR(get_shared_context(), "user {}:{} try to kickoff other session failed, errcode {}", zone_id,
                 req_body.user_id(), res);
    TASK_ACTION_RETURN_CODE(res);
  }

  // 2. 校验登入码，优先使用login_lock里经过续期的老code
  if (login_lock_tb->access_token_code() == req_body.access_token_code() &&
      atfw::util::time::time_utility::sys_now() <= protobuf_to_system_clock(login_lock_tb->access_token_expired())) {
    FCTXLOGDEBUG(get_shared_context(), "user {}:{} use access_token in login_lock table", zone_id, req_body.user_id());
  } else {
    if (util::time::time_utility::sys_now() > protobuf_to_system_clock(login_auth_tb->access_token_expired())) {
      FCTXLOGERROR(get_shared_context(), "user {}({}:{}) login code expired", req_body.open_id(), zone_id,
                   req_body.user_id());
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (0 != UTIL_STRFUNC_STRCMP(req_body.access_token_code().c_str(), login_auth_tb->access_token_code().c_str())) {
      FCTXLOGERROR(get_shared_context(), "user {}({}:{}) login code error(expected: {}, real: {})", req_body.open_id(),
                   zone_id, req_body.user_id(), login_auth_tb->access_token_code(), req_body.access_token_code());
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    FCTXLOGDEBUG(get_shared_context(), "user {}:{} use access_token in login_auth table", zone_id, req_body.user_id());
    login_lock_tb->set_access_token_code(login_auth_tb->access_token_code());
    protobuf_copy_message(*login_lock_tb->mutable_access_token_expired(), login_auth_tb->access_token_expired());
  }

  // 3. 写入登入信息和登入信息续期会在路由系统中完成
  res = RPC_AWAIT_CODE_RESULT(user_manager::me()->create_as<user>(get_shared_context(), req_body.user_id(), zone_id,
                                                                  req_body.open_id(), login_lock_tb,
                                                                  login_lock_cas_version, user_inst));
  is_new_user_ = user_inst && user_inst->is_new_user();
  // ============ 在这之后tb不再有效 ============

  if (!user_inst) {
    if (res < 0 && res >= PROJECT_NAMESPACE_ID::EnErrorCode_MIN) {
      set_response_code(res);
    } else {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    }
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 设置初始化任务，其他任务需要等待用户初始化完成才能继续
  initialization_task_lock_guard initialization_guard{std::static_pointer_cast<user_cache>(user_inst), get_task_id()};

  // 4. 先读本地缓存
  std::shared_ptr<session> my_sess = get_session();
  if (!my_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->set_client_info(req_body.client_info());

  // 8. 设置和Session互相关联
  user_inst->set_session(get_shared_context(), my_sess);
  // 填入上线时间
  my_sess->login_init(get_request());
  res = RPC_AWAIT_CODE_RESULT(user_inst->login_init(get_shared_context()));
  if (res < 0) {
    FCTXLOGERROR(*user_inst, "user login_init failed, result: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    session_manager::me()->remove(get_shared_context(), my_sess, PROJECT_NAMESPACE_ID::EN_CRT_UNKNOWN,
                                  "login init failed");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    TASK_ACTION_RETURN_CODE(res);
  }

  // 9. 替换Session中的用户对象，老Session要下线

  // 如果不存在则是登入过程中掉线了
  if (!my_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  my_sess->set_user(user_inst);

  FWLOGDEBUG("{} login curr data version: {}", *user_inst, user_inst->get_data_version());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_RPC_API int task_action_login::on_success() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  rsp_body.set_heartbeat_interval(logic_config::me()->get_logic_cfg().heartbeat().interval().seconds());
  rsp_body.set_is_new_user(is_new_user_);

  std::shared_ptr<session> s = get_session();
  if (s) {
    s->set_login_task_id(0);
  }

  // 1. 包校验
  user::ptr_t user_inst = user_manager::me()->find_as<user>(req_body.user_id(), get_zone_id());
  if (!user_inst) {
    FCTXLOGWARNING(get_shared_context(), "login success but user {}:{} not found, maybe parrallel login", get_zone_id(),
                   req_body.user_id());
    return get_result();
  }
  rsp_body.set_zone_id(user_inst->get_zone_id());
  rsp_body.set_version_type(user_inst->get_account_info().version_type());

  // TODO(owent) 断线重连，上次收包序号
  // rsp_body.set_last_sequence(user_inst->get_cache_data());

  // Session更换，老session要下线
  if (user_inst->get_session() != s) {
    FWLOGWARNING("{} login success but session changed , remove old session {}:{}", *user_inst, s->get_key().node_id,
                 s->get_key().session_id);
    session_manager::me()->remove(get_shared_context(), s,
                                  static_cast<int32_t>(atfw::gateway::close_reason_t::kKickoff));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    return get_result();
  }

  if (!user_inst->is_inited()) {
    FCTXLOGWARNING(get_shared_context(), "login success but user_inst {}:{} not inited", get_zone_id(),
                   req_body.user_id());
    user_manager::me()->async_remove(get_shared_context(), user_inst, true);
    return get_result();
  }

  // login success and try to restore tick limit
  user_inst->refresh_feature_limit(get_shared_context());
  user_inst->clear_dirty_cache();

  // 自动启动异步任务
  {
    task_type_trait::task_type task_inst;
    task_action_user_async_jobs::ctor_param_t params;
    params.user_inst = user_inst;
    params.caller_context = &get_shared_context();
    task_manager::me()->create_task_with_timeout<task_action_user_async_jobs>(
        task_inst, logic_config::me()->get_cfg_task().nomsg().timeout(), std::move(params));
    if (task_type_trait::empty(task_inst)) {
      FCTXLOGERROR(get_shared_context(), "{}", "create task_action_user_async_jobs failed");
    } else {
      dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

      int res = task_manager::me()->start_task(task_inst, start_data);
      if (res < 0) {
        FWLOGERROR("{} start task_action_user_async_jobs failed, res: {}({})", *user_inst, res,
                   protobuf_mini_dumper_get_error_msg(res));
      }
    }
  }

  // 加入快速保存队列，确保用户登入成功后保存一次在线状态
  user_inst->set_quick_save();
  user_inst->on_login(get_shared_context());

  return get_result();
}

GAMECLIENT_RPC_API int task_action_login::on_failed() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  // 1. 包校验
  std::shared_ptr<session> s = get_session();

  if (0 != req_body.user_id()) {
    user::ptr_t user_inst = user_manager::me()->find_as<user>(req_body.user_id(), get_zone_id());

    // Session更换，直接老session下线即可
    if (user_inst && user_inst->get_session() != s) {
      FWLOGWARNING("{} login success but session changed , remove old session {}:{}", *user_inst, s->get_key().node_id,
                   s->get_key().session_id);
    } else if (user_inst && !user_inst->is_inited()) {
      // 如果创建了未初始化的GameUser对象，则需要移除
      user_inst->clear_dirty_cache();
      user_manager::me()->async_remove(get_shared_context(), user_inst, true);
    }
  }

  // 登入过程中掉线了，直接退出
  if (!s) {
    FCTXLOGWARNING(get_shared_context(), "session ({}){}:{} not found", get_gateway_node_name(), get_gateway_node_id(),
                   get_gateway_session_id());
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  switch (get_response_code()) {
    case PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE:
    case PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN:
    case PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_BAN: {
      FCTXLOGWARNING(get_shared_context(), "{} login failed, rsp code: {}", *s, get_response_code());
      break;
    }
    default: {
      FCTXLOGERROR(get_shared_context(), "{} login failed, rsp code: {}, ret code: {}", *s, get_response_code(),
                   get_result());
      break;
    }
  }

  rsp_body.set_last_sequence(0);
  rsp_body.set_zone_id(0);

  // 手动发包并无情地踢下线
  send_response();

  session_manager::me()->remove(get_shared_context(), s,
                                static_cast<int32_t>(::atframework::gateway::close_reason_t::kFirstIdle));
  return get_result();
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::replace_session(std::shared_ptr<user> user_inst) {
  FWLOGDEBUG("{} relogin using login code: {}", *user_inst, get_request_body().access_token_code());

  // 获取当前Session
  std::shared_ptr<session> cur_sess = get_session();
  if (!cur_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 踢出前一个session
  std::shared_ptr<session> old_sess = user_inst->get_session();

  // 重复的登入包直接接受
  if (cur_sess == old_sess) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (old_sess && old_sess->login_protect(get_request().head().timestamp())) {
    session_manager::me()->remove(get_shared_context(), cur_sess,
                                  static_cast<int32_t>(::atframework::gateway::close_reason_t::kKickoff));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_PROTECT);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->set_session(get_shared_context(), cur_sess);
  if (old_sess) {
    // 下发踢下线包，防止循环重连互踢
    old_sess->set_user(nullptr);
    session_manager::me()->remove(get_shared_context(), old_sess,
                                  static_cast<int32_t>(::atframework::gateway::close_reason_t::kKickoff));
  }
  cur_sess->set_user(user_inst);
  // 填入上线时间
  cur_sess->login_init(get_request());

  if (get_request_body().has_client_info()) {
    user_inst->set_client_info(get_request_body().client_info());
  }

  FWLOGDEBUG("{} relogin curr data version: {}", *user_inst, user_inst->get_data_version());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::await_login_io_task(rpc::context& ctx,
                                                                                std::shared_ptr<user> user_inst) {
  router_user_cache::key_t router_key(router_user_manager::me()->get_type_id(), user_inst->get_zone_id(),
                                      user_inst->get_user_id());
  router_user_cache::ptr_t router_cache = router_user_manager::me()->get_cache(router_key);
  if (!router_cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  task_type_trait::id_type last_pull_object_task_id = router_cache->get_last_pull_object_task_id();
  if (0 == last_pull_object_task_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  task_type_trait::task_type last_pull_object_task = task_manager::me()->get_task(last_pull_object_task_id);
  if (task_type_trait::empty(last_pull_object_task)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  if (task_type_trait::is_exiting(last_pull_object_task)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, last_pull_object_task)));
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::await_logout_io_task(rpc::context& ctx,
                                                                                 std::shared_ptr<user> user_inst) {
  router_user_cache::key_t router_key(router_user_manager::me()->get_type_id(), user_inst->get_zone_id(),
                                      user_inst->get_user_id());
  router_user_cache::ptr_t router_cache = router_user_manager::me()->get_cache(router_key);
  if (!router_cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  if (router_cache->check_flag(router_object_base::flag_t::EN_ROFT_REMOVING_OBJECT) && router_cache->is_io_running()) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(router_cache->await_io_task(ctx)));
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::kickoff_other_session(
    uint64_t user_id, rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock>& login_lock_tb,
    uint64_t& login_lock_cas_version) {
  int ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_lock::get_all(get_shared_context(), user_id, *login_lock_tb, login_lock_cas_version));
  if (ret < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != ret) {
      FCTXLOGERROR(get_shared_context(), "user {} try to get login table failed when login", user_id);
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_OPERATE_DB_FAILED);
    }
    RPC_RETURN_CODE(ret);
  }

  // 如果在线则尝试踢出 TODO

  RPC_RETURN_CODE(0);
}
