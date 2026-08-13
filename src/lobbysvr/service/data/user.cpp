// Copyright 2021 atframework

#include "data/user.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <gsl/select-gsl.h>

#include <memory/object_allocator.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include <logic/async_jobs/user_async_jobs_manager.h>
#include <logic/cache/user_cache_manager.h>
#include <logic/chat/user_chat_manager.h>
#include <logic/matching/user_matching_manager.h>
#include <logic/orbit/user_orbit_manager.h>
#include <logic/rank/user_rank_manager.h>
#include <logic/team/user_team_manager.h>

#include <logic/user_manager.h>

#include <data/session.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/rpc_common_types.h>
#include <rpc/rpc_utils.h>

#include <string>

user::internal_flag_guard_t::internal_flag_guard_t() : flag_(internal_flag::EN_IFT_FEATURE_INVALID), owner_(nullptr) {}
user::internal_flag_guard_t::~internal_flag_guard_t() { reset(); }

void user::internal_flag_guard_t::setup(user &owner, internal_flag::type f) {
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

void user::internal_flag_guard_t::reset() {
  if (nullptr != owner_ && internal_flag::EN_IFT_FEATURE_INVALID != flag_) {
    owner_->internal_flags_.set(flag_, false);
  }

  owner_ = nullptr;
  flag_ = internal_flag::EN_IFT_FEATURE_INVALID;
}

user::user(fake_constructor &ctor)
    : base_type(ctor),
      heartbeat_data_{},
      user_async_jobs_manager_(atfw::component::memory::stl::make_strong_rc<user_async_jobs_manager>(*this)),
      user_rank_manager_(atfw::component::memory::stl::make_strong_rc<user_rank_manager>(*this)),
      user_cache_manager_(atfw::component::memory::stl::make_strong_rc<user_cache_manager>(*this)),
      user_chat_manager_(atfw::component::memory::stl::make_strong_rc<user_chat_manager>(*this)),
      user_orbit_manager_(atfw::component::memory::stl::make_strong_rc<user_orbit_manager>(*this)),
      user_matching_manager_(atfw::component::memory::stl::make_strong_rc<user_matching_manager>(*this)),
      user_team_manager_(atfw::component::memory::stl::make_strong_rc<user_team_manager>(*this)) {
  heartbeat_data_.continue_error_times = 0;
  heartbeat_data_.last_recv_time = 0;
  heartbeat_data_.sum_error_times = 0;

  cache_data_.refresh_feature_limit_second = 0;
  cache_data_.refresh_feature_limit_minute = 0;
  cache_data_.refresh_feature_limit_hour = 0;

  clear_dirty_cache();
}

user::~user() {}

bool user::can_be_writable() const {
  // this user type can be writable
  return true;
}

bool user::is_writable() const {
  // this user type can be writable
  return can_be_writable() && is_inited();
}

void user::init(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  base_type::init(user_id, zone_id, openid);

  // all manager init
  // ptr_t self = shared_from_this();
}

user::ptr_t user::create(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
  fake_constructor ctorp;
  ptr_t ret = atfw::memory::stl::make_shared<user>(ctorp);
  if (ret) {
    ret->init(user_id, zone_id, openid);
  }

  return ret;
}

rpc::result_code_type user::create_init(rpc::context &parent_ctx) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.create_init", std::move(trace_start_option));

  auto ret = RPC_AWAIT_CODE_RESULT(base_type::create_init(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }

  set_data_version(USER_DATA_LOGIC_VERSION);

  //! === manager implement === 创建后事件回调，这时候还没进入数据库并且未执行login_init()
  user_async_jobs_manager_->create_init(ctx);
  user_rank_manager_->create_init(ctx);
  user_matching_manager_->create_init(ctx);
  // TODO init all interval checkpoint

  // TODO init items
  // if (PROJECT_NAMESPACE_ID::EN_VERSION_GM != version_type) {
  //     excel::user_init_items::me()->foreach ([this](const excel::user_init_items::value_type &v) {
  //         if (0 != v->id()) {
  //             add_entity(v->id(), v->number(), PROJECT_NAMESPACE_ID::EN_ICMT_INIT,
  //             PROJECT_NAMESPACE_ID::EN_ICST_DEFAULT);
  //         }
  //     });
  // }

  RPC_RETURN_CODE(trace.finish({0, {}}));
}

rpc::result_code_type user::login_init(rpc::context &parent_ctx) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.login_init", std::move(trace_start_option));

  auto ret = RPC_AWAIT_CODE_RESULT(base_type::login_init(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }

  // 由于对象缓存可以被复用，这个函数可能会被多次执行。这个阶段，新版本的 login_table 已载入

  //! === manager implement === 登入成功后事件回调，新用户也会触发

  // all module login init
  user_async_jobs_manager_->login_init(ctx);
  user_rank_manager_->login_init(ctx);

  ret = user_cache_manager_->login_init(ctx);
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }

  ret = user_chat_manager_->login_init(ctx);
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }
  // 匹配恢复失败不阻断登录；玩家仍可通过 matching_check 主动重试。
  RPC_AWAIT_IGNORE_RESULT(user_matching_manager_->login_init(ctx));

  ret = RPC_AWAIT_CODE_RESULT(user_orbit_manager_->login_init(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }

  ret = user_team_manager_->login_init(ctx);
  if (ret < 0) {
    RPC_RETURN_CODE(trace.finish({ret, {}}));
  }

  set_inited();
  on_login(ctx);

  RPC_RETURN_CODE(trace.finish({0, {}}));
}

