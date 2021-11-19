// Copyright 2021 atframework
// Created by owent on 2016/10/6.
//

#include "logic/task_action_login_authorization.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <config/logic_config.h>
#include <rpc/auth/login.h>
#include <rpc/db/login.h>
#include <rpc/db/uuid.h>
#include <rpc/game/gamesvrservice.h>
#include <rpc/game/player.h>
#include <time/time_utility.h>

#include <proto_base.h>

#include <utility/protobuf_mini_dumper.h>
#include <utility/random_engine.h>

#include <data/session.h>
#include <logic/session_manager.h>

#include <memory>
#include <string>
#include <unordered_set>

std::unordered_set<std::string> task_action_login_authorization::white_skip_openids_;

task_action_login_authorization::task_action_login_authorization(dispatcher_start_data_t &&param)
    : task_action_cs_req_base(COPP_MACRO_STD_MOVE(param)),
      is_new_player_(false),
      strategy_type_(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_VERSION_DEFAULT),
      zone_id_(0),
      final_user_id_(0) {}
task_action_login_authorization::~task_action_login_authorization() {}

task_action_login_authorization::result_type task_action_login_authorization::operator()() {
  is_new_player_ = false;
  strategy_type_ = PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_VERSION_DEFAULT;
  zone_id_ = logic_config::me()->get_local_zone_id();

  session::ptr_t my_sess = get_session();
  if (!my_sess) {
    FWLOGERROR("session not found");
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_SYSTEM);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  // 设置登入协议ID
  my_sess->set_login_task_id(get_task_id());

  int res = 0;
  std::string login_code;
  login_code.resize(32);
  rpc::auth::login::generate_login_code(&login_code[0], login_code.size());

  // 1. 包校验
  msg_cref_type req = get_request();
  if (!req.has_body() || !req.body().has_mcs_login_auth_req()) {
    FWLOGERROR("login package error, msg: {}", req.DebugString());
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  const PROJECT_SERVER_FRAME_NAMESPACE_ID::CSLoginAuthReq &msg_body_raw = req.body().mcs_login_auth_req();
  PROJECT_SERVER_FRAME_NAMESPACE_ID::CSLoginAuthReq msg_body;
  protobuf_copy_message(msg_body, msg_body_raw);

  // 2. 版本号及更新逻辑
  uint32_t account_type = msg_body.account().account_type();
  // 调试平台状态，强制重定向平台，并且不验证密码
  if (logic_config::me()->get_cfg_loginsvr().debug_platform() > 0) {
    account_type = logic_config::me()->get_cfg_loginsvr().debug_platform();
  }

  uint32_t channel_id = msg_body.account().channel_id();
  uint32_t system_id = msg_body.system_id();
  // uint32_t version = msg_body.version();

  final_open_id_ = make_openid(msg_body_raw);
  msg_body.set_open_id(final_open_id_);

  // FIXME judge the update strategy
  // do {
  //    strategy_type_ = update_rule_manager::me()->get_version_type(account_type, system_id, version);
  //    // 检查客户端更新信息 更新不分平台值0
  //    if (update_rule_manager::me()->check_update(update_info_, account_type, channel_id, system_id, version,
  //    strategy_type_)) {
  //        set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_VERSION);
  //        return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_VERSION;
  //    }
  //} while (false);

  // 3. 平台校验逻辑
  // 调试模式不用验证
  if (logic_config::me()->get_cfg_loginsvr().debug_platform() <= 0) {
    auth_fn_t vfn = get_verify_fn(account_type);
    if (nullptr == vfn) {
      // 平台不收支持错误码
      set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_INVALID_PLAT);
      FWLOGERROR("user {} report invalid account type {}", msg_body.open_id(), account_type);
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 第三方平台用原始数据
    if (account_type == PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ATI_ACCOUNT_INNER) {
      res = (this->*vfn)(msg_body);
    } else {
      res = (this->*vfn)(msg_body_raw);
      // 有可能第三方认证会生成新的OpenId
      msg_body.set_open_id(final_open_id_);
    }

    if (res < 0) {
      // 平台校验错误错误码
      set_response_code(res);
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }
  }

  // 4. 开放时间限制
  bool pending_check = false;
  if (logic_config::me()->get_cfg_loginsvr().start_time().seconds() > 0 &&
      util::time::time_utility::get_now() < logic_config::me()->get_cfg_loginsvr().start_time().seconds()) {
    pending_check = true;
  }

  if (logic_config::me()->get_cfg_loginsvr().end_time().seconds() > 0 &&
      util::time::time_utility::get_now() > logic_config::me()->get_cfg_loginsvr().end_time().seconds()) {
    pending_check = true;
  }

  if (pending_check) {
    if (white_skip_openids_.size() !=
        static_cast<size_t>(logic_config::me()->get_cfg_loginsvr().white_openid_list_size())) {
      // 清除缓存
      white_skip_openids_.clear();
      for (const std::string &openid : logic_config::me()->get_cfg_loginsvr().white_openid_list()) {
        white_skip_openids_.insert(openid);
      }
    }

    // 白名单放过
    if (white_skip_openids_.end() == white_skip_openids_.find(final_open_id_)) {
      // 维护模式，直接踢下线
      // if (server_maintenance_mode) {
      //   set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_MAINTENANCE);
      // } else {
      set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_SERVER_PENDING);
      // }

      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }
  }

  // 5. 获取当前账户登入信息(如果不存在则直接转到 9)
  do {
    res = rpc::db::login::get(get_shared_context(), msg_body.open_id().c_str(), zone_id_, login_data_, version_);
    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res && res < 0) {
      FWLOGERROR("call login rpc method failed, msg: {}", msg_body.DebugString());
      set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_UNKNOWN);
      return res;
    }

    if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
      break;
    }

    // 6. 是否禁止登入
    if (util::time::time_utility::get_now() < login_data_.ban_time()) {
      set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_BAN);
      FWLOGINFO("user {} try to login but banned", msg_body.open_id());
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 优先使用未过期的gamesvr index
    if (login_data_.has_last_login() && login_data_.last_login().gamesvr_version() ==
                                            logic_config::me()->get_logic().server().reload_timepoint().seconds()) {
      if (util::time::time_utility::get_now() > static_cast<time_t>(login_data_.login_time()) &&
          util::time::time_utility::get_now() - static_cast<time_t>(login_data_.login_time()) <
              logic_config::me()->get_cfg_loginsvr().gamesvr().relogin_expire().seconds()) {
        // use old index
      } else {
        const auto &gamesvr_urls = logic_config::me()->get_cfg_loginsvr().gamesvr().addr();
        if (!gamesvr_urls.empty()) {
          login_data_.mutable_last_login()->set_gamesvr_index(
              util::random_engine::random_between<int32_t>(0, gamesvr_urls.size()));
        }
      }
    } else {
      const auto &gamesvr_urls = logic_config::me()->get_cfg_loginsvr().gamesvr().addr();
      if (!gamesvr_urls.empty()) {
        login_data_.mutable_last_login()->set_gamesvr_index(
            util::random_engine::random_between<int32_t>(0, gamesvr_urls.size()));
        login_data_.mutable_last_login()->set_gamesvr_version(
            logic_config::me()->get_logic().server().reload_timepoint().seconds());
      }
    }

    // 7. 如果在线则尝试踢出
    if (0 != login_data_.router_server_id()) {
      PROJECT_SERVER_FRAME_NAMESPACE_ID::SSPlayerKickOffReq kickoff_req;
      PROJECT_SERVER_FRAME_NAMESPACE_ID::SSPlayerKickOffRsp kickoff_rsp;
      kickoff_req.set_reason(::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
      int32_t ret = static_cast<int>(rpc::game::player_kickoff(get_shared_context(), login_data_.router_server_id(),
                                                               zone_id_, login_data_.user_id(), msg_body.open_id(),
                                                               kickoff_req, kickoff_rsp));
      if (ret) {
        FWLOGERROR("user {} send msg to {:#x} fail: {}", login_data_.open_id(), login_data_.router_server_id(), ret);
        // 超出最后行为时间，视为服务器异常断开。直接允许登入
        time_t last_saved_time = static_cast<time_t>(login_data_.login_time());
        if (last_saved_time < static_cast<time_t>(login_data_.login_code_expired())) {
          last_saved_time = static_cast<time_t>(login_data_.login_code_expired());
        }
        if (last_saved_time < static_cast<time_t>(login_data_.logout_time())) {
          last_saved_time = static_cast<time_t>(login_data_.logout_time());
        }

        if (util::time::time_utility::get_now() - last_saved_time <
            logic_config::me()->get_logic().session().login_code_protect().seconds()) {
          set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_ALREADY_ONLINE);
          return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_PLAYER_KICKOUT;
        } else {
          FWLOGWARNING("user {} send kickoff failed, but login time timeout, conitnue login.", login_data_.open_id());
          login_data_.set_router_server_id(0);
        }
      } else {
        // 8. 验证踢出后的登入pd
        uint64_t old_svr_id = login_data_.router_server_id();
        login_data_.Clear();
        res = rpc::db::login::get(get_shared_context(), msg_body.open_id().c_str(), zone_id_, login_data_, version_);
        if (res < 0) {
          FWLOGERROR("call login rpc method failed, msg: {}", msg_body.DebugString());
          set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_ALREADY_ONLINE);
          return res;
        }

        // 可能目标服务器重启后没有这个玩家的数据而直接忽略请求直接返回成功
        // 这时候走故障恢复流程，直接把router_server_id设成0即可
        if (0 != login_data_.router_server_id() && old_svr_id != login_data_.router_server_id()) {
          FWLOGERROR("user {} logout failed.", msg_body.open_id());
          // 踢下线失败的错误码
          set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_ALREADY_ONLINE);
          return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_PLAYER_KICKOUT;
        }
        login_data_.set_router_server_id(0);
      }
    }
  } while (false);

  // 9. 创建或更新登入信息（login_code）
  // 新用户则创建
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    // 生成容易识别的数字UUID
    int64_t player_uid = rpc::game::player::alloc_user_id(get_shared_context());
    if (player_uid <= 0) {
      FWLOGERROR("call alloc_user_id failed, openid: {}, res: {}", msg_body.open_id(), player_uid);
      set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_CREATE_PLAYER_FAILED);
      return res;
    }

    player_uid += PROJECT_SERVER_FRAME_NAMESPACE_ID::config::EN_GCC_START_PLAYER_ID;

    init_login_data(login_data_, msg_body, player_uid, channel_id);

    // 注册日志
    FWLOGINFO(
        "player {} register account finished, allocate player id: {}, account type: {}, channel: {}, system id: {}",
        msg_body.open_id(), player_uid, account_type, channel_id, system_id);
  }

  // 登入信息
  {
    final_user_id_ = login_data_.user_id();

    login_data_.set_zone_id(zone_id_);
    login_data_.set_stat_login_total_times(login_data_.stat_login_total_times() + 1);

    // 登入码
    login_data_.set_login_code(login_code);
    login_data_.set_login_code_expired(logic_config::me()->get_logic().session().login_code_valid_sec().seconds() +
                                       util::time::time_utility::get_now());

    // 平台信息更新
    PROJECT_SERVER_FRAME_NAMESPACE_ID::account_information *plat_dst = login_data_.mutable_account();
    const PROJECT_SERVER_FRAME_NAMESPACE_ID::DAccountData &plat_src = msg_body.account();

    plat_dst->set_account_type(static_cast<PROJECT_SERVER_FRAME_NAMESPACE_ID::EnAccountTypeID>(account_type));
    if (!plat_src.access().empty()) {
      plat_dst->set_access(plat_src.access());
    }
    plat_dst->set_version_type(strategy_type_);
  }

  // 保存登入信息
  login_data_.set_stat_login_success_times(login_data_.stat_login_success_times() + 1);
  res = rpc::db::login::set(get_shared_context(), msg_body.open_id().c_str(), zone_id_, login_data_, version_);
  if (res < 0) {
    FWLOGERROR("save login data for {} failed, msg:\n{}", msg_body.open_id(), login_data_.DebugString());
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_SYSTEM);
    return res;
  }

  // 10.登入成功
  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_login_authorization::on_success() {
  PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg = add_rsp_msg();

  PROJECT_SERVER_FRAME_NAMESPACE_ID::SCLoginAuthRsp *rsp_body = msg.mutable_body()->mutable_msc_login_auth_rsp();
  rsp_body->set_login_code(login_data_.login_code());
  rsp_body->set_open_id(final_open_id_);  // 最终使用的OpenID
  rsp_body->set_user_id(final_user_id_);
  rsp_body->set_version_type(strategy_type_);
  rsp_body->set_is_new_player(is_new_player_);
  rsp_body->set_zone_id(zone_id_);

  std::shared_ptr<session> my_sess = get_session();

  // 登入过程中掉线了，直接退出
  if (!my_sess) {
    FWLOGERROR("session not found");
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  // 完成登入流程，不再处于登入状态
  my_sess->set_login_task_id(0);

  // 如果是版本过低则要下发更新信息
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_UPDATE_NONE != update_info_.result()) {
    rsp_body->mutable_update_info()->Swap(&update_info_);
  }

  // FIXME 临时的登入服务器，以后走平台下发策略
  const auto &gamesvr_urls = logic_config::me()->get_cfg_loginsvr().gamesvr().addr();
  if (!gamesvr_urls.empty()) {
    for (int32_t i = 0; i < gamesvr_urls.size(); ++i) {
      rsp_body->add_login_address(
          gamesvr_urls.Get((login_data_.last_login().gamesvr_index() + i) % gamesvr_urls.size()));
    }
  }

  // 先发送数据，再通知踢下线
  send_response();

  // 登入成功，不需要再在LoginSvr上操作了
  session_manager::me()->remove(my_sess, ::atframe::gateway::close_reason_t::EN_CRT_EOF);
  return get_result();
}

