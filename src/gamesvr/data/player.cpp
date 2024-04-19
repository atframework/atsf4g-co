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

rpc::result_code_type player::create_init(rpc::context &parent_ctx, uint32_t version_type) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.create_init", std::move(trace_start_option));

  auto res = RPC_AWAIT_CODE_RESULT(base_type::create_init(ctx, version_type));
  if (res < 0) {
    RPC_RETURN_CODE(trace.finish({res, {}}));
  }

  set_data_version(PLAYER_DATA_LOGIC_VERSION);

  //! === manager implement === 创建后事件回调，这时候还没进入数据库并且未执行login_init()
  res = RPC_AWAIT_CODE_RESULT(user_async_jobs_manager_->create_init(ctx, version_type));
  if (res < 0) {
    RPC_RETURN_CODE(trace.finish({res, {}}));
  }

  // TODO init all interval checkpoint

  // TODO init items
  // if (PROJECT_NAMESPACE_ID::EN_VERSION_GM != version_type) {
  //     excel::player_init_items::me()->foreach ([this](const excel::player_init_items::value_type &v) {
  //         if (0 != v->id()) {
  //             add_entity(v->id(), v->number(), PROJECT_NAMESPACE_ID::EN_ICMT_INIT,
  //             PROJECT_NAMESPACE_ID::EN_ICST_DEFAULT);
  //         }
  //     });
  // }

  RPC_RETURN_CODE(trace.finish({res, {}}));
}

rpc::result_code_type player::login_init(rpc::context &parent_ctx) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.login_init", std::move(trace_start_option));

  auto res = RPC_AWAIT_CODE_RESULT(base_type::login_init(ctx));
  if (res < 0) {
    RPC_RETURN_CODE(trace.finish({res, {}}));
  }

  // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

  //! === manager implement === 登入成功后事件回调，新用户也会触发

  // all module login init
  res = RPC_AWAIT_CODE_RESULT(user_async_jobs_manager_->login_init(ctx));
  if (res < 0) {
    RPC_RETURN_CODE(trace.finish({res, {}}));
  }

  set_inited();
  on_login(ctx);

  RPC_RETURN_CODE(trace.finish({res, {}}));
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

void player::refresh_feature_limit(rpc::context &ctx) {
  base_type::refresh_feature_limit(ctx);

  //! === manager implement === 不定期调用，用于刷新逻辑
  // all modules refresh limit
  user_async_jobs_manager_->refresh_feature_limit(ctx);

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

void player::on_login(rpc::context &parent_ctx) {
  // Trigger by login_init()
  if (!is_inited()) {
    return;
  }

  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.on_login", std::move(trace_start_option));

  base_type::on_login(ctx);

  // TODO sync messages

  trace.finish({0, {}});
}

void player::on_logout(rpc::context &parent_ctx) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.on_logout", std::move(trace_start_option));

  base_type::on_logout(ctx);

  trace.finish({0, {}});
}

void player::on_saved(rpc::context &ctx) {
  // at last call base on remove callback
  base_type::on_saved(ctx);
}

void player::on_update_session(rpc::context &ctx, const std::shared_ptr<session> &from,
                               const std::shared_ptr<session> &to) {
  base_type::on_update_session(ctx, from, to);
}

void player::init_from_table_data(rpc::context &parent_ctx, const PROJECT_NAMESPACE_ID::table_user &tb_player) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.init_from_table_data", std::move(trace_start_option));

  base_type::init_from_table_data(ctx, tb_player);

  // TODO data patch, 这里用于版本升级时可能需要升级玩家数据库，做版本迁移
  // PROJECT_NAMESPACE_ID::table_user tb_patch;
  // const PROJECT_NAMESPACE_ID::table_user *src_tb = &tb_player;
  // if (data_version_ < PLAYER_DATA_LOGIC_VERSION) {
  //     protobuf_copy_message(tb_patch, tb_player);
  //     src_tb = &tb_patch;
  //     //GameUserPatchMgr::Instance()->Patch(tb_patch, m_iDataVersion, GAME_USER_DATA_LOGIC);
  //     data_version_ = PLAYER_DATA_LOGIC_VERSION;
  // }

  //! === manager implement === 从数据库读取，注意本接口可能被调用多次，需要清理老数据
  if (tb_player.has_async_job_blob_data()) {
    user_async_jobs_manager_->init_from_table_data(ctx, tb_player);
  }

  trace.finish({0, {}});
}

int player::dump(rpc::context &parent_ctx, PROJECT_NAMESPACE_ID::table_user &user, bool always) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::context::tracer trace;
  rpc::context::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "player.dump", std::move(trace_start_option));

  int ret = base_type::dump(ctx, user, always);
  if (ret < 0) {
    return trace.finish({ret, {}});
  }

  //! === manager implement === 保存到数据库
  // all modules dump to DB
  ret = user_async_jobs_manager_->dump(ctx, user);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump async_jobs_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return trace.finish({ret, {}});
  }

  return trace.finish({ret, {}});
}