bool user::is_dirty() const {
  bool ret = base_type::is_dirty();

#define USER_CHECK_RET_DIRTY(RET, EXPR) \
  if (RET) {                            \
    return RET;                         \
  }                                     \
  RET = EXPR

  //! === manager implement === 检查是否有脏数据
  USER_CHECK_RET_DIRTY(ret, user_async_jobs_manager_->is_dirty());
  USER_CHECK_RET_DIRTY(ret, user_rank_manager_->is_dirty());
  USER_CHECK_RET_DIRTY(ret, user_matching_manager_->is_dirty());

#undef USER_CHECK_RET_DIRTY

  return ret;
}

void user::clear_dirty() {
  //! === manager implement === 清理脏数据标记
  user_async_jobs_manager_->clear_dirty();
  user_rank_manager_->clear_dirty();
  user_matching_manager_->clear_dirty();
}

void user::refresh_feature_limit(rpc::context &ctx) {
  base_type::refresh_feature_limit(ctx);

  //! === manager implement === 不定期调用，用于刷新逻辑
  // all modules refresh limit
  user_async_jobs_manager_->refresh_feature_limit(ctx);
  // user_rank_manager_->refresh_feature_limit(ctx);

  time_t now = atfw::util::time::time_utility::get_now();
  if (now != cache_data_.refresh_feature_limit_second) {
    cache_data_.refresh_feature_limit_second = now;
    // 每秒仅需要执行一次的refresh_feature_limit

    user_cache_manager_->refresh_feature_limit_second(ctx);
    user_orbit_manager_->refresh_feature_limit_second(ctx);
  }
  if (now >= cache_data_.refresh_feature_limit_minute + atfw::util::time::time_utility::MINITE_SECONDS ||
      now < cache_data_.refresh_feature_limit_minute) {
    cache_data_.refresh_feature_limit_minute = now - (now % atfw::util::time::time_utility::MINITE_SECONDS);

    // 每分钟仅需要执行一次的refresh_feature_limit
    user_cache_manager_->refresh_feature_limit_minute(ctx);
  }
  if (now >= cache_data_.refresh_feature_limit_hour + atfw::util::time::time_utility::HOUR_SECONDS ||
      now < cache_data_.refresh_feature_limit_hour) {
    cache_data_.refresh_feature_limit_hour = now - (now % atfw::util::time::time_utility::HOUR_SECONDS);

    // 每小时仅需要执行一次的refresh_feature_limit
  }
}

void user::on_login(rpc::context &parent_ctx) {
  // Trigger by login_init()
  if (!is_inited()) {
    return;
  }
  if (internal_flags_.test(internal_flag::EN_IFT_IS_LOGIN)) {
    // 已经登录
    return;
  }

  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.on_login", std::move(trace_start_option));

  base_type::on_login(ctx);

  // TODO sync messages
  internal_flags_.set(internal_flag::EN_IFT_IS_LOGIN, true);
  trace.finish({0, {}});
}

