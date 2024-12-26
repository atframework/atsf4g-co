// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#include "router/router_object_base.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

#include "router/router_manager_set.h"

SERVER_FRAME_API bool router_object_base::key_t::operator==(const key_t &r) const noexcept {
  return object_id == r.object_id && zone_id == r.zone_id && type_id == r.type_id;
}

SERVER_FRAME_API bool router_object_base::key_t::operator!=(const key_t &r) const noexcept {
  return object_id != r.object_id || zone_id != r.zone_id || type_id != r.type_id;
}

SERVER_FRAME_API bool router_object_base::key_t::operator<(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id < r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id < r.zone_id;
  }

  return object_id < r.object_id;
}

SERVER_FRAME_API bool router_object_base::key_t::operator<=(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id < r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id < r.zone_id;
  }

  return object_id <= r.object_id;
}

SERVER_FRAME_API bool router_object_base::key_t::operator>(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id > r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id > r.zone_id;
  }

  return object_id > r.object_id;
}

SERVER_FRAME_API bool router_object_base::key_t::operator>=(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id > r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id > r.zone_id;
  }

  return object_id >= r.object_id;
}

SERVER_FRAME_API router_object_base::flag_guard::flag_guard(router_object_base &owner, int f) : owner_(&owner), f_(f) {
  if (f_ & owner_->get_flags()) {
    f_ = 0;
  } else if (0 != f_) {
    owner_->set_flag(f_);
  }
}

SERVER_FRAME_API router_object_base::flag_guard::~flag_guard() {
  if (nullptr != owner_ && 0 != f_) {
    owner_->unset_flag(f_);
  }
}

SERVER_FRAME_API router_object_base::io_task_guard::io_task_guard() : await_task_id_(0) {}

SERVER_FRAME_API router_object_base::io_task_guard::~io_task_guard() {
  if (0 == await_task_id_ || owner_.expired()) {
    return;
  }

  std::shared_ptr<router_object_base> owner = owner_.lock();
  if (!owner) {
    return;
  }

  // IO任务被抢占
  if (owner->io_task_id_ != await_task_id_) {
    return;
  }

  owner->wakeup_io_task_awaiter();
}

