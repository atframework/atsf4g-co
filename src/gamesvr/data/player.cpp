// Copyright 2021 atframework

#include "data/player.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <gsl/select-gsl.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <logic/async_jobs/user_async_jobs_manager.h>
#include <logic/player_manager.h>

#include <data/session.h>
#include <rpc/gamesvrclientservice/gamesvrclientservice.h>
#include <rpc/rpc_utils.h>

player::internal_flag_guard_t::internal_flag_guard_t()
    : flag_(internal_flag::EN_IFT_FEATURE_INVALID), owner_(nullptr) {}
player::internal_flag_guard_t::~internal_flag_guard_t() { reset(); }

void player::internal_flag_guard_t::setup(player &owner, internal_flag::type f) {
  if (f <= internal_flag::EN_IFT_FEATURE_INVALID || f >= internal_flag::EN_IFT_MAX) {
    return;
  }

  // 已被其他地方设置
  if (owner.internal_flags_.test(f)) {
    return;
  }

  reset();
  owner_ = &owner;
  flag_ = f;
  owner_->internal_flags_.set(flag_, true);
}

void player::internal_flag_guard_t::reset() {
  if (nullptr != owner_ && internal_flag::EN_IFT_FEATURE_INVALID != flag_) {
    owner_->internal_flags_.set(flag_, false);
  }

  owner_ = nullptr;
  flag_ = internal_flag::EN_IFT_FEATURE_INVALID;
}

player::task_queue_node::task_queue_node(const task_manager::task_ptr_t &t) : related_task(t), is_waiting(false) {}

player::task_queue_lock_guard::task_queue_lock_guard(player &user)
    : lock_target_(&user), is_exiting_(false), queue_iter_(user.task_lock_queue_.end()) {
  task_manager::task_t::ptr_t task = cotask::this_task::get<task_manager::task_t>();
  if (!task) {
    lock_target_ = nullptr;
    return;
  }

  if (task->is_exiting()) {
    lock_target_ = nullptr;
    is_exiting_ = true;
    return;
  }

  // 只要不为空，说明其他任务正在执行，需要切出等待
  bool need_yield = false == user.task_lock_queue_.empty();
  queue_iter_ = user.task_lock_queue_.insert(user.task_lock_queue_.end(), task_queue_node(task));
  if (queue_iter_ == user.task_lock_queue_.end()) {
    lock_target_ = nullptr;
    return;
  }

  if (need_yield) {
    queue_iter_->is_waiting = true;
    task->yield();
    queue_iter_->is_waiting = false;

    is_exiting_ = task->is_exiting();
  }
}

player::task_queue_lock_guard::~task_queue_lock_guard() {
  if (nullptr == lock_target_) {
    return;
  }

  if (queue_iter_ != lock_target_->task_lock_queue_.end()) {
    lock_target_->task_lock_queue_.erase(queue_iter_);
  }

  // 正常流程下 queue_iter_ == lock_target_->task_lock_queue_.begin()
  // 异常流程 queue_iter_ 会被提前释放，这时候 lock_target_->task_lock_queue_.begin().is_waiting ==
  // false

  // 激活下一个任务
  while (!lock_target_->task_lock_queue_.empty()) {
    if (!lock_target_->task_lock_queue_.front().related_task) {
      lock_target_->task_lock_queue_.pop_front();
      continue;
    }

    // 如果下一个任务在等待解锁，则直接解锁。否则可能下一个任务已经解锁（timeout或被killed）但在等待其他RPC
    if (lock_target_->task_lock_queue_.front().is_waiting) {
      lock_target_->task_lock_queue_.front().related_task->resume();
    }
    break;
  }
}

bool player::task_queue_lock_guard::is_exiting() const { return is_exiting_; }

player::player(fake_constructor &ctor) : base_type(ctor), user_async_jobs_manager_(new user_async_jobs_manager(*this)) {
  heartbeat_data_.continue_error_times = 0;
  heartbeat_data_.last_recv_time = 0;
  heartbeat_data_.sum_error_times = 0;

  cache_data_.refresh_feature_limit_second = 0;
  cache_data_.refresh_feature_limit_minute = 0;
  cache_data_.refresh_feature_limit_hour = 0;

  clear_dirty_cache();
}

player::~player() {}

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
  ptr_t ret = std::make_shared<player>(ctorp);
  if (ret) {
    ret->init(user_id, zone_id, openid);
  }

  return ret;
}

void player::create_init(uint32_t version_type) {
  base_type::create_init(version_type);

  set_data_version(PLAYER_DATA_LOGIC_VERSION);

  //! === manager implement === 创建后事件回调，这时候还没进入数据库并且未执行login_init()
  user_async_jobs_manager_->create_init(version_type);

  // TODO init all interval checkpoint

  // TODO init items
  // if (PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_VERSION_GM != version_type) {
  //     excel::player_init_items::me()->foreach ([this](const excel::player_init_items::value_type &v) {
  //         if (0 != v->id()) {
  //             add_entity(v->id(), v->number(), PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ICMT_INIT,
  //             PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ICST_DEFAULT);
  //         }
  //     });
  // }
}

