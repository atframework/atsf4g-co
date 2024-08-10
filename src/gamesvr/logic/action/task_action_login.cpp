// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:57:03

#include "logic/action/task_action_login.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/db/login.h>

#include <data/player.h>
#include <data/session.h>
#include <logic/player_manager.h>
#include <logic/session_manager.h>

#include <config/logic_config.h>
#include <proto_base.h>
#include <rpc/db/player.h>
#include <rpc/rpc_async_invoke.h>

#include <router/router_player_manager.h>

#include <dispatcher/task_manager.h>

#include <memory>
#include <string>

#include "logic/action/task_action_player_async_jobs.h"

GAMECLIENT_RPC_API task_action_login::task_action_login(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)), is_new_player_(false) {}

GAMECLIENT_RPC_API task_action_login::~task_action_login() {}

GAMECLIENT_RPC_API const char* task_action_login::name() const { return "task_action_login"; }

GAMECLIENT_RPC_API task_action_login::result_type task_action_login::operator()() {
  const rpc_request_type& req_body = get_request_body();

  is_new_player_ = false;
  uint32_t zone_id = logic_config::me()->get_local_zone_id();
  set_user_key(req_body.user_id(), zone_id);

  rpc::result_code_type::value_type res = 0;

  // 先查找用户缓存，使用缓存。如果缓存正确则不需要拉取login表和user表
  player::ptr_t user = player_manager::me()->find_as<player>(req_body.user_id(), zone_id);
  // 如果先前的login还在执行中，需要等待路由系统的io任务完成，否则前一个登入尚未设置router对象为writable
  // 后面的remove不会等待IO事件，而本次的login写表时会冲突。最终导致两个登入都失败
  if (user && !user->is_writable()) {
    res = RPC_AWAIT_CODE_RESULT(await_io_task(get_shared_context(), user));
    if (res < 0) {
      set_response_code(res);
      TASK_ACTION_RETURN_CODE(res);
    }
  }

  if (user && user->has_initialization_task_id()) {
    res = RPC_AWAIT_CODE_RESULT(user->await_initialization_task(get_shared_context()));
    if (res < 0) {
      TASK_ACTION_RETURN_CODE(res);
    }
  }

  if (user && user->get_login_info().login_code() == req_body.login_code() &&
      util::time::time_utility::get_sys_now() <= static_cast<time_t>(user->get_login_info().login_code_expired()) &&
      user->is_writable()) {
    RPC_AWAIT_IGNORE_RESULT(replace_session(user));
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (player_manager::me()->has_create_user_lock(req_body.user_id(), zone_id)) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 如果有缓存要强制失效，因为可能其他地方登入了，这时候也不能复用缓存
  RPC_AWAIT_IGNORE_RESULT(player_manager::me()->remove(get_shared_context(), req_body.user_id(), zone_id, true));
  user.reset();

  PROJECT_NAMESPACE_ID::table_login tb;
  std::string version;
  res = RPC_AWAIT_CODE_RESULT(
      rpc::db::login::get(get_shared_context(), req_body.open_id().c_str(), zone_id, tb, version));
  if (res < 0) {
    FCTXLOGERROR(get_shared_context(), "player {} not found", req_body.open_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (req_body.user_id() != tb.user_id()) {
    FCTXLOGERROR(get_shared_context(), "player {} expect user_id={}, but we got {} not found", req_body.open_id(),
                 tb.user_id(), req_body.user_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_USERID_NOT_MATCH);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 2. 校验登入码
  if (util::time::time_utility::get_sys_now() > tb.login_code_expired()) {
    FCTXLOGERROR(get_shared_context(), "player {}({}:{}) login code expired", req_body.open_id(), zone_id,
                 req_body.user_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (0 != UTIL_STRFUNC_STRCMP(req_body.login_code().c_str(), tb.login_code().c_str())) {
    FCTXLOGERROR(get_shared_context(), "player {}({}:{}) login code error(expected: {}, real: {})", req_body.open_id(),
                 zone_id, req_body.user_id(), tb.login_code(), req_body.login_code());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 3. 写入登入信息和登入信息续期会在路由系统中完成

  res = RPC_AWAIT_CODE_RESULT(player_manager::me()->create_as<player>(get_shared_context(), req_body.user_id(), zone_id,
                                                                      req_body.open_id(), tb, version, user));
  is_new_player_ = user && user->get_version() == "1";
  // ============ 在这之后tb不再有效 ============

  if (!user) {
    if (res < 0 && res >= PROJECT_NAMESPACE_ID::EnErrorCode_MIN) {
      set_response_code(res);
    } else {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_USER_NOT_FOUND);
    }
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 设置初始化任务，其他任务需要等待玩家初始化完成才能继续
  initialization_task_lock_guard initialization_guard{std::static_pointer_cast<player_cache>(user), get_task_id()};

  // 4. 先读本地缓存
  std::shared_ptr<session> my_sess = get_session();
  if (!my_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user->set_client_info(req_body.client_info());

  // 8. 设置和Session互相关联
  user->set_session(get_shared_context(), my_sess);
  user->login_init(get_shared_context());

  // 如果不存在则是登入过程中掉线了
  if (!my_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  my_sess->set_player(user);

  FWPLOGDEBUG(*user, "login curr data version: {}", user->get_version());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_RPC_API int task_action_login::on_success() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  rsp_body.set_heartbeat_interval(logic_config::me()->get_logic().heartbeat().interval().seconds());
  rsp_body.set_is_new_player(is_new_player_);

  std::shared_ptr<session> s = get_session();
  if (s) {
    s->set_login_task_id(0);
  }

  // 1. 包校验
  player::ptr_t user = player_manager::me()->find_as<player>(req_body.user_id(), get_zone_id());
  if (!user) {
    FCTXLOGWARNING(get_shared_context(), "login success but user {}:{} not found, maybe parrallel login", get_zone_id(),
                   req_body.user_id());
    return get_result();
  }
  rsp_body.set_zone_id(static_cast<int32_t>(user->get_zone_id()));
  rsp_body.set_version_type(user->get_account_info().version_type());

  // TODO(owent) 断线重连，上次收包序号
  // rsp_body.set_last_sequence(user->get_cache_data());

  // Session更换，老session要下线
  if (user->get_session() != s) {
    FWPLOGWARNING(*user, "login success but session changed , remove old session {}:{}", s->get_key().node_id,
                  s->get_key().session_id);
    session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    return get_result();
  }

  if (!user->is_inited()) {
    FCTXLOGWARNING(get_shared_context(), "login success but user {}:{} not inited", get_zone_id(), req_body.user_id());
    player_manager::me()->async_remove(get_shared_context(), user, true);
    return get_result();
  }

  // login success and try to restore tick limit
  user->refresh_feature_limit(get_shared_context());
  user->clear_dirty_cache();

  // 自动启动异步任务
  {
    task_type_trait::id_type tid = 0;
    task_action_player_async_jobs::ctor_param_t params;
    params.user = user;
    params.caller_context = &get_shared_context();
    task_manager::me()->create_task_with_timeout<task_action_player_async_jobs>(
        tid, logic_config::me()->get_cfg_task().nomsg().timeout().seconds(), COPP_MACRO_STD_MOVE(params));
    if (0 == tid) {
      FCTXLOGERROR(get_shared_context(), "{}", "create task_action_player_async_jobs failed");
    } else {
      dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

      int res = task_manager::me()->start_task(tid, start_data);
      if (res < 0) {
        FWPLOGERROR(*user, "start task_action_player_async_jobs failed, res: {}({})", res,
                    protobuf_mini_dumper_get_error_msg(res));
      }
    }
  }

  // 加入快速保存队列，确保玩家登入成功后保存一次在线状态
  user->set_quick_save();

  return get_result();
}

GAMECLIENT_RPC_API int task_action_login::on_failed() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  // 1. 包校验
  std::shared_ptr<session> s = get_session();

  if (0 != req_body.user_id()) {
    player::ptr_t user = player_manager::me()->find_as<player>(req_body.user_id(), get_zone_id());

    // Session更换，直接老session下线即可
    if (user && user->get_session() != s) {
      FWPLOGWARNING(*user, "login success but session changed , remove old session {}:{}", s->get_key().node_id,
                    s->get_key().session_id);
    } else if (user && !user->is_inited()) {
      // 如果创建了未初始化的GameUser对象，则需要移除
      user->clear_dirty_cache();
      player_manager::me()->async_remove(get_shared_context(), user, true);
    }
  }

  // 登入过程中掉线了，直接退出
  if (!s) {
    FCTXLOGWARNING(get_shared_context(), "session [{},{}] not found", get_gateway_info().first,
                   get_gateway_info().second);
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  switch (get_response_code()) {
    case PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE:
    case PROJECT_NAMESPACE_ID::EN_ERR_NOT_LOGIN:
    case PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_BAN: {
      FCTXLOGWARNING(get_shared_context(), "session [{},{}] login failed, rsp code: {}, ret code: {}",
                     get_gateway_info().first, get_gateway_info().second, get_response_code(), get_result());
      break;
    }
    default: {
      FCTXLOGERROR(get_shared_context(), "session [{},{}] login failed, rsp code: {}, ret code: {}",
                   get_gateway_info().first, get_gateway_info().second, get_response_code(), get_result());
      break;
    }
  }

  rsp_body.set_last_sequence(0);
  rsp_body.set_zone_id(0);

  // 手动发包并无情地踢下线
  send_response();

  session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_FIRST_IDLE);
  return get_result();
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::replace_session(std::shared_ptr<player> user) {
  FWPLOGDEBUG(*user, "relogin using login code: {}", get_request_body().login_code());

  // 获取当前Session
  std::shared_ptr<session> cur_sess = get_session();
  if (!cur_sess) {
    FCTXLOGERROR(get_shared_context(), "{}", "session not found");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 踢出前一个session
  std::shared_ptr<session> old_sess = user->get_session();

  // 重复的登入包直接接受
  if (cur_sess == old_sess) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user->set_session(get_shared_context(), cur_sess);
  if (old_sess) {
    // 下发踢下线包，防止循环重连互踢
    old_sess->set_player(nullptr);
    session_manager::me()->remove(old_sess, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
  }
  cur_sess->set_player(user);

  if (get_request_body().has_client_info()) {
    user->set_client_info(get_request_body().client_info());
  }

  FWPLOGDEBUG(*user, "relogin curr data version: {}", user->get_version());

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_RPC_API rpc::result_code_type task_action_login::await_io_task(rpc::context& ctx,
                                                                          std::shared_ptr<player> user) {
  router_player_cache::key_t router_key(router_player_manager::me()->get_type_id(), user->get_zone_id(),
                                        user->get_user_id());
  router_player_cache::ptr_t router_cache = router_player_manager::me()->get_cache(router_key);
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