SERVER_FRAME_API rpc::result_code_type router_object_base::io_task_guard::take(rpc::context &ctx,
                                                                               router_object_base &owner) noexcept {
  rpc::result_code_type::value_type ret = 0;
  // 已被调用者接管，则忽略子层接管
  if (owner.io_task_id_ == ctx.get_task_context().task_id) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(owner.await_io_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (0 == ctx.get_task_context().task_id) {
    RPC_RETURN_CODE(ret);
  }

  owner_ = owner.shared_from_this();
  await_task_id_ = ctx.get_task_context().task_id;

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API router_object_base::router_object_base(const key_t &k)
    : key_(k),
      last_save_time_(0),
      last_visit_time_(0),
      router_svr_id_(0),
      router_svr_ver_(0),
      timer_sequence_(0),
      timer_list_(nullptr),
      io_task_id_(0),
      saving_sequence_(0),
      saved_sequence_(0),
      io_last_pull_cache_task_id_(0),
      io_last_pull_object_task_id_(0),
      flags_(0) {
  // 创建时初始化最后访问时间
  refresh_visit_time();
}

SERVER_FRAME_API router_object_base::router_object_base(key_t &&k)
    : key_(k),
      last_save_time_(0),
      last_visit_time_(0),
      router_svr_id_(0),
      router_svr_ver_(0),
      timer_sequence_(0),
      timer_list_(nullptr),
      io_task_id_(0),
      saving_sequence_(0),
      saved_sequence_(0),
      io_last_pull_cache_task_id_(0),
      io_last_pull_object_task_id_(0),
      flags_(0) {
  // 创建时初始化最后访问时间
  refresh_visit_time();
}

SERVER_FRAME_API router_object_base::~router_object_base() {
  // 清理掉timer
  unset_timer_ref();
}

SERVER_FRAME_API void router_object_base::refresh_visit_time() {
  last_visit_time_ = atfw::util::time::time_utility::get_sys_now();

  // 刷新访问事件要取消移除缓存的计划任务
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
}

SERVER_FRAME_API void router_object_base::refresh_save_time() {
  last_save_time_ = atfw::util::time::time_utility::get_sys_now();
}

SERVER_FRAME_API rpc::result_code_type router_object_base::remove_object(rpc::context &ctx, void *priv_data,
                                                                         uint64_t transfer_to_svr_id,
                                                                         io_task_guard &guard) {
  // 先等待其他IO任务完成
  auto ret = RPC_AWAIT_CODE_RESULT(guard.take(ctx, *this));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  // 在等待其他任务的时候已经完成移除或降级，直接成功即可
  if (!is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  // 移除实体需要设置路由BUS ID为0并保存一次
  uint64_t old_router_server_id = get_router_server_id();
  uint64_t old_router_ver = get_router_version();

  if (transfer_to_svr_id != get_router_server_id()) {
    set_router_server_id(transfer_to_svr_id, old_router_ver + 1);
  }
  refresh_visit_time();

  ret = RPC_AWAIT_CODE_RESULT(save(ctx, priv_data, guard));
  if (ret < 0) {
    FWLOGERROR("remove router object {}:{}:{}, res: {}", get_key().type_id, get_key().zone_id, get_key().object_id,
               ret);

    // 保存失败则恢复原来的路由信息
    set_router_server_id(old_router_server_id, old_router_ver);
    RPC_RETURN_CODE(ret);
  }

  downgrade();
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API bool router_object_base::is_cache_available() const {
  if (is_pulling_cache()) {
    return false;
  }

  if (is_writable()) {
    return true;
  }

  if (last_save_time_ + logic_config::me()->get_cfg_router().cache_update_interval().seconds() <
      atfw::util::time::time_utility::get_sys_now()) {
    return false;
  }

  return true;
}

SERVER_FRAME_API bool router_object_base::is_object_available() const {
  if (is_pulling_object()) {
    return false;
  }

  return is_writable();
}

SERVER_FRAME_API rpc::result_code_type router_object_base::pull_cache(rpc::context &ctx, void *priv_data) {
  return pull_object(ctx, priv_data);
}

SERVER_FRAME_API rpc::result_code_type router_object_base::save(rpc::context &ctx, void *priv_data) {
  io_task_guard io_guard;
  auto ret = RPC_AWAIT_CODE_RESULT(save(ctx, priv_data, io_guard));
  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API int router_object_base::upgrade() {
  if (check_flag(flag_t::EN_ROFT_IS_OBJECT)) {
    return 0;
  }

  refresh_visit_time();
  set_flag(flag_t::EN_ROFT_IS_OBJECT);
  unset_flag(flag_t::EN_ROFT_CACHE_REMOVED);

  // 升级操作要取消移除缓存和降级的计划任务
  unset_flag(flag_t::EN_ROFT_FORCE_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
  return 0;
}

SERVER_FRAME_API int router_object_base::downgrade() {
  if (!check_flag(flag_t::EN_ROFT_IS_OBJECT)) {
    return 0;
  }

  refresh_visit_time();
  unset_flag(flag_t::EN_ROFT_IS_OBJECT);
  return 0;
}

SERVER_FRAME_API int router_object_base::send_transfer_msg_failed(atframework::SSMsg &&req) {
  task_action_ss_req_base::message_type rsp_msg;
  uint64_t dst_pd = req.head().node_id();
  gsl::string_view dst_node_name = req.head().node_name();
  if (req.head().has_router()) {
    dst_pd = req.head().router().router_source_node_id();
    dst_node_name = req.head().router().router_source_node_name();
  }

  task_action_ss_req_base::init_msg(rsp_msg, dst_pd, dst_node_name, req);

  // 如果没有task_id则要不复制路由信息，防止触发路由转发
  if (0 == rsp_msg.head().destination_task_id()) {
    rsp_msg.mutable_head()->clear_router();
  }

  // 转移失败错误码
  rsp_msg.mutable_head()->set_error_code(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TRANSFER);

  return ss_msg_dispatcher::me()->send_to_proc(dst_pd, rsp_msg);
}

SERVER_FRAME_API void router_object_base::trace_router(rpc::context &ctx, uint32_t type_id, uint32_t zone_id,
                                                       uint64_t object_id) {
  ctx.update_task_context_reference_object(type_id, zone_id, object_id);
  auto &trace_span = ctx.get_trace_span();
  if (!trace_span) {
    return;
  }

  trace_span->AddEvent("router", {{"type_id", type_id}, {"zone_id", zone_id}, {"object_id", object_id}});
}

SERVER_FRAME_API void router_object_base::wakeup_io_task_awaiter() {
  task_type_trait::task_type failed_task;
  while (!io_task_awaiter_.empty()) {
    if (0 != io_task_id_) {
      task_type_trait::task_type io_task = task_manager::me()->get_task(io_task_id_);
      if (!task_type_trait::empty(io_task) && !task_type_trait::is_exiting(io_task)) {
        break;
      }
    }

    task_type_trait::task_type wake_task = io_task_awaiter_.front();
    if (!task_type_trait::empty(wake_task) && !task_type_trait::is_exiting(wake_task) &&
        !task_type_trait::equal(failed_task, wake_task)) {
      // iter will be erased in task
      dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
      callback_data.message.message_type =
          reinterpret_cast<uintptr_t>(reinterpret_cast<const void *>(&io_task_awaiter_));
      callback_data.sequence = task_type_trait::get_task_id(wake_task);

      if (rpc::custom_resume(wake_task, callback_data) < 0) {
        failed_task = wake_task;
      } else {
        task_type_trait::reset_task(failed_task);
      }
    } else {
      // This should not be called
      if (!task_type_trait::empty(wake_task)) {
        FWLOGERROR("Wake iterator of task {} should be removed by task action",
                   task_type_trait::get_task_id(wake_task));
      }
      io_task_awaiter_.pop_front();
    }
  }
}

SERVER_FRAME_API rpc::result_code_type router_object_base::await_io_task(rpc::context &ctx) {
  int32_t ret = 0;
  if (0 == io_task_id_ || io_task_id_ == ctx.get_task_context().task_id) {
    RPC_RETURN_CODE(ret);
  }

  while (0 != io_task_id_ && io_task_id_ != ctx.get_task_context().task_id) {
    task_type_trait::task_type io_task = task_manager::me()->get_task(io_task_id_);
    if (task_type_trait::empty(io_task)) {
      io_task_id_ = 0;
      break;
    }

    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_status);
    ret = task_manager::convert_task_status_to_error_code(current_status);
    if (ret < 0) {
      break;
    }

    if (task_type_trait::is_exiting(io_task)) {
      io_task_id_ = 0;
      break;
    }

    FWLOGDEBUG("task {} start to await for task {} by router object/cache {}:{}:{}", ctx.get_task_context().task_id,
               io_task_id_, get_key().type_id, get_key().zone_id, get_key().object_id);

    std::list<task_type_trait::task_type>::iterator awaiter_iter;
    {
      task_type_trait::task_type self_task = task_manager::me()->get_task(ctx.get_task_context().task_id);
      if (task_type_trait::empty(self_task)) {
        break;
      }
      awaiter_iter = io_task_awaiter_.insert(io_task_awaiter_.end(), self_task);
    }

    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = ctx.get_task_context().task_id;
    await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                                          std::chrono::seconds{6});

    RPC_AWAIT_IGNORE_RESULT(rpc::custom_wait(ctx, reinterpret_cast<const void *>(&io_task_awaiter_), await_options));
    io_task_awaiter_.erase(awaiter_iter);
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_object_base::await_io_task(rpc::context &ctx,
                                                                         task_type_trait::task_type &other_task) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");
  if (task_type_trait::empty(other_task)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  if (task_type_trait::get_task_id(other_task) == io_task_id_) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(await_io_task(ctx)));
  }

  rpc::result_code_type::value_type ret = 0;
  while (true) {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_status);
    ret = task_manager::convert_task_status_to_error_code(current_status);
    if (ret < 0) {
      break;
    }

    if (task_type_trait::empty(other_task) ||
        ctx.get_task_context().task_id == task_type_trait::get_task_id(other_task)) {
      break;
    }

    if (task_type_trait::is_exiting(other_task)) {
      task_type_trait::reset_task(other_task);
      continue;
    }

    FWLOGDEBUG("task {} start to await for task {} by router object/cache {}:{}:{}", ctx.get_task_context().task_id,
               task_type_trait::get_task_id(other_task), get_key().type_id, get_key().zone_id, get_key().object_id);

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, other_task));
    if (ret < 0) {
      break;
    }
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_object_base::internal_pull_cache(rpc::context &ctx, void *priv_data,
                                                                               io_task_guard &guard) {
  // 触发拉取缓存时要取消移除缓存的计划任务
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(guard.take(ctx, *this));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  {
    auto router_ptr = shared_from_this();
    // 先等待之前的任务完成再设置flag
    flag_guard fg(*router_ptr, flag_t::EN_ROFT_PULLING_CACHE);

    ret = RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(ctx));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    std::shared_ptr<router_manager_metrics_data> metrics_data =
        router_manager_set::me()->mutable_metrics_data(router_ptr->get_key().type_id);
    if (metrics_data) {
      ++metrics_data->pull_cache_count;
    }

    ret = RPC_AWAIT_CODE_RESULT(router_ptr->pull_cache(ctx, priv_data));

    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    // 拉取成功要refresh_save_time
    router_ptr->refresh_save_time();
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_object_base::internal_pull_object(rpc::context &ctx, void *priv_data,
                                                                                io_task_guard &guard) {
  // 触发拉取实体时要取消移除缓存和降级的计划任务
  unset_flag(flag_t::EN_ROFT_FORCE_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  auto ret = RPC_AWAIT_CODE_RESULT(guard.take(ctx, *this));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  // 其他任务中已经拉取成功并已经升级为实体且未失效，直接视为成功
  if (is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  {
    auto router_ptr = shared_from_this();
    // 先等待之前的任务完成再设置flag
    flag_guard fg(*router_ptr, flag_t::EN_ROFT_PULLING_OBJECT);

    router_ptr->unset_flag(flag_t::EN_ROFT_CACHE_REMOVED);
    router_ptr->unset_flag(flag_t::EN_ROFT_FORCE_PULL_OBJECT);

    // 执行读任务
    RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(ctx));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    std::shared_ptr<router_manager_metrics_data> metrics_data =
        router_manager_set::me()->mutable_metrics_data(router_ptr->get_key().type_id);
    if (metrics_data) {
      ++metrics_data->pull_object_count;
    }

    ret = RPC_AWAIT_CODE_RESULT(router_ptr->pull_object(ctx, priv_data));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    // 拉取成功要refresh_save_time
    router_ptr->refresh_save_time();

    if (0 != router_ptr->get_router_server_id()) {
      if (logic_config::me()->get_local_server_id() != router_ptr->get_router_server_id()) {
        // 可能某处的缓存过老，这是正常流程，返回错误码即可，不用打错误日志
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
      }
    }

    // 升级为实体
    router_ptr->upgrade();
  }

  RPC_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::result_code_type router_object_base::internal_save_object(rpc::context &ctx, void *priv_data,
                                                                                io_task_guard &guard) {
  unset_flag(flag_t::EN_ROFT_SCHED_SAVE_OBJECT);

  // 排队写任务和并发写merge
  uint64_t this_saving_seq = ++saving_sequence_;

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  // 如果有其他任务正在保存，需要等待那个任务结束
  auto ret = RPC_AWAIT_CODE_RESULT(guard.take(ctx, *this));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (!is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
  }

  // 因为可能叠加和被其他任务抢占，所以这里需要核查一遍保存序号
  // 如果其他任务的保存涵盖了自己的数据变化，则直接成功返回
  if (saved_sequence_ >= this_saving_seq) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  {
    auto router_ptr = shared_from_this();
    // 先等待之前的任务完成再设置flag
    flag_guard fg(*router_ptr, flag_t::EN_ROFT_SAVING);

    uint64_t real_saving_seq = router_ptr->saving_sequence_;
    ret = RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(ctx));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    std::shared_ptr<router_manager_metrics_data> metrics_data =
        router_manager_set::me()->mutable_metrics_data(router_ptr->get_key().type_id);
    if (metrics_data) {
      ++metrics_data->save_count;
    }

    ret = RPC_AWAIT_CODE_RESULT(router_ptr->save_object(ctx, priv_data));

    if (ret >= 0 && real_saving_seq > router_ptr->saved_sequence_) {
      router_ptr->saved_sequence_ = real_saving_seq;
    } else if (ret < 0) {
      // 保存失败
      RPC_RETURN_CODE(ret);
    }

    router_ptr->refresh_save_time();
  }

  RPC_RETURN_CODE(ret);
}

void router_object_base::reset_timer_ref(std::list<router_system_timer_t> *timer_list,
                                         const std::list<router_system_timer_t>::iterator &it) {
  if (timer_list_ == timer_list && timer_iter_ == it) {
    return;
  }

  unset_timer_ref();

  timer_list_ = timer_list;
  timer_iter_ = it;
}

void router_object_base::check_and_remove_timer_ref(std::list<router_system_timer_t> *timer_list,
                                                    const std::list<router_system_timer_t>::iterator &it) {
  if (timer_list_ != timer_list || timer_iter_ != it) {
    return;
  }

  if (timer_list_ != nullptr && timer_iter_ != timer_list_->end()) {
    // 内部接口，会在外层执行timer_list_->erase(timer_iter_);所以这里不执行移除
    timer_iter_ = timer_list_->end();
  }
  timer_list_ = nullptr;
}

void router_object_base::unset_timer_ref() {
  // 清理掉timer
  if (timer_list_ != nullptr && timer_iter_ != timer_list_->end()) {
    timer_list_->erase(timer_iter_);
    timer_iter_ = timer_list_->end();
  }
  timer_list_ = nullptr;
}

rpc::result_code_type router_object_base::await_io_schedule_order_task(rpc::context &ctx) {
  if (io_schedule_order_.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto last_task_id = *io_schedule_order_.rbegin();
  // Waiting for all previous task finished
  while (!io_schedule_order_.empty()) {
    task_type_trait::id_type select_task_id = 0;

    {
      auto iter = io_schedule_order_.lower_bound(last_task_id);
      if (iter == io_schedule_order_.end()) {
        select_task_id = *io_schedule_order_.rbegin();
      } else if (*iter == last_task_id) {
        select_task_id = last_task_id;
      } else if (iter != io_schedule_order_.begin()) {
        --iter;
        select_task_id = *iter;
      }
    }

    if (0 == select_task_id || select_task_id > last_task_id) {
      break;
    }

    if (task_manager::is_instance_destroyed()) {
      break;
    }

    auto task = task_manager::me()->get_task(select_task_id);
    // Fix data
    if (task_type_trait::empty(task)) {
      io_schedule_order_.erase(select_task_id);
      continue;
    }

    if (task_type_trait::is_exiting(task)) {
      io_schedule_order_.erase(select_task_id);
      continue;
    }

    if (task_type_trait::get_task_id(task) == ctx.get_task_context().task_id) {
      io_schedule_order_.erase(select_task_id);
      continue;
    }

    auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, task));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    io_schedule_order_.erase(select_task_id);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
