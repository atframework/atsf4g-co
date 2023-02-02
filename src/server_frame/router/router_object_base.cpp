// Copyright 2021 atframework
// Created by owent on 2018-05-07.
//

#include "router/router_object_base.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <dispatcher/task_manager.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

bool router_object_base::key_t::operator==(const key_t &r) const noexcept {
  return object_id == r.object_id && zone_id == r.zone_id && type_id == r.type_id;
}

bool router_object_base::key_t::operator!=(const key_t &r) const noexcept {
  return object_id != r.object_id || zone_id != r.zone_id || type_id != r.type_id;
}

bool router_object_base::key_t::operator<(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id < r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id < r.zone_id;
  }

  return object_id < r.object_id;
}

bool router_object_base::key_t::operator<=(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id < r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id < r.zone_id;
  }

  return object_id <= r.object_id;
}

bool router_object_base::key_t::operator>(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id > r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id > r.zone_id;
  }

  return object_id > r.object_id;
}

bool router_object_base::key_t::operator>=(const key_t &r) const noexcept {
  if (type_id != r.type_id) {
    return type_id > r.type_id;
  }

  if (zone_id != r.zone_id) {
    return zone_id > r.zone_id;
  }

  return object_id >= r.object_id;
}

router_object_base::flag_guard::flag_guard(router_object_base &owner, int f) : owner_(&owner), f_(f) {
  if (f_ & owner_->get_flags()) {
    f_ = 0;
  } else if (0 != f_) {
    owner_->set_flag(f_);
  }
}

router_object_base::flag_guard::~flag_guard() {
  if (nullptr != owner_ && 0 != f_) {
    owner_->unset_flag(f_);
  }
}

router_object_base::router_object_base(const key_t &k)
    : key_(k),
      last_save_time_(0),
      last_visit_time_(0),
      router_svr_id_(0),
      router_svr_ver_(0),
      timer_sequence_(0),
      timer_list_(nullptr),
      saving_sequence_(0),
      saved_sequence_(0),
      io_last_pull_cache_task_id_(0),
      io_last_pull_object_task_id_(0),
      flags_(0) {
  // 创建时初始化最后访问时间
  refresh_visit_time();
}

router_object_base::router_object_base(key_t &&k)
    : key_(k),
      last_save_time_(0),
      last_visit_time_(0),
      router_svr_id_(0),
      router_svr_ver_(0),
      timer_sequence_(0),
      timer_list_(nullptr),
      saving_sequence_(0),
      saved_sequence_(0),
      io_last_pull_cache_task_id_(0),
      io_last_pull_object_task_id_(0),
      flags_(0) {
  // 创建时初始化最后访问时间
  refresh_visit_time();
}

router_object_base::~router_object_base() {
  // 清理掉timer
  unset_timer_ref();
}

void router_object_base::refresh_visit_time() {
  last_visit_time_ = util::time::time_utility::get_now();

  // 刷新访问事件要取消移除缓存的计划任务
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
}

void router_object_base::refresh_save_time() { last_save_time_ = util::time::time_utility::get_now(); }

