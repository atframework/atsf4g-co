// Copyright 2021 atframework
// Created by owent on 2016/10/6.
//

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <rpc/db/player.h>

#include <data/player_cache.h>
#include <data/session.h>
#include <logic/player_manager.h>
#include <logic/session_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include "task_action_player_logout.h"

task_action_player_logout::task_action_player_logout(ctor_param_t&& param)
    : task_action_no_req_base(param), ctor_param_(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_logout::~task_action_player_logout() {}

task_action_player_logout::result_type task_action_player_logout::operator()() {
  FWLOGDEBUG("task_action_player_logout for session [{:#}, {}] start", ctor_param_.atgateway_bus_id,
             ctor_param_.atgateway_session_id);
  session::key_t key;
  key.bus_id = ctor_param_.atgateway_bus_id;
  key.session_id = ctor_param_.atgateway_session_id;
  session::ptr_t s = session_manager::me()->find(key);
  session::flag_guard_t flag_guard;
  if (s) {
    flag_guard.setup(*s, session::flag_t::EN_SESSION_FLAG_CLOSING);
    // 如果正在其他任务中执行移除流程，这里直接跳过即可
    if (!flag_guard) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 连接断开的时候需要保存一下数据
    player_cache::ptr_t user = s->get_player();
    bool user_writeable = false;
    // 如果玩家数据是缓存，不是实际登入点，则不用保存
    if (user) {
      user_writeable = user->is_writable();
      set_user_key(user->get_user_id(), user->get_zone_id());
      user->set_session(get_shared_context(), nullptr);
      s->set_player(nullptr);
    }
    session_manager::me()->remove(s);

    if (user) {
      set_response_code(user->await_before_logout_tasks());
      if (get_response_code() < 0) {
        FWPLOGERROR(*user, "kickoff failed, res: {}({})", get_response_code(),
                    protobuf_mini_dumper_get_error_msg(get_response_code()));

        return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      }

      if (user_writeable && !user->has_session()) {
        auto remove_res = RPC_AWAIT_CODE_RESULT(player_manager::me()->remove(get_shared_context(), user, false));
        if (remove_res < 0) {
          set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
          FWPLOGERROR(*user, "logout failed, res: {}({})", static_cast<int>(remove_res),
                      protobuf_mini_dumper_get_error_msg(remove_res));
        }
      }
    }
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_player_logout::on_success() { return get_result(); }

int task_action_player_logout::on_failed() { return get_result(); }