void user::on_logout(rpc::context &parent_ctx) {
  if (!internal_flags_.test(internal_flag::EN_IFT_IS_LOGIN)) {
    // 未登录状态不处理logout
    return;
  }
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.on_logout", std::move(trace_start_option));

  base_type::on_logout(ctx);

  user_cache_manager_->on_logout(ctx);

  internal_flags_.set(internal_flag::EN_IFT_IS_LOGIN, false);
  trace.finish({0, {}});
}

void user::on_saved(rpc::context &ctx) {
  // at last call base on remove callback
  base_type::on_saved(ctx);

  user_cache_manager_->on_saved(ctx);
}

void user::on_update_session(rpc::context &ctx, const std::shared_ptr<session> &from,
                             const std::shared_ptr<session> &to) {
  base_type::on_update_session(ctx, from, to);

  user_cache_manager_->on_update_session(ctx);
}

void user::init_from_table_data(rpc::context &parent_ctx, const PROJECT_NAMESPACE_ID::table_user &tb_user) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.init_from_table_data", std::move(trace_start_option));

  base_type::init_from_table_data(ctx, tb_user);

  // TODO data patch, 这里用于版本升级时可能需要升级用户数据库，做版本迁移
  // PROJECT_NAMESPACE_ID::table_user tb_patch;
  // const PROJECT_NAMESPACE_ID::table_user *src_tb = &tb_user;
  // if (data_version_ < USER_DATA_LOGIC_VERSION) {
  //     protobuf_copy_message(tb_patch, tb_user);
  //     src_tb = &tb_patch;
  //     //GameUserPatchMgr::Instance()->Patch(tb_patch, m_iDataVersion, GAME_USER_DATA_LOGIC);
  //     data_version_ = USER_DATA_LOGIC_VERSION;
  // }

  //! === manager implement === 从数据库读取，注意本接口可能被调用多次，需要清理老数据
  if (tb_user.has_async_job_blob_data()) {
    user_async_jobs_manager_->init_from_table_data(ctx, tb_user);
  }

  if (tb_user.has_rank_data()) {
    user_rank_manager_->init_from_table_data(ctx, tb_user);
  }

  if (tb_user.has_orbit_room_data()) {
    user_orbit_manager_->init_from_table_data(ctx, tb_user);
  }

  if (tb_user.has_matching_data()) {
    user_matching_manager_->init_from_table_data(ctx, tb_user);
  }

  trace.finish({0, {}});
}

int user::dump(rpc::context &parent_ctx, PROJECT_NAMESPACE_ID::table_user &table, bool always) {
  rpc::context ctx{parent_ctx.create_temporary_child()};
  rpc::telemetry::tracer trace;
  rpc::telemetry::trace_start_option trace_start_option;
  trace_start_option.dispatcher = nullptr;
  trace_start_option.is_remote = false;
  trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ctx.setup_tracer(trace, "user.dump", std::move(trace_start_option));

  int ret = base_type::dump(ctx, table, always);
  if (ret < 0) {
    return trace.finish({ret, {}});
  }

  //! === manager implement === 保存到数据库
  // all modules dump to DB
  ret = user_async_jobs_manager_->dump(ctx, table);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump async_jobs_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return trace.finish({ret, {}});
  }

  ret = user_rank_manager_->dump(ctx, table);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump user_rank_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return trace.finish({ret, {}});
  }

  ret = user_orbit_manager_->dump(ctx, table);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump user_orbit_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return trace.finish({ret, {}});
  }

  ret = user_matching_manager_->dump(ctx, table);
  if (ret < 0) {
    FWPLOGERROR(*this, "dump user_matching_manager_ failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    return trace.finish({ret, {}});
  }

  return trace.finish({ret, {}});
}

void user::update_heartbeat() {
  const auto &logic_cfg = logic_config::me()->get_logic_cfg();
  time_t heartbeat_interval = logic_cfg.heartbeat().interval().seconds();
  time_t heartbeat_tolerance = logic_cfg.heartbeat().tolerance().seconds();
  time_t tol_dura = heartbeat_interval - heartbeat_tolerance;
  time_t now_time = atfw::util::time::time_utility::get_sys_now();

  // 小于容忍值得要统计错误次数
  if (now_time - heartbeat_data_.last_recv_time < tol_dura) {
    ++heartbeat_data_.continue_error_times;
    ++heartbeat_data_.sum_error_times;
  } else {
    heartbeat_data_.continue_error_times = 0;
  }

  heartbeat_data_.last_recv_time = now_time;
}