void player::update_heartbeat() {
  const auto &logic_cfg = logic_config::me()->get_logic();
  time_t heartbeat_interval = logic_cfg.heartbeat().interval().seconds();
  time_t heartbeat_tolerance = logic_cfg.heartbeat().tolerance().seconds();
  time_t tol_dura = heartbeat_interval - heartbeat_tolerance;
  time_t now_time = util::time::time_utility::get_sys_now();

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

void player::send_all_syn_msg(rpc::context &ctx) {
  if (internal_flags_.test(internal_flag::EN_IFT_IN_DIRTY_CALLBACK)) {
    FWPLOGERROR(*this, "can not send sync messages when when running dirty handle {}",
                cache_data_.current_dirty_handle_name);
    return;
  }

  auto sess = get_session();
  if (sess) {
    dirty_message_container dirty_msg;
    {
      internal_flag_guard_t flag_guard;
      flag_guard.setup(*this, internal_flag::EN_IFT_IN_DIRTY_CALLBACK);
      if (!flag_guard) {
        return;
      }

      for (auto &handle : cache_data_.dirty_handles) {
        if (handle.second.build_fn) {
          cache_data_.current_dirty_handle_name = handle.second.name;
          handle.second.build_fn(*this, dirty_msg);
        }
      }
      cache_data_.current_dirty_handle_name = gsl::string_view{};
    }

    if (dirty_msg.player_dirty) {
      rpc::gamesvrclientservice::send_player_dirty_chg_sync(ctx, *dirty_msg.player_dirty, *sess);
    }
  }

  clear_dirty_cache();
}

rpc::result_code_type player::await_before_logout_tasks(rpc::context &ctx) {
  // 等待全部涉及保存的异步任务完成
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::await_before_logout_tasks(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(user_async_jobs_manager_->wait_for_async_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}

void player::clear_dirty_cache() {
  {
    internal_flag_guard_t flag_guard;
    flag_guard.setup(*this, internal_flag::EN_IFT_IN_DIRTY_CALLBACK);
    if (!flag_guard) {
      FWPLOGERROR(*this, "can not clear dirty handles when running dirty handle {}",
                  cache_data_.current_dirty_handle_name);
      return;
    }

    // 清理要推送的脏数据
    for (auto &handle : cache_data_.dirty_handles) {
      if (handle.second.clear_fn) {
        cache_data_.current_dirty_handle_name = handle.second.name;
        handle.second.clear_fn(*this);
      }
    }
    cache_data_.current_dirty_handle_name = gsl::string_view{};
    cache_data_.dirty_handles.clear();
  }

  // Other clear actions
}

template <class TMSG, class TCONTAINER>
static player::dirty_sync_handle_t _player_generate_dirty_handle(
    gsl::string_view /*handle_name*/, TMSG *(PROJECT_NAMESPACE_ID::SCPlayerDirtyChgSync::*add_fn)(),
    TCONTAINER player::cache_t::*get_mem) {
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
      output.player_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCPlayerDirtyChgSync>();
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

PROJECT_NAMESPACE_ID::DItem &player::mutable_dirty_item(const PROJECT_NAMESPACE_ID::DItem &in) {
  insert_dirty_handle_if_not_exists(reinterpret_cast<uintptr_t>(&cache_data_.dirty_item_by_type),
                                    "player.mutable_dirty_item", [](gsl::string_view handle_name, player &) {
                                      return _player_generate_dirty_handle(
                                          handle_name, &PROJECT_NAMESPACE_ID::SCPlayerDirtyChgSync::add_dirty_items,
                                          &player::cache_t::dirty_item_by_type);
                                    });

  PROJECT_NAMESPACE_ID::DItem &ret = cache_data_.dirty_item_by_type[static_cast<int32_t>(in.type_id())];
  ret = in;
  return ret;
}

void player::insert_dirty_handle_if_not_exists(uintptr_t key, gsl::string_view handle_name,
                                               dirty_sync_handle_t (*create_handle_fn)(gsl::string_view handle_name,
                                                                                       player &)) {
  if (!create_handle_fn) {
    return;
  }

  if (internal_flags_.test(internal_flag::EN_IFT_IN_DIRTY_CALLBACK)) {
    FWPLOGERROR(*this, "can not insert dirty handle {} when running dirty handle {}", handle_name,
                cache_data_.current_dirty_handle_name);
    return;
  }

  if (cache_data_.dirty_handles.end() != cache_data_.dirty_handles.find(key)) {
    return;
  }

  cache_data_.dirty_handles[key] = create_handle_fn(handle_name, *this);
}

void player::insert_dirty_handle_if_not_exists(uintptr_t key, gsl::string_view handle_name,
                                               build_dirty_message_fn_t build_fn, clear_dirty_cache_fn_t clear_fn) {
  if (!build_fn && !clear_fn) {
    return;
  }

  if (internal_flags_.test(internal_flag::EN_IFT_IN_DIRTY_CALLBACK)) {
    FWPLOGERROR(*this, "can not insert dirty handle {} when running dirty handle {}", handle_name,
                cache_data_.current_dirty_handle_name);
    return;
  }

  if (cache_data_.dirty_handles.end() != cache_data_.dirty_handles.find(key)) {
    return;
  }

  dirty_sync_handle_t &handle = cache_data_.dirty_handles[key];
  handle.build_fn = build_fn;
  handle.clear_fn = clear_fn;
  handle.name = handle_name;
}