int task_action_login_authorization::on_failed() {
  std::shared_ptr<session> s = get_session();
  // 登入过程中掉线了，直接退出
  if (!s) {
    FWLOGERROR("session not found");
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &msg = add_rsp_msg();
  PROJECT_SERVER_FRAME_NAMESPACE_ID::SCLoginAuthRsp *rsp_body = msg.mutable_body()->mutable_msc_login_auth_rsp();
  rsp_body->set_login_code("");
  rsp_body->set_open_id(final_open_id_);
  rsp_body->set_user_id(final_user_id_);
  rsp_body->set_ban_time(login_data_.ban_time());
  rsp_body->set_version_type(strategy_type_);
  rsp_body->set_zone_id(zone_id_);

  // 如果是版本过低则要下发更新信息
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_UPDATE_NONE != update_info_.result()) {
    rsp_body->mutable_update_info()->Swap(&update_info_);
  }

  // if (PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_SERVER_PENDING == get_response_code() ||
  // PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_MAINTENANCE == get_response_code())
  // {
  //   rsp_body->set_start_time(logic_config::me()->get_logic().server_open_time);
  // } else {
  //   FWLOGERROR("session [{:#x}, {}] login failed", get_gateway_info().first, get_gateway_info().second);
  // }

  send_response();

  // 无情地踢下线
  session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
  return get_result();
}

