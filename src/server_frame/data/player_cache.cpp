// Copyright 2021 atframework

#include "data/player_cache.h"

#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <logic/player_manager.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>

#include <string>

#include "data/session.h"

player_cache::player_cache(fake_constructor &) : user_id_(0), zone_id_(0), data_version_(0) {
  server_sequence_ =
      static_cast<uint64_t>(
          (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
          << 10) +
      static_cast<uint64_t>(util::time::time_utility::get_now_usec() / 1000);
}

player_cache::~player_cache() { FWPLOGDEBUG(*this, "destroyed {}", reinterpret_cast<const void *>(this)); }

bool player_cache::can_be_writable() const {
  // player cache always can not be writable
  return false;
}

bool player_cache::is_writable() const { return false; }

void player_cache::init(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  user_id_ = user_id;
  zone_id_ = zone_id;
  openid_id_ = openid;
  data_version_ = 0;

  // all manager init
  // ptr_t self = shared_from_this();
}

player_cache::ptr_t player_cache::create(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  fake_constructor ctorp;
  ptr_t ret = std::make_shared<player_cache>(ctorp);
  if (ret) {
    ret->init(user_id, zone_id, openid);
  }

  login_info_.mutable_blob_data()->set_business_register_time(util::time::time_utility::get_now());

  return ret;
}

rpc::result_code_type player_cache::create_init(rpc::context &, uint32_t /*version_type*/) {
  data_version_ = 0;
  version_.assign("0");

  // copy account information
  protobuf_copy_message(get_account_info(), get_login_info().account());
  login_info_.set_zone_id(get_zone_id());

  RPC_RETURN_CODE(0);
}

rpc::result_code_type player_cache::login_init(rpc::context &) {
  // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

  // refresh account parameters，这里只刷新部分数据
  {
    PROJECT_NAMESPACE_ID::account_information &account = get_account_info();
    account.set_access(get_login_info().account().access());
    account.set_account_type(get_login_info().account().account_type());
    account.set_version_type(get_login_info().account().version_type());

    // 冗余的key字段
    account.mutable_profile()->set_open_id(get_open_id());
    account.mutable_profile()->set_user_id(get_user_id());
  }

  login_info_.set_zone_id(get_zone_id());

  RPC_RETURN_CODE(0);
}

bool player_cache::is_dirty() const {
  //! === manager implement === 检查是否有脏数据
  bool ret = false;
  ret = ret || account_info_.is_dirty();
  ret = ret || player_data_.is_dirty();
  ret = ret || player_options_.is_dirty();
  return ret;
}

void player_cache::clear_dirty() {
  //! === manager implement === 清理脏数据标记
  account_info_.clear_dirty();
  player_data_.clear_dirty();
  player_options_.clear_dirty();
}

void player_cache::refresh_feature_limit(rpc::context &) {
  // refresh daily limit
}

bool player_cache::gm_init() { return true; }

bool player_cache::is_gm() const { return get_account_info().version_type() == PROJECT_NAMESPACE_ID::EN_VERSION_GM; }

void player_cache::on_login(rpc::context &) {}

void player_cache::on_logout(rpc::context &) {}

void player_cache::on_saved(rpc::context &) {}

void player_cache::on_update_session(rpc::context &, const std::shared_ptr<session> &,
                                     const std::shared_ptr<session> &) {}

void player_cache::init_from_table_data(rpc::context &, const PROJECT_NAMESPACE_ID::table_user &tb_player) {
  data_version_ = tb_player.data_version();

  const PROJECT_NAMESPACE_ID::table_user *src_tb = &tb_player;
  if (src_tb->has_account()) {
    protobuf_copy_message(account_info_.ref(), src_tb->account());
  }

  if (src_tb->has_player()) {
    protobuf_copy_message(player_data_.ref(), src_tb->player());
    if (player_data_.ref().session_sequence() > server_sequence_) {
      server_sequence_ = player_data_.ref().session_sequence();
    }
  }

  if (src_tb->has_options()) {
    protobuf_copy_message(player_options_.ref(), src_tb->options());
  }
}

int player_cache::dump(rpc::context &, PROJECT_NAMESPACE_ID::table_user &user, bool always) {
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

void player_cache::send_all_syn_msg(rpc::context &) {}

rpc::result_code_type player_cache::await_before_logout_tasks(rpc::context &) { RPC_RETURN_CODE(0); }

void player_cache::set_session(rpc::context &ctx, std::shared_ptr<session> session_ptr) {
  std::shared_ptr<session> old_sess = session_.lock();
  if (old_sess == session_ptr) {
    return;
  }

  session_ = session_ptr;
  on_update_session(ctx, old_sess, session_ptr);

  // 如果为置空Session，则要加入登出缓存排队列表
  if (!session_ptr) {
    // 移除Session时触发Logout
    if (old_sess && is_writable()) {
      on_logout(ctx);
    }
  } else if (is_writable()) {
    on_login(ctx);
  }
}

std::shared_ptr<session> player_cache::get_session() { return session_.lock(); }

bool player_cache::has_session() const { return false == session_.expired(); }

void player_cache::load_and_move_login_info(PROJECT_NAMESPACE_ID::table_login &&lg, const std::string &ver) {
  login_info_.Swap(&lg);
  login_info_version_ = ver;

  login_info_.set_zone_id(get_zone_id());
}

uint64_t player_cache::alloc_server_sequence() {
  uint64_t ret = ++server_sequence_;
  player_data_.ref().set_session_sequence(ret);
  return ret;
}

void player_cache::set_quick_save() const {
  router_manager_base *mgr = router_manager_set::me()->get_manager(PROJECT_NAMESPACE_ID::EN_ROT_PLAYER);
  if (nullptr == mgr) {
    return;
  }

  router_manager_base::key_t key(PROJECT_NAMESPACE_ID::EN_ROT_PLAYER, get_zone_id(), get_user_id());
  std::shared_ptr<router_object_base> obj = mgr->get_base_cache(key);
  if (!obj || !obj->is_writable()) {
    return;
  }

  router_manager_set::me()->mark_fast_save(mgr, obj);
}
