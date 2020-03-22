#include <log/log_wrapper.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <logic/player_manager.h>

#include <logic/action/task_action_player_remote_patch_jobs.h>

#include <data/session.h>
#include "player.h"


player::player(fake_constructor & ctor) : base_type(ctor) {
    heartbeat_data_.continue_error_times = 0;
    heartbeat_data_.last_recv_time       = 0;
    heartbeat_data_.sum_error_times      = 0;
}

player::~player() { }

bool player::can_be_writable() const {
    // this player type can be writable
    return true;
}

bool player::is_writable() const {
    // this player type can be writable
    return can_be_writable() && is_inited();
}

void player::init(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
    base_type::init(user_id, zone_id, openid);

    // all manager init
    // ptr_t self = shared_from_this();
}

player::ptr_t player::create(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
    fake_constructor ctorp;
    ptr_t            ret = std::make_shared<player>(ctorp);
    if (ret) {
        ret->init(user_id, zone_id, openid);
    }

    return ret;
}

void player::create_init(uint32_t version_type) {
    base_type::create_init(version_type);

    set_data_version(PLAYER_DATA_LOGIC_VERSION);

    // TODO all module create init
    // TODO init all interval checkpoint

    // TODO init items
    // if (hello::EN_VERSION_GM != version_type) {
    //     excel::player_init_items::me()->foreach ([this](const excel::player_init_items::value_type &v) {
    //         if (0 != v->id()) {
    //             add_entity(v->id(), v->number(), hello::EN_ICMT_INIT, hello::EN_ICST_DEFAULT);
    //         }
    //     });
    // }
}

void player::login_init() {
    base_type::login_init();

    // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

    // TODO check all interval checkpoint

    // TODO all module login init

    set_inited();
    on_login();
}

void player::refresh_feature_limit() {
    base_type::refresh_feature_limit();
    // refresh daily limit
}

void player::on_login() {
    base_type::on_login();

    // TODO sync messages
}

void player::on_logout() {
    base_type::on_logout();
}

void player::on_remove() {

    // at last call base on remove callback
    base_type::on_remove();
}


void player::init_from_table_data(const hello::table_user &tb_player) {
    base_type::init_from_table_data(tb_player);

    // TODO data patch, 这里用于版本升级时可能需要升级玩家数据库，做版本迁移
    // hello::table_user tb_patch;
    // const hello::table_user *src_tb = &tb_player;
    // if (data_version_ < PLAYER_DATA_LOGIC_VERSION) {
    //     protobuf_copy_message(tb_patch, tb_player);
    //     src_tb = &tb_patch;
    //     //GameUserPatchMgr::Instance()->Patch(tb_patch, m_iDataVersion, GAME_USER_DATA_LOGIC);
    //     data_version_ = PLAYER_DATA_LOGIC_VERSION;
    // }
    // TODO all modules load from DB
}

int player::dump(hello::table_user &user, bool always) {
    int ret = base_type::dump(user, always);
    if (ret < 0) {
        return ret;
    }

    // TODO all modules dump to DB

    return ret;
}

void player::update_heartbeat() {
    const logic_config::LC_LOGIC &logic_cfg           = logic_config::me()->get_cfg_logic();
    time_t                        heartbeat_interval  = logic_cfg.heartbeat_interval;
    time_t                        heartbeat_tolerance = logic_cfg.heartbeat_tolerance;
    time_t                        tol_dura            = heartbeat_interval - heartbeat_tolerance;
    time_t                        now_time            = util::time::time_utility::get_now();

    // 小于容忍值得要统计错误次数
    if (now_time - heartbeat_data_.last_recv_time < tol_dura) {
        ++heartbeat_data_.continue_error_times;
        ++heartbeat_data_.sum_error_times;
    } else {
        heartbeat_data_.continue_error_times = 0;
    }

    heartbeat_data_.last_recv_time = now_time;

    // 顺带更新login_code的有效期
    get_login_info().set_login_code_expired(now_time + logic_cfg.session_login_code_valid_sec);
}

void player::start_patch_remote_command() {
    inner_flags_.set(inner_flag::EN_IFT_NEED_PATCH_REMOTE_COMMAND, true);

    try_patch_remote_command();
}

void player::try_patch_remote_command() {
    if (remote_command_patch_task_ && remote_command_patch_task_->is_exiting()) {
        remote_command_patch_task_.reset();
    }

    if (!inner_flags_.test(inner_flag::EN_IFT_NEED_PATCH_REMOTE_COMMAND)) {
        return;
    }

    // 队列化，同时只能一个任务执行
    if (remote_command_patch_task_) {
        return;
    }

    inner_flags_.set(inner_flag::EN_IFT_NEED_PATCH_REMOTE_COMMAND, false);

    task_manager::id_t                                 tid = 0;
    task_action_player_remote_patch_jobs::ctor_param_t params;
    params.user              = shared_from_this();
    params.timeout_duration  = logic_config::me()->get_cfg_logic().task_nomsg_timeout;
    params.timeout_timepoint = util::time::time_utility::get_now() + params.timeout_duration;
    task_manager::me()->create_task_with_timeout<task_action_player_remote_patch_jobs>(tid, params.timeout_duration, COPP_MACRO_STD_MOVE(params));

    if (0 == tid) {
        WLOGERROR("create task_action_player_remote_patch_jobs failed");
    } else {
        remote_command_patch_task_ = task_manager::me()->get_task(tid);

        dispatcher_start_data_t start_data;
        start_data.private_data     = NULL;
        start_data.message.msg_addr = NULL;
        start_data.message.msg_type = 0;

        int res = task_manager::me()->start_task(tid, start_data);
        if (res < 0) {
            WLOGERROR("start task_action_player_remote_patch_jobs for player %s(%u:%llu) failed, res: %d", get_open_id().c_str(), get_zone_id(), get_user_id_llu(), res);
            remote_command_patch_task_.reset();
            return;
        }
    }
}

void player::send_all_syn_msg() {
    // TODO 升级通知
    // TODO 道具变更通知
    // TODO 任务/成就变更通知

    clear_dirty_cache();
}

void player::clear_dirty_cache() {
    // TODO 清理要推送的脏数据
}

player_cs_syn_msg_holder::player_cs_syn_msg_holder(player::ptr_t u) : owner_(u) {}
player_cs_syn_msg_holder::~player_cs_syn_msg_holder() {
    if (!owner_) {
        return;
    }

    std::shared_ptr<session> sess = owner_->get_session();
    if (!sess) {
        return;
    }

    if (msg_.has_body()) {
        sess->send_msg_to_client(msg_);
    }
}