void player::login_init() {
  base_type::login_init();

  // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

  //! === manager implement === 登入成功后事件回调，新用户也会触发

  // all module login init
  user_async_jobs_manager_->login_init();

  set_inited();
  on_login();
}

bool player::is_dirty() const {
  bool ret = base_type::is_dirty();

#define PLAYER_CHECK_RET_DIRTY(RET, EXPR) \
  if (RET) {                              \
    return RET;                           \
  }                                       \
  RET = EXPR

  //! === manager implement === 检查是否有脏数据
  PLAYER_CHECK_RET_DIRTY(ret, user_async_jobs_manager_->is_dirty());

#undef PLAYER_CHECK_RET_DIRTY

  return ret;
}

void player::clear_dirty() {
  //! === manager implement === 清理脏数据标记
  user_async_jobs_manager_->clear_dirty();
}

void player::refresh_feature_limit() {
  base_type::refresh_feature_limit();

  //! === manager implement === 不定期调用，用于刷新逻辑
  // all modules refresh limit
  user_async_jobs_manager_->refresh_feature_limit();

  time_t now = util::time::time_utility::get_now();
  if (now != cache_data_.refresh_feature_limit_second) {
    cache_data_.refresh_feature_limit_second = now;
    // 每秒仅需要执行一次的refresh_feature_limit
  }
  if (now >= cache_data_.refresh_feature_limit_minute + util::time::time_utility::MINITE_SECONDS ||
      now < cache_data_.refresh_feature_limit_minute) {
    cache_data_.refresh_feature_limit_minute = now - now % util::time::time_utility::MINITE_SECONDS;

    // 每分钟仅需要执行一次的refresh_feature_limit
  }
  if (now >= cache_data_.refresh_feature_limit_hour + util::time::time_utility::HOUR_SECONDS ||
      now < cache_data_.refresh_feature_limit_hour) {
    cache_data_.refresh_feature_limit_hour = now - now % util::time::time_utility::HOUR_SECONDS;

    // 每小时仅需要执行一次的refresh_feature_limit
  }
}

void player::on_login() {
  // Trigger by login_init()
  if (!is_inited()) {
    return;
  }

  base_type::on_login();

  // TODO sync messages
}

void player::on_logout() { base_type::on_logout(); }

void player::on_saved() {
  // at last call base on remove callback
  base_type::on_saved();
}

void player::on_update_session(const std::shared_ptr<session> &from, const std::shared_ptr<session> &to) {
  base_type::on_update_session(from, to);
}

void player::init_from_table_data(const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &tb_player) {
  base_type::init_from_table_data(tb_player);

  // TODO data patch, 这里用于版本升级时可能需要升级玩家数据库，做版本迁移
  // PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user tb_patch;
  // const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user *src_tb = &tb_player;
  // if (data_version_ < PLAYER_DATA_LOGIC_VERSION) {
  //     protobuf_copy_message(tb_patch, tb_player);
  //     src_tb = &tb_patch;
  //     //GameUserPatchMgr::Instance()->Patch(tb_patch, m_iDataVersion, GAME_USER_DATA_LOGIC);
  //     data_version_ = PLAYER_DATA_LOGIC_VERSION;
  // }

  //! === manager implement === 从数据库读取，注意本接口可能被调用多次，需要清理老数据
  if (tb_player.has_async_jobs()) {
    user_async_jobs_manager_->init_from_table_data(tb_player);
  }
}

int player::dump(PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user &user, bool always) {
  int ret = base_type::dump(user, always);
  if (ret < 0) {
    return ret;
  }

  //! === manager implement === 保存到数据库
  // all modules dump to DB
  ret = user_async_jobs_manager_->dump(user);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump async_jobs_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return ret;
  }

  return ret;
}

void player::update_heartbeat() {
  const auto &logic_cfg = logic_config::me()->get_logic();
  time_t heartbeat_interval = logic_cfg.heartbeat().interval().seconds();
  time_t heartbeat_tolerance = logic_cfg.heartbeat().tolerance().seconds();
  time_t tol_dura = heartbeat_interval - heartbeat_tolerance;
  time_t now_time = util::time::time_utility::get_now();

  // 小于容忍值得要统计错误次数
  if (now_time - heartbeat_data_.last_recv_time < tol_dura) {
    ++heartbeat_data_.continue_error_times;
    ++heartbeat_data_.sum_error_times;
  } else {
    heartbeat_data_.continue_error_times = 0;
  }

  heartbeat_data_.last_recv_time = now_time;

  // 顺带更新login_code的有效期
  get_login_info().set_login_code_expired(now_time + logic_cfg.session().login_code_valid_sec().seconds());
}

