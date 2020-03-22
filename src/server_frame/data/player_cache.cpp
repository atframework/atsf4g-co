#include <log/log_wrapper.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <logic/player_manager.h>

#include "player_cache.h"
#include "session.h"


player_cache::player_cache(fake_constructor &) : user_id_(0), zone_id_(0), data_version_(0) {
}

player_cache::~player_cache() { WPLOGDEBUG(*this, "destroyed"); }

bool player_cache::can_be_writable() const {
    // player cache always can not be writable
    return false;
}

bool player_cache::is_writable() const {
    return false;
}

void player_cache::init(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
    user_id_      = user_id;
    zone_id_      = zone_id;
    openid_id_    = openid;
    data_version_ = 0;

    // all manager init
    // ptr_t self = shared_from_this();
}

player_cache::ptr_t player_cache::create(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
    fake_constructor ctorp;
    ptr_t            ret = std::make_shared<player_cache>(ctorp);
    if (ret) {
        ret->init(user_id, zone_id, openid);
    }

    return ret;
}

void player_cache::create_init(uint32_t version_type) {
    data_version_ = 0;
    version_.assign("0");

    // copy account information
    protobuf_copy_message(get_account_info(), get_login_info().account());
    login_info_.set_zone_id(get_zone_id());
}

void player_cache::login_init() {
    // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

    // refresh account parameters，这里只刷新部分数据
    {
        hello::account_information &account = get_account_info();
        account.set_access(get_login_info().account().access());
        account.set_account_type(get_login_info().account().account_type());
        account.set_version_type(get_login_info().account().version_type());

        // 冗余的key字段
        account.mutable_profile()->set_open_id(get_open_id());
        account.mutable_profile()->set_user_id(get_user_id());
    }

    login_info_.set_zone_id(get_zone_id());
}

void player_cache::refresh_feature_limit() {
    // refresh daily limit
}

bool player_cache::gm_init() { return true; }

bool player_cache::is_gm() const { return get_account_info().version_type() == hello::EN_VERSION_GM; }

void player_cache::on_login() {}

void player_cache::on_logout() {}

void player_cache::on_remove() {}

void player_cache::init_from_table_data(const hello::table_user &tb_player) {
    data_version_ = tb_player.data_version();

    const hello::table_user *src_tb = &tb_player;
    if (src_tb->has_account()) {
        protobuf_copy_message(account_info_.ref(), src_tb->account());
    }

    if (src_tb->has_player()) {
        protobuf_copy_message(player_data_.ref(), src_tb->player());
    }

    if (src_tb->has_options()) {
        protobuf_copy_message(player_options_.ref(), src_tb->options());
    }
}

int player_cache::dump(hello::table_user &user, bool always) {
    user.set_open_id(get_open_id());
    user.set_user_id(get_user_id());
    user.set_zone_id(get_zone_id());
    user.set_data_version(data_version_);

    if (always || player_data_.is_dirty()) {
        protobuf_copy_message(*user.mutable_player(), player_data_.ref());
    }

    if (always || account_info_.is_dirty()) {
        protobuf_copy_message(*user.mutable_account(), account_info_.ref());
    }

    if (always || player_options_.is_dirty()) {
        protobuf_copy_message(*user.mutable_options(), player_options_.ref());
    }

    return 0;
}

void player_cache::send_all_syn_msg() {}

void player_cache::set_session(std::shared_ptr<session> session_ptr) {
    std::shared_ptr<session> old_sess = session_.lock();
    if (old_sess == session_ptr) {
        return;
    }

    session_ = session_ptr;

    // 如果为置空Session，则要加入登出缓存排队列表
    if (!session_ptr) {
        // 移除Session时触发Logout
        if (old_sess) {
            on_logout();
        }
    }
}

std::shared_ptr<session> player_cache::get_session() { return session_.lock(); }

bool player_cache::has_session() const { return false == session_.expired(); }

void player_cache::load_and_move_login_info(hello::table_login COPP_MACRO_RV_REF lg, const std::string& ver) {
    login_info_.Swap(&lg);
    login_info_version_ = ver;

    login_info_.set_zone_id(get_zone_id());
}