rpc::result_code_type router_object_base::remove_object(rpc::context &ctx, void *priv_data,
                                                        uint64_t transfer_to_svr_id) {
  // 先等待其他IO任务完成
  auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
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

  ret = RPC_AWAIT_CODE_RESULT(save(ctx, priv_data));
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

bool router_object_base::is_cache_available() const {
  if (is_pulling_cache()) {
    return false;
  }

  if (is_writable()) {
    return true;
  }

  if (last_save_time_ + logic_config::me()->get_cfg_router().cache_update_interval().seconds() <
      util::time::time_utility::get_now()) {
    return false;
  }

  return true;
}

bool router_object_base::is_object_available() const {
  if (is_pulling_object()) {
    return false;
  }

  return is_writable();
}

rpc::result_code_type router_object_base::pull_cache(rpc::context &ctx, void *priv_data) {
  return pull_object(ctx, priv_data);
}

int router_object_base::upgrade() {
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

int router_object_base::downgrade() {
  if (!check_flag(flag_t::EN_ROFT_IS_OBJECT)) {
    return 0;
  }

  refresh_visit_time();
  unset_flag(flag_t::EN_ROFT_IS_OBJECT);
  return 0;
}

int router_object_base::send_transfer_msg_failed(atframework::SSMsg &&req) {
  task_action_ss_req_base::msg_type rsp_msg;
  uint64_t dst_pd = req.head().bus_id();
  if (req.head().has_router()) {
    dst_pd = req.head().router().router_src_bus_id();
  }

  task_action_ss_req_base::init_msg(rsp_msg, dst_pd, req);

  // 如果没有task_id则要不复制路由信息，防止触发路由转发
  if (0 == rsp_msg.head().dst_task_id()) {
    rsp_msg.mutable_head()->clear_router();
  }

  // 转移失败错误码
  rsp_msg.mutable_head()->set_error_code(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TRANSFER);

  return ss_msg_dispatcher::me()->send_to_proc(dst_pd, rsp_msg);
}

void router_object_base::trace_router(rpc::context &ctx, uint32_t type_id, uint32_t zone_id, uint64_t object_id) {
  auto &trace_span = ctx.get_trace_span();
  if (!trace_span) {
    return;
  }

  trace_span->AddEvent("router", {{"type_id", type_id}, {"zone_id", zone_id}, {"object_id", object_id}});
}

void router_object_base::wakeup_io_task_awaiter() {
  if (task_type_trait::empty(io_task_)) {
    return;
  }
  task_type_trait::reset_task(io_task_);

  while (!io_task_awaiter_.empty() && task_type_trait::empty(io_task_)) {
    task_type_trait::task_type wake_task = io_task_awaiter_.front();
    if (!task_type_trait::empty(wake_task) && !task_type_trait::is_exiting(wake_task)) {
      // iter will be erased in task
      dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
      callback_data.message.msg_type = reinterpret_cast<uintptr_t>(reinterpret_cast<const void *>(&io_task_awaiter_));
      callback_data.sequence = task_type_trait::get_task_id(wake_task);

      rpc::custom_resume(wake_task, callback_data);
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

rpc::result_code_type router_object_base::await_io_task(rpc::context &ctx) {
  int32_t ret = 0;
  while (!task_type_trait::empty(io_task_) &&
         ctx.get_task_context().task_id != task_type_trait::get_task_id(io_task_)) {
    ret = task_manager::convert_task_status_to_error_code(TASK_COMPAT_GET_CURRENT_STATUS());
    if (ret < 0) {
      break;
    }

    if (task_type_trait::is_exiting(io_task_)) {
      wakeup_io_task_awaiter();
      continue;
    }

    FWLOGDEBUG("task {} start to await for task {} by router object/cache {}:{}:{}", ctx.get_task_context().task_id,
               task_type_trait::get_task_id(io_task_), get_key().type_id, get_key().zone_id, get_key().object_id);

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

    RPC_AWAIT_IGNORE_RESULT(
        rpc::custom_wait(ctx, reinterpret_cast<const void *>(&io_task_awaiter_), nullptr, await_options));
    io_task_awaiter_.erase(awaiter_iter);
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type router_object_base::await_io_task(rpc::context &ctx, task_type_trait::task_type &other_task) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");
  if (task_type_trait::empty(other_task)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  if (ctx.get_task_context().task_id == task_type_trait::get_task_id(io_task_)) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(await_io_task(ctx)));
  }

  rpc::result_code_type::value_type ret = 0;
  while (true) {
    ret = task_manager::convert_task_status_to_error_code(TASK_COMPAT_GET_CURRENT_STATUS());
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

rpc::result_code_type router_object_base::pull_cache_inner(rpc::context &ctx, void *priv_data) {
  // 触发拉取缓存时要取消移除缓存的计划任务
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  auto router_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "router_object.pull_cache", [router_ptr, priv_data](rpc::context &child_ctx) -> rpc::result_code_type {
        // 先等待之前的任务完成再设置flag
        flag_guard fg(*router_ptr, flag_t::EN_ROFT_PULLING_CACHE);

        rpc::result_code_type::value_type ret =
            RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(child_ctx));
        if (ret < 0) {
          RPC_RETURN_CODE(ret);
        }

        ret = RPC_AWAIT_CODE_RESULT(router_ptr->pull_cache(child_ctx, priv_data));
        router_ptr->wakeup_io_task_awaiter();

        if (ret < 0) {
          RPC_RETURN_CODE(ret);
        }

        // 拉取成功要refresh_save_time
        router_ptr->refresh_save_time();

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    RPC_RETURN_CODE(*invoke_result.get_error());
  } else {
    // 执行读任务
    io_last_pull_cache_task_id_ = task_type_trait::get_task_id(*invoke_result.get_success());
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
    if (ret >= 0) {
      ret = task_type_trait::get_result(*invoke_result.get_success());
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type router_object_base::pull_object_inner(rpc::context &ctx, void *priv_data) {
  // 触发拉取实体时要取消移除缓存和降级的计划任务
  unset_flag(flag_t::EN_ROFT_FORCE_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
  unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  // 其他任务中已经拉取成功并已经升级为实体且未失效，直接视为成功
  if (is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_SUCCESS);
  }

  auto router_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "router_object.pull_object", [router_ptr, priv_data](rpc::context &child_ctx) -> rpc::result_code_type {
        // 先等待之前的任务完成再设置flag
        flag_guard fg(*router_ptr, flag_t::EN_ROFT_PULLING_OBJECT);

        router_ptr->unset_flag(flag_t::EN_ROFT_CACHE_REMOVED);
        router_ptr->unset_flag(flag_t::EN_ROFT_FORCE_PULL_OBJECT);

        // 执行读任务
        auto ret = RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(child_ctx));
        if (ret < 0) {
          router_ptr->wakeup_io_task_awaiter();
          RPC_RETURN_CODE(ret);
        }

        ret = RPC_AWAIT_CODE_RESULT(router_ptr->pull_object(child_ctx, priv_data));
        router_ptr->wakeup_io_task_awaiter();

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

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    RPC_RETURN_CODE(*invoke_result.get_error());
  } else {
    // 执行读任务
    io_last_pull_object_task_id_ = task_type_trait::get_task_id(*invoke_result.get_success());
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
    if (ret >= 0) {
      ret = task_type_trait::get_result(*invoke_result.get_success());
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type router_object_base::save_object_inner(rpc::context &ctx, void *priv_data) {
  unset_flag(flag_t::EN_ROFT_SCHED_SAVE_OBJECT);

  // 排队写任务和并发写merge
  uint64_t this_saving_seq = ++saving_sequence_;

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  // 如果有其他任务正在保存，需要等待那个任务结束
  auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
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

  auto router_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "router_object.save_object", [router_ptr, priv_data](rpc::context &child_ctx) -> rpc::result_code_type {
        // 先等待之前的任务完成再设置flag
        flag_guard fg(*router_ptr, flag_t::EN_ROFT_SAVING);

        uint64_t real_saving_seq = router_ptr->saving_sequence_;
        auto ret = RPC_AWAIT_CODE_RESULT(router_ptr->await_io_schedule_order_task(child_ctx));
        if (ret < 0) {
          RPC_RETURN_CODE(ret);
        }
        ret = RPC_AWAIT_CODE_RESULT(router_ptr->save_object(child_ctx, priv_data));
        router_ptr->wakeup_io_task_awaiter();

        if (ret >= 0 && real_saving_seq > router_ptr->saved_sequence_) {
          router_ptr->saved_sequence_ = real_saving_seq;
        } else if (ret < 0) {
          // 保存失败
          RPC_RETURN_CODE(ret);
        }

        router_ptr->refresh_save_time();

        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    RPC_RETURN_CODE(*invoke_result.get_error());
  } else {
    // 执行写任务
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = *invoke_result.get_success();
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
    if (ret >= 0) {
      ret = task_type_trait::get_result(*invoke_result.get_success());
    }
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

    auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, task));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    io_schedule_order_.erase(select_task_id);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}