void user::send_all_syn_msg(rpc::context &ctx) {
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

    if (dirty_msg.user_dirty) {
      rpc::lobbysvrclientservice::send_user_dirty_chg_sync(ctx, *dirty_msg.user_dirty, *sess);
    }
  }

  // 缓存过期更新
  user_cache_manager_->update_user_cache_info(ctx);

  clear_dirty_cache();
}

rpc::result_code_type user::await_before_logout_tasks(rpc::context &ctx) {
  // 等待全部涉及保存的异步任务完成
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::await_before_logout_tasks(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(user_async_jobs_manager_->wait_for_async_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(wait_task_lock(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}

void user::clear_dirty_cache() {
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

namespace {
template <class TMSG, class TCONTAINER>
static user::dirty_sync_handle_t _user_generate_dirty_handle(
    gsl::string_view /*handle_name*/, TMSG *(PROJECT_NAMESPACE_ID::SCUserDirtyChgSync::*add_fn)(),
    TCONTAINER user::cache_t::*get_mem) {
  user::dirty_sync_handle_t handle;
  handle.build_fn = [add_fn, get_mem](user &user_inst, user::dirty_message_container &output) {
    if (!get_mem) {
      return;
    }

    TCONTAINER &container = (user_inst.get_cache_data().*get_mem);
    if (container.empty()) {
      return;
    }

    if (!output.user_dirty) {
      output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
    }
    if (!output.user_dirty) {
      FWLOGERROR("malloc dirty msg body failed");
      return;
    }

    for (auto &dirty_data : container) {
      auto copied_item = (output.user_dirty.get()->*add_fn)();
      if (nullptr == copied_item) {
        FWLOGERROR("SCUserDirtyChgSync add item failed");
        return;
      }
      protobuf_copy_message(*copied_item, dirty_data.second);
    }
  };

  handle.clear_fn = [get_mem](user &user_inst) {
    if (get_mem) {
      (user_inst.get_cache_data().*get_mem).clear();
    }
  };

  return handle;
}
}  // namespace

PROJECT_NAMESPACE_ID::DItemInstance &user::mutable_dirty_item(const PROJECT_NAMESPACE_ID::DItemInstance &in) {
  insert_dirty_handle_if_not_exists(reinterpret_cast<uintptr_t>(&cache_data_.dirty_item_by_type),
                                    "user.mutable_dirty_item", [](gsl::string_view handle_name, user &) {
                                      return _user_generate_dirty_handle(
                                          handle_name, &PROJECT_NAMESPACE_ID::SCUserDirtyChgSync::add_dirty_items,
                                          &user::cache_t::dirty_item_by_type);
                                    });

  PROJECT_NAMESPACE_ID::DItemInstance &ret =
      cache_data_.dirty_item_by_type[static_cast<int32_t>(in.item_basic().type_id())];
  ret = in;
  return ret;
}

void user::insert_dirty_handle_if_not_exists(uintptr_t key, gsl::string_view handle_name,
                                             dirty_sync_handle_t (*create_handle_fn)(gsl::string_view handle_name,
                                                                                     user &)) {
  if (create_handle_fn == nullptr) {
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

void user::insert_dirty_handle_if_not_exists(uintptr_t key, gsl::string_view handle_name,
                                             // NOLINTNEXTLINE(performance-unnecessary-value-param)
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

static std::vector<std::pair<bool (PROJECT_NAMESPACE_ID::CSUserGetInfoReq::*)() const,
                             void (*)(rpc::context &, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp &, user &)>>
    g_get_info_handle_list;

void user::init_get_info_handle(bool (PROJECT_NAMESPACE_ID::CSUserGetInfoReq::*check_need_fn)() const,
                                void (*dump_fn)(rpc::context &, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp &, user &)) {
  if (check_need_fn == nullptr || dump_fn == nullptr) {
    FWLOGERROR("init_get_info_handle failed, check_need_fn or dump_fn is nullptr");
    return;
  }
  g_get_info_handle_list.emplace_back(check_need_fn, dump_fn);
}

std::vector<std::pair<bool (PROJECT_NAMESPACE_ID::CSUserGetInfoReq::*)() const,
                      void (*)(rpc::context &, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp &, user &)>>
user::get_get_info_handle() {
  return g_get_info_handle_list;
}
