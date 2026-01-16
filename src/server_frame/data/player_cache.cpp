// Copyright 2021 atframework

#include "data/player_cache.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <dispatcher/task_manager.h>
#include <logic/player_manager.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>

#include <memory>
#include <string>
#include <utility>

#include "data/session.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"

SERVER_FRAME_API initialization_task_lock_guard::~initialization_task_lock_guard() {
  if (!guard_) {
    return;
  }

  guard_->initialization_task_id_ = 0;
}

SERVER_FRAME_API initialization_task_lock_guard::initialization_task_lock_guard(
    std::shared_ptr<player_cache> user, task_type_trait::id_type task_id) noexcept
    : guard_(user) {
  if (!guard_) {
    return;
  }

  if (0 != guard_->initialization_task_id_) {
    guard_.reset();
    return;
  }

  guard_->initialization_task_id_ = task_id;
}

SERVER_FRAME_API initialization_task_lock_guard::initialization_task_lock_guard(
    initialization_task_lock_guard &&other) noexcept
    : guard_(std::move(other.guard_)) {
  other.guard_.reset();
}

SERVER_FRAME_API initialization_task_lock_guard &initialization_task_lock_guard::operator=(
    initialization_task_lock_guard &&other) noexcept {
  guard_.swap(other.guard_);
  other.guard_.reset();

  return *this;
}

SERVER_FRAME_API bool initialization_task_lock_guard::has_value() const noexcept { return !!guard_; }

SERVER_FRAME_API player_cache::player_cache(fake_constructor &)
    : user_id_(0),
      zone_id_(0),
      login_lock_version_(0),
      user_cas_version_(0),
      create_init_(false),
      initialization_task_id_(0),
      data_version_(0) {
  server_sequence_ =
      static_cast<uint64_t>(
          (util::time::time_utility::get_sys_now() - PROJECT_NAMESPACE_ID::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET)
          << 10) +
      static_cast<uint64_t>(util::time::time_utility::get_now_usec() / 1000);
}

SERVER_FRAME_API player_cache::~player_cache() {
  FWPLOGDEBUG(*this, "destroyed {}", reinterpret_cast<const void *>(this));
}

SERVER_FRAME_API bool player_cache::can_be_writable() const {
  // player cache always can not be writable
  return false;
}

SERVER_FRAME_API bool player_cache::is_writable() const { return false; }

SERVER_FRAME_API void player_cache::init(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  user_id_ = user_id;
  zone_id_ = zone_id;
  openid_id_ = openid;
  data_version_ = 0;

  // all manager init
  // ptr_t self = shared_from_this();
}

SERVER_FRAME_API player_cache::ptr_t player_cache::create(uint64_t user_id, uint32_t zone_id,
                                                          const std::string &openid) {
  fake_constructor ctorp;
  ptr_t ret = atfw::memory::stl::make_shared<player_cache>(ctorp);
  if (ret) {
    ret->init(user_id, zone_id, openid);
  }

  return ret;
}

SERVER_FRAME_API void player_cache::create_init(rpc::context &) {
  data_version_ = 0;
  create_init_ = true;
}

SERVER_FRAME_API void player_cache::login_init(rpc::context &) {}

SERVER_FRAME_API bool player_cache::is_dirty() const {
  //! === manager implement === 检查是否有脏数据
  bool ret = false;
  ret = ret || account_info_.is_dirty();
  ret = ret || player_data_.is_dirty();
  ret = ret || login_info_.is_dirty();
  return ret;
}

SERVER_FRAME_API void player_cache::clear_dirty() {
  //! === manager implement === 清理脏数据标记
  account_info_.clear_dirty();
  player_data_.clear_dirty();
  login_info_.clear_dirty();
}

SERVER_FRAME_API void player_cache::refresh_feature_limit(rpc::context &) {
  // refresh daily limit
}

SERVER_FRAME_API bool player_cache::gm_init() { return true; }

SERVER_FRAME_API bool player_cache::is_gm() const {
  return get_account_info().version_type() == PROJECT_NAMESPACE_ID::EN_VERSION_GM;
}

SERVER_FRAME_API void player_cache::on_login(rpc::context &) {
  // 更新 login_info_ 数据
  if (is_new_user()) {
    // 新用户，设置注册时间
    login_info_.ref().set_business_register_time(static_cast<uint32_t>(util::time::time_utility::get_sys_now()));
  }
  login_info_.ref().set_business_login_time(static_cast<uint32_t>(util::time::time_utility::get_sys_now()));
  login_info_.ref().set_stat_login_total_times(login_info_.ref().stat_login_total_times() + 1);
  login_info_.ref().set_stat_login_success_times(login_info_.ref().stat_login_success_times() + 1);
}

