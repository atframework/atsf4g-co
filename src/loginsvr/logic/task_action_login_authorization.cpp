//
// Created by owent on 2016/10/6.
//
#include <std/foreach.h>

#include <log/log_wrapper.h>

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/logic_config.h>
#include <rpc/auth/login.h>
#include <rpc/db/login.h>
#include <rpc/db/uuid.h>
#include <rpc/game/player.h>
#include <time/time_utility.h>


#include <proto_base.h>

#include <utility/protobuf_mini_dumper.h>
#include <utility/random_engine.h>

#include <data/session.h>
#include <logic/session_manager.h>


#include "task_action_login_authorization.h"

UTIL_ENV_AUTO_SET(std::string) task_action_login_authorization::white_skip_openids_;

task_action_login_authorization::task_action_login_authorization(dispatcher_start_data_t COPP_MACRO_RV_REF param)
    : task_action_cs_req_base(COPP_MACRO_STD_MOVE(param)), is_new_player_(false), strategy_type_(hello::EN_VERSION_DEFAULT), zone_id_(0), final_user_id_(0) {}
task_action_login_authorization::~task_action_login_authorization() {}

int task_action_login_authorization::operator()() {
    is_new_player_ = false;
    strategy_type_ = hello::EN_VERSION_DEFAULT;
    zone_id_       = logic_config::me()->get_cfg_logic().zone_id;

    session::ptr_t my_sess = get_session();
    if (!my_sess) {
        WLOGERROR("session not found");
        set_rsp_code(hello::EN_ERR_SYSTEM);
        return hello::err::EN_SYS_PARAM;
    }
    // 设置登入协议ID
    my_sess->set_login_task_id(get_task_id());

    int         res = 0;
    std::string login_code;
    login_code.resize(32);
    rpc::auth::login::generate_login_code(&login_code[0], login_code.size());

    // 1. 包校验
    msg_cref_type req = get_request();
    if (!req.has_body() || !req.body().has_mcs_login_auth_req()) {
        WLOGERROR("login package error, msg: %s", req.DebugString().c_str());
        set_rsp_code(hello::EN_ERR_INVALID_PARAM);
        return hello::err::EN_SUCCESS;
    }

    const ::hello::CSLoginAuthReq &msg_body_raw = req.body().mcs_login_auth_req();
    ::hello::CSLoginAuthReq        msg_body;
    protobuf_copy_message(msg_body, msg_body_raw);

    // 2. 版本号及更新逻辑
    uint32_t account_type = msg_body.account().account_type();
    // 调试平台状态，强制重定向平台，并且不验证密码
    if (logic_config::me()->get_cfg_svr_login().debug_platform_mode > 0) {
        account_type = logic_config::me()->get_cfg_svr_login().debug_platform_mode;
    }

    uint32_t channel_id    = msg_body.account().channel_id();
    uint32_t platform_type = msg_body.platform_type();
    // uint32_t version = msg_body.version();

    final_open_id_ = make_openid(msg_body_raw);
    msg_body.set_open_id(final_open_id_);

    // TODO judge the update strategy
    // do {
    //    strategy_type_ = update_rule_manager::me()->get_version_type(account_type, platform_type, version);
    //    // 检查客户端更新信息 更新不分平台值0
    //    if (update_rule_manager::me()->check_update(update_info_, account_type, channel_id, platform_type, version, strategy_type_)) {
    //        set_rsp_code(hello::EN_ERR_LOGIN_VERSION);
    //        return hello::EN_ERR_LOGIN_VERSION;
    //    }
    //} while (false);

    // 3. 平台校验逻辑
    // 调试模式不用验证
    if (logic_config::me()->get_cfg_svr_login().debug_platform_mode <= 0) {
        auth_fn_t vfn = get_verify_fn(account_type);
        if (NULL == vfn) {
            // 平台不收支持错误码
            set_rsp_code(hello::EN_ERR_LOGIN_INVALID_PLAT);
            WLOGERROR("user %s report invalid account type %u", msg_body.open_id().c_str(), account_type);
            return hello::err::EN_SUCCESS;
        }

        // 第三方平台用原始数据
        if (account_type == hello::EN_ATI_ACCOUNT_INNER) {
            res = (this->*vfn)(msg_body);
        } else {
            res = (this->*vfn)(msg_body_raw);
            // 有可能第三方认证会生成新的OpenId
            msg_body.set_open_id(final_open_id_);
        }

        if (res < 0) {
            // 平台校验错误错误码
            set_rsp_code(res);
            return hello::err::EN_SUCCESS;
        }
    }

    // 4. 开放时间限制
    bool pending_check = false;
    if (logic_config::me()->get_cfg_svr_login().start_time > 0 && util::time::time_utility::get_now() < logic_config::me()->get_cfg_svr_login().start_time) {
        pending_check = true;
    }

    if (logic_config::me()->get_cfg_svr_login().end_time > 0 && util::time::time_utility::get_now() > logic_config::me()->get_cfg_svr_login().end_time) {
        pending_check = true;
    }

    if (pending_check) {
        if (white_skip_openids_.size() != logic_config::me()->get_cfg_svr_login().white_openid_list.size()) {
            // 清除缓存
            white_skip_openids_.clear();
            owent_foreach(const std::string &openid, logic_config::me()->get_cfg_svr_login().white_openid_list) { white_skip_openids_.insert(openid); }
        }

        // 白名单放过
        if (white_skip_openids_.end() == white_skip_openids_.find(final_open_id_)) {
            // 维护模式，直接踢下线
            if (logic_config::me()->get_cfg_logic().server_maintenance_mode) {
                set_rsp_code(hello::EN_ERR_MAINTENANCE);
            } else {
                set_rsp_code(hello::EN_ERR_LOGIN_SERVER_PENDING);
            }

            return hello::err::EN_SUCCESS;
        }
    }

    // 5. 获取当前账户登入信息(如果不存在则直接转到 9)
    do {
        res = rpc::db::login::get(msg_body.open_id().c_str(), zone_id_, login_data_, version_);
        if (hello::err::EN_DB_RECORD_NOT_FOUND != res && res < 0) {
            WLOGERROR("call login rpc method failed, msg: %s", msg_body.DebugString().c_str());
            set_rsp_code(hello::EN_ERR_UNKNOWN);
            return res;
        }

        if (hello::err::EN_DB_RECORD_NOT_FOUND == res) {
            break;
        }

        // 6. 是否禁止登入
        if (util::time::time_utility::get_now() < login_data_.ban_time()) {
            set_rsp_code(hello::EN_ERR_LOGIN_BAN);
            WLOGINFO("user %s try to login but banned", msg_body.open_id().c_str());
            return hello::err::EN_SUCCESS;
        }

        // 优先使用未过期的gamesvr index
        if (login_data_.has_last_login() && login_data_.last_login().gamesvr_version() == logic_config::me()->get_cfg_svr_login().reload_version) {
            if (util::time::time_utility::get_now() > static_cast<time_t>(login_data_.login_time()) &&
                util::time::time_utility::get_now() - static_cast<time_t>(login_data_.login_time()) <
                    logic_config::me()->get_cfg_svr_login().relogin_expired_time) {
                // use old index
            } else {
                const std::vector<std::string> &gamesvr_urls = logic_config::me()->get_cfg_svr_login().gamesvr_list;
                if (!gamesvr_urls.empty()) {
                    login_data_.mutable_last_login()->set_gamesvr_index(util::random_engine::random_between<uint32_t>(0, gamesvr_urls.size()));
                }
            }
        } else {
            const std::vector<std::string> &gamesvr_urls = logic_config::me()->get_cfg_svr_login().gamesvr_list;
            if (!gamesvr_urls.empty()) {
                login_data_.mutable_last_login()->set_gamesvr_index(util::random_engine::random_between<uint32_t>(0, gamesvr_urls.size()));
                login_data_.mutable_last_login()->set_gamesvr_version(logic_config::me()->get_cfg_svr_login().reload_version);
            }
        }

        // 7. 如果在线则尝试踢出
        if (0 != login_data_.router_server_id()) {
            int32_t ret = rpc::game::player::send_kickoff(login_data_.router_server_id(), login_data_.user_id(), zone_id_, login_data_.open_id(),
                                                          ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
            if (ret) {
                WLOGERROR("user %s send msg to 0x%llx fail: %d", login_data_.open_id().c_str(), static_cast<unsigned long long>(login_data_.router_server_id()),
                          ret);
                // 超出最后行为时间，视为服务器异常断开。直接允许登入
                time_t last_saved_time = static_cast<time_t>(login_data_.login_time());
                if (last_saved_time < static_cast<time_t>(login_data_.login_code_expired())) {
                    last_saved_time = static_cast<time_t>(login_data_.login_code_expired());
                }
                if (last_saved_time < static_cast<time_t>(login_data_.logout_time())) {
                    last_saved_time = static_cast<time_t>(login_data_.logout_time());
                }

                if (util::time::time_utility::get_now() - last_saved_time < logic_config::me()->get_cfg_logic().session_login_code_protect) {
                    set_rsp_code(hello::EN_ERR_LOGIN_ALREADY_ONLINE);
                    return hello::err::EN_PLAYER_KICKOUT;
                } else {
                    WLOGWARNING("user %s send kickoff failed, but login time timeout, conitnue login.", login_data_.open_id().c_str());
                    login_data_.set_router_server_id(0);
                }
            } else {
                // 8. 验证踢出后的登入pd
                login_data_.Clear();
                uint64_t old_svr_id = login_data_.router_server_id();
                res                 = rpc::db::login::get(msg_body.open_id().c_str(), zone_id_, login_data_, version_);
                if (res < 0) {
                    WLOGERROR("call login rpc method failed, msg: %s", msg_body.DebugString().c_str());
                    set_rsp_code(hello::EN_ERR_LOGIN_ALREADY_ONLINE);
                    return res;
                }

                // 可能目标服务器重启后没有这个玩家的数据而直接忽略请求直接返回成功
                // 这时候走故障恢复流程，直接把router_server_id设成0即可
                if (0 != login_data_.router_server_id() && old_svr_id != login_data_.router_server_id()) {
                    WLOGERROR("user %s loginout failed.", msg_body.open_id().c_str());
                    // 踢下线失败的错误码
                    set_rsp_code(hello::EN_ERR_LOGIN_ALREADY_ONLINE);
                    return hello::err::EN_PLAYER_KICKOUT;
                }
                login_data_.set_router_server_id(0);
            }
        }
    } while (false);

    // 9. 创建或更新登入信息（login_code）
    // 新用户则创建
    if (hello::err::EN_DB_RECORD_NOT_FOUND == res) {
        // 生成容易识别的数字UUID
        int64_t player_uid = rpc::db::uuid::generate_global_unique_id(hello::config::EN_GUIT_PLAYER_ID, 0, 0);
        if (player_uid <= 0) {
            WLOGERROR("call generate_global_unique_id failed, openid:%s, res:%d", msg_body.open_id().c_str(), static_cast<int>(player_uid));
            set_rsp_code(hello::EN_ERR_LOGIN_CREATE_PLAYER_FAILED);
            return res;
        }

        player_uid += hello::config::EN_GCC_START_PLAYER_ID;

        init_login_data(login_data_, msg_body, player_uid, channel_id);

        // 注册日志
        WLOGINFO("player %s register account finished, allocate player id: %lld, account type: %u, channel: %u, platform: %d", msg_body.open_id().c_str(),
                 static_cast<long long>(player_uid), account_type, channel_id, platform_type);
    }

    // 登入信息
    {
        final_user_id_ = login_data_.user_id();

        login_data_.set_zone_id(zone_id_);
        login_data_.set_stat_login_total_times(login_data_.stat_login_total_times() + 1);

        // 登入码
        login_data_.set_login_code(login_code);
        login_data_.set_login_code_expired(logic_config::me()->get_cfg_logic().session_login_code_valid_sec + util::time::time_utility::get_now());

        // 平台信息更新
        ::hello::account_information *plat_dst = login_data_.mutable_account();
        const ::hello::DAccountData & plat_src = msg_body.account();

        plat_dst->set_account_type(static_cast<hello::EnAccountTypeID>(account_type));
        if (!plat_src.access().empty()) {
            plat_dst->set_access(plat_src.access());
        }
        plat_dst->set_version_type(strategy_type_);
    }

    // 保存登入信息
    login_data_.set_stat_login_success_times(login_data_.stat_login_success_times() + 1);
    res = rpc::db::login::set(msg_body.open_id().c_str(), zone_id_, login_data_, version_);
    if (res < 0) {
        WLOGERROR("save login data for %s failed, msg:\n%s", msg_body.open_id().c_str(), login_data_.DebugString().c_str());
        set_rsp_code(hello::EN_ERR_SYSTEM);
        return res;
    }

    // 10.登入成功
    return hello::err::EN_SUCCESS;
}

int task_action_login_authorization::on_success() {
    hello::CSMsg &msg = add_rsp_msg();

    ::hello::SCLoginAuthRsp *rsp_body = msg.mutable_body()->mutable_msc_login_auth_rsp();
    rsp_body->set_login_code(login_data_.login_code());
    rsp_body->set_open_id(final_open_id_); // 最终使用的OpenID
    rsp_body->set_user_id(final_user_id_);
    rsp_body->set_version_type(strategy_type_);
    rsp_body->set_is_new_player(is_new_player_);
    rsp_body->set_zone_id(zone_id_);

    std::shared_ptr<session> my_sess = get_session();

    // 登入过程中掉线了，直接退出
    if (!my_sess) {
        WLOGERROR("session not found");
        return hello::err::EN_SUCCESS;
    }

    // 完成登入流程，不再处于登入状态
    my_sess->set_login_task_id(0);

    // 如果是版本过低则要下发更新信息
    if (hello::EN_UPDATE_NONE != update_info_.result()) {
        rsp_body->mutable_update_info()->Swap(&update_info_);
    }

    // TODO 临时的登入服务器，以后走平台下发策略
    const std::vector<std::string> &gamesvr_urls = logic_config::me()->get_cfg_svr_login().gamesvr_list;
    if (!gamesvr_urls.empty()) {
        for (size_t i = 0; i < gamesvr_urls.size(); ++i) {
            rsp_body->add_login_address(gamesvr_urls[(login_data_.last_login().gamesvr_index() + i) % gamesvr_urls.size()]);
        }
    }

    // 先发送数据，再通知踢下线
    send_rsp_msg();

    // 登入成功，不需要再在LoginSvr上操作了
    session_manager::me()->remove(my_sess, ::atframe::gateway::close_reason_t::EN_CRT_EOF);
    return get_ret_code();
}

int task_action_login_authorization::on_failed() {
    std::shared_ptr<session> s = get_session();
    // 登入过程中掉线了，直接退出
    if (!s) {
        WLOGERROR("session not found");
        return hello::err::EN_SUCCESS;
    }

    hello::CSMsg &         msg      = add_rsp_msg();
    hello::SCLoginAuthRsp *rsp_body = msg.mutable_body()->mutable_msc_login_auth_rsp();
    rsp_body->set_login_code("");
    rsp_body->set_open_id(final_open_id_);
    rsp_body->set_user_id(final_user_id_);
    rsp_body->set_ban_time(login_data_.ban_time());
    rsp_body->set_version_type(strategy_type_);
    rsp_body->set_zone_id(zone_id_);

    // 如果是版本过低则要下发更新信息
    if (hello::EN_UPDATE_NONE != update_info_.result()) {
        rsp_body->mutable_update_info()->Swap(&update_info_);
    }

    if (hello::EN_ERR_LOGIN_SERVER_PENDING == get_rsp_code() || hello::EN_ERR_MAINTENANCE == get_rsp_code()) {
        rsp_body->set_start_time(logic_config::me()->get_cfg_logic().server_open_time);
    } else {
        WLOGERROR("session [0x%llx, 0x%llx] login failed", static_cast<unsigned long long>(get_gateway_info().first),
                  static_cast<unsigned long long>(get_gateway_info().second));
    }

    send_rsp_msg();

    // 无情地踢下线
    session_manager::me()->remove(s, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    return get_ret_code();
}

int32_t task_action_login_authorization::check_proto_update(uint32_t ver_no) {
    // TODO check if client version is available
    return hello::err::EN_SUCCESS;
}

task_action_login_authorization::auth_fn_t task_action_login_authorization::get_verify_fn(uint32_t account_type) {
    static auth_fn_t all_auth_fns[hello::EnAccountTypeID_ARRAYSIZE];

    if (NULL != all_auth_fns[hello::EN_ATI_ACCOUNT_INNER]) {
        return all_auth_fns[account_type % hello::EnAccountTypeID_ARRAYSIZE];
    }

    all_auth_fns[hello::EN_ATI_ACCOUNT_INNER] = &task_action_login_authorization::verify_plat_account;
    return all_auth_fns[account_type % hello::EnAccountTypeID_ARRAYSIZE];
}

void task_action_login_authorization::init_login_data(hello::table_login &tb, const ::hello::CSLoginAuthReq &req, int64_t player_uid, uint32_t channel_id) {
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

std::string task_action_login_authorization::make_openid(const hello::CSLoginAuthReq &req) {
    return rpc::auth::login::make_open_id(zone_id_, req.account().account_type(), req.account().channel_id(), req.open_id());
}

int task_action_login_authorization::verify_plat_account(const ::hello::CSLoginAuthReq &req) {
    hello::table_login tb;
    std::string        version;
    int                res = rpc::db::login::get(req.open_id().c_str(), zone_id_, tb, version);
    if (hello::err::EN_DB_RECORD_NOT_FOUND != res && res < 0) {
        WLOGERROR("call login rpc method failed, msg: %s", req.DebugString().c_str());
        return hello::EN_ERR_SYSTEM;
    }

    if (hello::err::EN_DB_RECORD_NOT_FOUND == res) {
        return hello::EN_SUCCESS;
    }

    // 校验密码
    if (!req.has_account()) {
        if (tb.account().access().empty()) {
            return hello::EN_SUCCESS;
        }

        // 参数错误
        return hello::EN_ERR_INVALID_PARAM;
    }

    if (req.account().access() != tb.account().access()) {
        // 平台校验不通过错误码
        return hello::EN_ERR_LOGIN_VERIFY;
    }

    return hello::EN_SUCCESS;
}