int32_t task_action_login_authorization::check_proto_update(uint32_t ver_no) {
  // FIXME check if client version is available
  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

task_action_login_authorization::auth_fn_t task_action_login_authorization::get_verify_fn(uint32_t account_type) {
  static auth_fn_t all_auth_fns[PROJECT_SERVER_FRAME_NAMESPACE_ID::EnAccountTypeID_ARRAYSIZE];

  if (nullptr != all_auth_fns[PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ATI_ACCOUNT_INNER]) {
    return all_auth_fns[account_type % PROJECT_SERVER_FRAME_NAMESPACE_ID::EnAccountTypeID_ARRAYSIZE];
  }

  all_auth_fns[PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ATI_ACCOUNT_INNER] =
      &task_action_login_authorization::verify_plat_account;
  return all_auth_fns[account_type % PROJECT_SERVER_FRAME_NAMESPACE_ID::EnAccountTypeID_ARRAYSIZE];
}

void task_action_login_authorization::init_login_data(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login &tb,
                                                      const PROJECT_SERVER_FRAME_NAMESPACE_ID::CSLoginAuthReq &req,
                                                      int64_t player_uid, uint32_t channel_id) {
  tb.set_open_id(req.open_id());
  tb.set_user_id(static_cast<uint64_t>(player_uid));
  tb.set_zone_id(zone_id_);

  tb.set_router_server_id(0);
  tb.set_router_version(0);
  tb.set_login_time(0);
  tb.set_register_time(util::time::time_utility::get_now());

  tb.set_ban_time(0);

  // tb.mutable_account()->mutable_profile()->mutable_uuid()->set_open_id(req.open_id());
  tb.mutable_account()->set_channel_id(channel_id);

  version_.assign("0");
  is_new_player_ = true;

  tb.set_stat_login_total_times(0);
  tb.set_stat_login_success_times(0);
  tb.set_stat_login_failed_times(0);
}

std::string task_action_login_authorization::make_openid(const PROJECT_SERVER_FRAME_NAMESPACE_ID::CSLoginAuthReq &req) {
  return rpc::auth::login::make_open_id(zone_id_, req.account().account_type(), req.account().channel_id(),
                                        req.open_id());
}

int task_action_login_authorization::verify_plat_account(const PROJECT_SERVER_FRAME_NAMESPACE_ID::CSLoginAuthReq &req) {
  PROJECT_SERVER_FRAME_NAMESPACE_ID::table_login tb;
  std::string version;
  int res = rpc::db::login::get(get_shared_context(), req.open_id().c_str(), zone_id_, tb, version);
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND != res && res < 0) {
    FWLOGERROR("call login rpc method failed, msg: {}", req.DebugString());
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_SYSTEM;
  }

  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_SUCCESS;
  }

  // 校验密码
  if (!req.has_account()) {
    if (tb.account().access().empty()) {
      return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_SUCCESS;
    }

    // 参数错误
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  if (req.account().access() != tb.account().access()) {
    // 平台校验不通过错误码
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_VERIFY;
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_SUCCESS;
}