void player::send_all_syn_msg() {
  auto sess = get_session();
  if (sess) {
    internal_flag_guard_t flag_guard;
    flag_guard.setup(*this, internal_flag::EN_IFT_IN_DIRTY_CALLBACK);
    if (!flag_guard) {
      FWPLOGERROR(*this, "can not send sync messages when in dirty callback");
      return;
    }

    dirty_message_container dirty_msg;
    for (auto &handle : cache_data_.dirty_handles) {
      if (handle.second.build_fn) {
        handle.second.build_fn(*this, dirty_msg);
      }
    }

    if (dirty_msg.player_dirty) {
      rpc::context ctx;
      rpc::gamesvrclientservice::send_player_dirty_chg_sync(ctx, *dirty_msg.player_dirty, *sess);
    }
  }

  clear_dirty_cache();
}

int player::await_before_logout_tasks() {
  // 等待全部涉及保存的异步任务完成
  int ret = base_type::await_before_logout_tasks();
  if (ret < 0) {
    return ret;
  }

  ret = user_async_jobs_manager_->wait_for_async_task();
  if (ret < 0) {
    return ret;
  }

  return ret;
}

void player::clear_dirty_cache() {
  internal_flag_guard_t flag_guard;
  flag_guard.setup(*this, internal_flag::EN_IFT_IN_DIRTY_CALLBACK);
  if (!flag_guard) {
    FWPLOGERROR(*this, "can not clean dirty handles when in dirty callback");
    return;
  }

  // 清理要推送的脏数据
  for (auto &handle : cache_data_.dirty_handles) {
    if (handle.second.clear_fn) {
      handle.second.clear_fn(*this);
    }
  }
  cache_data_.dirty_handles.clear();
}

template <class TMSG, class TCONTAINER>
static player::dirty_sync_handle_t _player_generate_dirty_handle(
    TMSG *(PROJECT_SERVER_FRAME_NAMESPACE_ID::SCPlayerDirtyChgSync::*add_fn)(), TCONTAINER player::cache_t::*get_mem) {
  player::dirty_sync_handle_t handle;
  handle.build_fn = [add_fn, get_mem](player &user, player::dirty_message_container &output) {
    if (!get_mem) {
      return;
    }

    TCONTAINER &container = (user.get_cache_data().*get_mem);
    if (container.empty()) {
      return;
    }

    if (!output.player_dirty) {
      output.player_dirty = gsl::make_unique<PROJECT_SERVER_FRAME_NAMESPACE_ID::SCPlayerDirtyChgSync>();
    }
    if (!output.player_dirty) {
      FWLOGERROR("malloc dirty msg body failed");
      return;
    }

    for (auto &dirty_data : container) {
      auto copied_item = (output.player_dirty.get()->*add_fn)();
      if (nullptr == copied_item) {
        FWLOGERROR("SCPlayerDirtyChgSync add item failed");
        return;
      }
      protobuf_copy_message(*copied_item, dirty_data.second);
    }
  };

  handle.clear_fn = [get_mem](player &user) {
    if (get_mem) {
      (user.get_cache_data().*get_mem).clear();
    }
  };

  return handle;
}

PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem &player::mutable_dirty_item(
    const PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem &in) {
  insert_dirty_handle_if_not_exists(reinterpret_cast<uintptr_t>(&cache_data_.dirty_item_by_type), [](player &) {
    return _player_generate_dirty_handle(&PROJECT_SERVER_FRAME_NAMESPACE_ID::SCPlayerDirtyChgSync::add_dirty_items,
                                         &player::cache_t::dirty_item_by_type);
  });

  PROJECT_SERVER_FRAME_NAMESPACE_ID::DItem &ret = cache_data_.dirty_item_by_type[in.type_id()];
  ret = in;
  return ret;
}

void player::insert_dirty_handle_if_not_exists(uintptr_t key, dirty_sync_handle_t (*create_handle_fn)(player &)) {
  if (!create_handle_fn) {
    return;
  }

  if (internal_flags_.test(internal_flag::EN_IFT_IN_DIRTY_CALLBACK)) {
    FWPLOGERROR(*this, "can not insert dirty handle when in dirty callback");
    return;
  }

  if (cache_data_.dirty_handles.end() != cache_data_.dirty_handles.find(key)) {
    return;
  }

  cache_data_.dirty_handles[key] = create_handle_fn(*this);
}

void player::insert_dirty_handle_if_not_exists(uintptr_t key, build_dirty_message_fn_t build_fn,
                                               clear_dirty_cache_fn_t clear_fn) {
  if (!build_fn && !clear_fn) {
    return;
  }

  if (internal_flags_.test(internal_flag::EN_IFT_IN_DIRTY_CALLBACK)) {
    FWPLOGERROR(*this, "can not insert dirty handle when in dirty callback");
    return;
  }

  if (cache_data_.dirty_handles.end() != cache_data_.dirty_handles.find(key)) {
    return;
  }

  dirty_sync_handle_t &handle = cache_data_.dirty_handles[key];
  handle.build_fn = build_fn;
  handle.clear_fn = clear_fn;
}
