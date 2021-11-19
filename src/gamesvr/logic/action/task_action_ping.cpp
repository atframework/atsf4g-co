// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:57:03

#include "task_action_ping.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/login.h>

#include <data/player.h>
#include <data/session.h>
#include <logic/player_manager.h>
#include <logic/session_manager.h>

#include <config/logic_config.h>
#include <proto_base.h>
#include <time/time_utility.h>

#include <memory>

task_action_ping::task_action_ping(dispatcher_start_data_t&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_ping::~task_action_ping() {}

const char* task_action_ping::name() const { return "task_action_ping"; }

task_action_ping::result_type task_action_ping::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  // 用户更新心跳信息
  user->update_heartbeat();

  // 心跳超出容忍值，直接提下线
  if (user->get_heartbeat_data().continue_error_times >= logic_config::me()->get_logic().heartbeat().error_times()) {
    // 封号一段时间

    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_BAN);
    int kick_off_reason = PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_CRT_LOGIN_BAN;
    PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login tb;
    do {
      std::string login_ver;
      int res =
          rpc::db::login::get(get_shared_context(), user->get_open_id().c_str(), user->get_zone_id(), tb, login_ver);
      if (res < 0) {
        WLOGERROR("call login rpc Get method failed, user %s, res: %d", user->get_open_id().c_str(), res);
        break;
      }

      if (!tb.has_except()) {
        tb.mutable_except()->set_last_except_time(0);
        tb.mutable_except()->set_except_con_times(0);
        tb.mutable_except()->set_except_sum_times(0);
      }
      tb.mutable_except()->set_except_sum_times(tb.except().except_sum_times() + 1);
      if (0 != tb.except().last_except_time() &&
          util::time::time_utility::get_now() - tb.except().last_except_time() <=
              logic_config::me()->get_logic().heartbeat().ban_time_bound().seconds()) {
        tb.mutable_except()->set_except_con_times(tb.except().except_con_times() + 1);
      } else {
        tb.mutable_except()->set_except_con_times(1);
      }

      tb.mutable_except()->set_last_except_time(util::time::time_utility::get_now());

      if (tb.except().except_con_times() >= logic_config::me()->get_logic().heartbeat().ban_error_times()) {
        tb.set_ban_time(static_cast<uint32_t>(util::time::time_utility::get_now() +
                                              logic_config::me()->get_logic().session().login_ban_time().seconds()));
        kick_off_reason = PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_CRT_LOGIN_BAN;
        set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_BAN);
      } else {
        kick_off_reason = PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_CRT_SPEED_WARNING;
        set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_SPEED_WARNING);
      }
      // 保存封号结果
      res = rpc::db::login::set(get_shared_context(), user->get_open_id().c_str(), user->get_zone_id(), tb, login_ver);
      if (res < 0) {
        WLOGERROR("call login rpc Set method failed, user %s, zone id: %u, res: %d", user->get_open_id().c_str(),
                  user->get_zone_id(), res);
      } else {
        user->load_and_move_login_info(COPP_MACRO_STD_MOVE(tb), login_ver);
      }
    } while (false);

    // 踢出原因是封号
    // 游戏速度异常，强制踢出，这时候也可能是某些手机切到后台再切回来会加速运行，这时候强制断开走登入流程，防止高频发包

    // 先发包
    send_response();

    session::ptr_t sess = user->get_session();
    if (sess) {
      sess->send_kickoff(kick_off_reason);
    }

    // 再踢下线
    player_manager::me()->remove(user);
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_ping::on_success() { return get_result(); }

int task_action_ping::on_failed() { return get_result(); }