SERVER_FRAME_API void player_cache::on_logout(rpc::context &) {
  login_info_.ref().set_business_logout_time(static_cast<uint32_t>(util::time::time_utility::get_sys_now()));
}

SERVER_FRAME_API void player_cache::on_saved(rpc::context &) {}

SERVER_FRAME_API void player_cache::on_update_session(rpc::context &, const std::shared_ptr<session> &,
                                                      const std::shared_ptr<session> &) {}

SERVER_FRAME_API bool player_cache::is_new_user() const { return login_info_.ref().business_login_time() == 0; }

SERVER_FRAME_API void player_cache::init_from_table_data(rpc::context &,
                                                         const PROJECT_NAMESPACE_ID::table_user &tb_player) {
  create_init_ = tb_player.create_init();
  const PROJECT_NAMESPACE_ID::table_user *src_tb = &tb_player;
  if (src_tb->has_account_data()) {
    protobuf_copy_message(account_info_.ref(), src_tb->account_data());
  }

  if (src_tb->has_user_data()) {
    protobuf_copy_message(player_data_.ref(), src_tb->user_data());
    if (player_data_.ref().session_sequence() > server_sequence_) {
      server_sequence_ = player_data_.ref().session_sequence();
    }
  }

  if (src_tb->has_login_data()) {
    protobuf_copy_message(login_info_.ref(), src_tb->login_data());
  }

  data_version_ = tb_player.data_version();
}

SERVER_FRAME_API int player_cache::dump(rpc::context &, PROJECT_NAMESPACE_ID::table_user &user, bool always) {
  user.set_open_id(get_open_id());
  user.set_user_id(get_user_id());
  user.set_zone_id(get_zone_id());

  user.set_create_init(create_init_);
  user.set_data_version(data_version_);

  if (always || player_data_.is_dirty()) {
    protobuf_copy_message(*user.mutable_user_data(), player_data_.ref());
  }

  if (always || account_info_.is_dirty()) {
    protobuf_copy_message(*user.mutable_account_data(), account_info_.ref());
  }

  if (always || login_info_.is_dirty()) {
    protobuf_copy_message(*user.mutable_login_data(), login_info_.ref());
  }

  return 0;
}

SERVER_FRAME_API void player_cache::send_all_syn_msg(rpc::context &) {}

SERVER_FRAME_API rpc::result_code_type player_cache::await_before_logout_tasks(rpc::context &) { RPC_RETURN_CODE(0); }

SERVER_FRAME_API int32_t
player_cache::client_rpc_filter(rpc::context & /*ctx*/, task_action_cs_req_base & /*cs_task_action*/,
                                const atframework::DispatcherOptions * /*dispatcher_options*/) {
  return 0;
}

SERVER_FRAME_API void player_cache::set_session(rpc::context &ctx, std::shared_ptr<session> session_ptr) {
  std::shared_ptr<session> old_sess = session_.lock();
  if (old_sess == session_ptr) {
    return;
  }

  session_ = session_ptr;
  on_update_session(ctx, old_sess, session_ptr);
}

SERVER_FRAME_API std::shared_ptr<session> player_cache::get_session() { return session_.lock(); }

SERVER_FRAME_API bool player_cache::has_session() const { return false == session_.expired(); }

SERVER_FRAME_API void player_cache::load_and_move_login_lock(PROJECT_NAMESPACE_ID::table_login_lock &&lg,
                                                             uint64_t ver) {
  login_lock_.Swap(&lg);
  login_lock_version_ = ver;
}

SERVER_FRAME_API uint64_t player_cache::alloc_server_sequence() {
  uint64_t ret = ++server_sequence_;
  player_data_.ref().set_session_sequence(ret);
  return ret;
}

SERVER_FRAME_API void player_cache::set_quick_save() const {
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

SERVER_FRAME_API bool player_cache::has_initialization_task_id() const noexcept { return 0 != initialization_task_id_; }

SERVER_FRAME_API rpc::result_code_type player_cache::await_initialization_task(rpc::context &ctx) {
  task_type_trait::task_type t = task_manager::me()->get_task(initialization_task_id_);
  if (task_type_trait::empty(t)) {
    initialization_task_id_ = 0;
    RPC_RETURN_CODE(0);
  }

  if (task_type_trait::is_exiting(t)) {
    initialization_task_id_ = 0;
    RPC_RETURN_CODE(0);
  }

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, t));
  if (task_type_trait::is_exiting(t)) {
    initialization_task_id_ = 0;
  }
  RPC_RETURN_CODE(ret);
}
