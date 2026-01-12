// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#include "logic/async_jobs/task_action_player_remote_patch_jobs.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>
#include <data/session.h>
#include <router/router_player_cache.h>
#include <router/router_player_manager.h>

#include <dispatcher/task_manager.h>
#include <logic/async_jobs/user_async_jobs_manager.h>

#include <rpc/db/async_jobs.h>

#include <string>
#include <utility>
#include <vector>

task_action_player_remote_patch_jobs::task_action_player_remote_patch_jobs(ctor_param_t&& param)
    : task_action_no_req_base(param),
      param_(param),
      need_restart_(false),
      is_writable_(false),
      patched_job_number_(0) {}

task_action_player_remote_patch_jobs::~task_action_player_remote_patch_jobs() {}

static rpc::result_code_type save_player_data(rpc::context& ctx, router_player_cache::ptr_t& cache,
                                              size_t batch_job_number, std::vector<int64_t>& complete_jobs_idx,
                                              int job_type, uint64_t user_id, uint32_t zone_id,
                                              const std::string& openid) {
  if (!cache || !cache->is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  int ret = 0;
  // 保存玩家数据
  if (batch_job_number > 0) {
    ret = RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr));
    if (ret < 0) {
      // 这里可能是因为保存过程城中下线了，这时候直接放弃执行即可。下次登入后会继续执行的
      if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE == ret) {
        FWLOGWARNING("save player {}({}:{}) failed, res: {}({})", openid, zone_id, user_id, ret,
                     protobuf_mini_dumper_get_error_msg(ret));
      } else {
        FWLOGERROR("save player {}({}:{}) failed, res: {}({})", openid, zone_id, user_id, ret,
                   protobuf_mini_dumper_get_error_msg(ret));
      }
      RPC_RETURN_CODE(ret);
    }
  }

  if (!complete_jobs_idx.empty()) {
    // 移除远程命令
    ret = RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::del_jobs(ctx, job_type, user_id, zone_id, complete_jobs_idx));
    if (ret < 0) {
      FWLOGERROR("delete async jobs for player {}({}:{}) failed, res: {}({})", openid, zone_id, user_id, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
      RPC_RETURN_CODE(ret);
    }

    complete_jobs_idx.clear();
  }

  RPC_RETURN_CODE(ret);
}

task_action_player_remote_patch_jobs::result_type task_action_player_remote_patch_jobs::operator()() {
  need_restart_ = false;
  is_writable_ = false;
  if (!param_.user) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  set_user_key(param_.user->get_user_id(), param_.user->get_zone_id());
  param_.user->refresh_feature_limit(get_shared_context());

  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), param_.user->get_zone_id(),
                                 param_.user->get_user_id());
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  // 缓存已被移除，当前player可能是上下文缓存，忽略patch
  if (!cache) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 缓存已被踢出，当前player是路由缓存，忽略patch
  if (!cache->is_writable()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 注意这里会续期缓存生命周期，所以要确保前面判定都过后才能到这里
  if (!cache->is_object_equal(param_.user)) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  is_writable_ = true;
  need_restart_ = false;
  patched_job_number_ = 0;
  bool pending_to_logout = false;

  /**
   * 一致性：
   *     执行远程命令patch期间可能触发下线/踢出流程。所以所有的操作需要带唯一ID，如果已经执行过则要忽略。
   *     需要再保存玩家数据成功后才能移除patch队列。
   *
   * 重试和超时保护：
   *     如果一次执行的任务过多，可能当前任务会超时，所以需要检测到运行了很长时间后中断退出，并重启一个任务继续后续流程。
   *     出错的流程不应该重启任务，而是放进队列尾等待后续重试，否则某些服务故障期间可能会导致无限循环。
   */

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  const ::google::protobuf::EnumDescriptor* desc = PROJECT_NAMESPACE_ID::EnPlayerAsyncJobsType_descriptor();
  for (int pull_jobs_idx = 0; desc && is_writable_ && !pending_to_logout && pull_jobs_idx < desc->value_count() &&
                              param_.user->is_inited() && false == need_restart_;
       ++pull_jobs_idx) {
    const ::google::protobuf::EnumValueDescriptor* val_desc = desc->value(pull_jobs_idx);
    if (NULL == val_desc) {
      continue;
    }

    // 忽略列表
    if (0 == val_desc->number()) {
      continue;
    }

    if (!param_.async_job_type.empty() &&
        param_.async_job_type.find(val_desc->number()) == param_.async_job_type.end()) {
      continue;
    }

    FWPLOGDEBUG(*param_.user, "task_action_player_remote_patch_jobs load from get_jobs type {}", val_desc->number());

    std::vector<rpc::db::async_jobs::async_jobs_record> job_list;
    std::vector<int64_t> complete_jobs_idx;

    ret = RPC_AWAIT_CODE_RESULT(rpc::db::async_jobs::get_jobs(
        get_shared_context(), val_desc->number(), param_.user->get_user_id(), param_.user->get_zone_id(), job_list));
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      param_.user->get_user_async_jobs_manager().clear_job_uuids(val_desc->number());
      ret = 0;
      continue;
    }

    complete_jobs_idx.reserve(PROJECT_NAMESPACE_ID::EN_SL_PLAYER_ASYNC_JOBS_BATCH_NUMBER);
    size_t batch_job_number = 0;

    // 优先执行数据库存量
    for (size_t i = 0; i < job_list.size() && !pending_to_logout && false == need_restart_; ++i) {
      // 如果拉取完玩家下线了，中断后续任务
      is_writable_ = cache->is_writable();
      if (!is_writable_) {
        break;
      }

      complete_jobs_idx.push_back(job_list[i].record_index);

      if (param_.user->get_user_async_jobs_manager().is_job_uuid_exists(val_desc->number(),
                                                                        job_list[i].action_blob->action_uuid())) {
        // 已执行则跳过
        continue;
      }
      param_.user->get_user_async_jobs_manager().add_job_uuid(val_desc->number(),
                                                              job_list[i].action_blob->action_uuid());

      async_job_ptr_type job_data_ptr = job_list[i].action_blob.share_instance();

      ++batch_job_number;
      int async_job_res = do_job(val_desc->number(), job_data_ptr);
      if (async_job_res < 0) {
        FWPLOGERROR(*param_.user, "do async action {}, msg: {} failed, res: {}({})",
                    static_cast<int>(job_data_ptr->action_case()), job_data_ptr->DebugString(), async_job_res,
                    protobuf_mini_dumper_get_error_msg(async_job_res));

        // 添加重试队列
        if (job_data_ptr->left_retry_times() > 0) {
          param_.user->get_user_async_jobs_manager().add_retry_job(val_desc->number(), job_data_ptr);
        } else {
          param_.user->get_user_async_jobs_manager().remove_retry_job(val_desc->number(), job_data_ptr->action_uuid());
        }
      } else {
        // 移除重试队列
        param_.user->get_user_async_jobs_manager().remove_retry_job(val_desc->number(), job_data_ptr->action_uuid());
      }

      if (batch_job_number >= PROJECT_NAMESPACE_ID::EN_SL_PLAYER_ASYNC_JOBS_BATCH_NUMBER) {
        // 如果拉取完玩家下线了，中断后续任务
        is_writable_ = cache->is_writable();
        if (!is_writable_) {
          break;
        }

        if (!sub_tasks_.empty() && PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT != ret &&
            PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT != ret) {
          auto await_res = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), sub_tasks_));
          if (await_res < 0) {
            if (ret > 0) {
              ret = await_res;
            }
            break;
          }
          sub_tasks_.clear();
        }

        // 保存玩家数据
        ret = RPC_AWAIT_CODE_RESULT(save_player_data(get_shared_context(), cache, batch_job_number, complete_jobs_idx,
                                                     val_desc->number(), param_.user->get_user_id(),
                                                     param_.user->get_zone_id(), param_.user->get_open_id()));
        if (ret < 0) {
          break;
        }

        patched_job_number_ += batch_job_number;
        batch_job_number = 0;
        param_.user->get_user_async_jobs_manager().clear_job_uuids(val_desc->number());
      }
    }

    // 再执行retry队列
    for (auto& job_data_ptr : param_.user->get_user_async_jobs_manager().get_retry_jobs(val_desc->number())) {
      if (pending_to_logout || need_restart_) {
        break;
      }
      // 如果拉取完玩家下线了，中断后续任务
      is_writable_ = cache->is_writable();
      if (!is_writable_) {
        break;
      }

      if (!job_data_ptr) {
        continue;
      }

      ++batch_job_number;
      int async_job_res = do_job(val_desc->number(), job_data_ptr);
      if (async_job_res < 0) {
        FWPLOGERROR(*param_.user, "do async action {}, msg: {} failed, res: {}({})",
                    static_cast<int>(job_data_ptr->action_case()), job_data_ptr->DebugString(), async_job_res,
                    protobuf_mini_dumper_get_error_msg(async_job_res));

        // 添加重试队列
        if (job_data_ptr->left_retry_times() > 0) {
          param_.user->get_user_async_jobs_manager().add_retry_job(val_desc->number(), job_data_ptr);
        } else {
          param_.user->get_user_async_jobs_manager().remove_retry_job(val_desc->number(), job_data_ptr->action_uuid());
        }
      } else {
        // 移除重试队列
        param_.user->get_user_async_jobs_manager().remove_retry_job(val_desc->number(), job_data_ptr->action_uuid());
      }

      if (batch_job_number >= PROJECT_NAMESPACE_ID::EN_SL_PLAYER_ASYNC_JOBS_BATCH_NUMBER) {
        // 如果拉取完玩家下线了，中断后续任务
        is_writable_ = cache->is_writable();
        if (!is_writable_) {
          break;
        }

        if (!sub_tasks_.empty() && PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT != ret &&
            PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT != ret) {
          auto await_res = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), sub_tasks_));
          if (await_res < 0) {
            if (ret > 0) {
              ret = await_res;
            }
            break;
          }
          sub_tasks_.clear();
        }

        // 保存玩家数据
        ret = RPC_AWAIT_CODE_RESULT(save_player_data(get_shared_context(), cache, batch_job_number, complete_jobs_idx,
                                                     val_desc->number(), param_.user->get_user_id(),
                                                     param_.user->get_zone_id(), param_.user->get_open_id()));
        if (ret < 0) {
          break;
        }

        patched_job_number_ += batch_job_number;
        batch_job_number = 0;
        param_.user->get_user_async_jobs_manager().clear_job_uuids(val_desc->number());
      }
    }

    if (ret < 0) {
      break;
    }

    // 如果拉取完玩家下线了，中断后续任务
    is_writable_ = cache->is_writable();
    if (!is_writable_) {
      break;
    }

    if (!sub_tasks_.empty() && PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT != ret &&
        PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT != ret) {
      auto await_res = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(get_shared_context(), sub_tasks_));
      if (await_res < 0) {
        if (ret > 0) {
          ret = await_res;
        }
        break;
      }
      sub_tasks_.clear();
    }

    if (batch_job_number > 0 || !complete_jobs_idx.empty()) {
      ret = RPC_AWAIT_CODE_RESULT(save_player_data(get_shared_context(), cache, batch_job_number, complete_jobs_idx,
                                                   val_desc->number(), param_.user->get_user_id(),
                                                   param_.user->get_zone_id(), param_.user->get_open_id()));
      if (ret < 0) {
        break;
      }

      patched_job_number_ += batch_job_number;
      batch_job_number = 0;
      param_.user->get_user_async_jobs_manager().clear_job_uuids(val_desc->number());

      // 如果对象被踢出（不可写），则放弃后续流程
      is_writable_ = cache->is_writable();
      if (!is_writable_) {
        break;
      }
    }

    // 执行时间过长则中断，下一次再启动流程
    need_restart_ = (param_.timeout_timepoint - atfw::util::time::time_utility::now()) < param_.timeout_duration / 2;

    // 如果玩家离线和正在准备登出则停止异步任务流程，下次登入再继续
    {
      session::ptr_t s = param_.user->get_session();
      if (!s) {
        pending_to_logout = true;
      } else if (s->check_flag(session::flag_t::EN_SESSION_FLAG_CLOSING) ||
                 s->check_flag(session::flag_t::EN_SESSION_FLAG_CLOSED)) {
        pending_to_logout = true;
      }
    }
  }

  // 可能是从中间中断的，需要重新计算一次是否可写
  is_writable_ = cache->is_writable();

  if (pending_to_logout) {
    need_restart_ = false;
  }

  // 如果是执行过程中玩家对象离线导致不可写，直接跳过即可，前面会打印warning日志，不需要输出错误
  if (PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE == ret) {
    ret = 0;
  }
  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_player_remote_patch_jobs::on_success() {
  if (param_.user) {
    FWPLOGDEBUG(*param_.user, "do {} success", "task_action_player_remote_patch_jobs");
  }

  // 尝试再启动一次，启动排队后的任务
  if (is_writable_ && param_.user) {
    if (task_type_trait::get_task_id(param_.user->get_user_async_jobs_manager().remote_command_patch_task_) ==
        get_task_id()) {
      task_type_trait::reset_task(param_.user->get_user_async_jobs_manager().remote_command_patch_task_);
    }

    if (need_restart_) {
      param_.user->get_user_async_jobs_manager().reset_async_jobs_protect();
    }

    rpc::context new_ctx{rpc::context::create_without_task()};
    rpc::telemetry::tracer new_tracer;
    rpc::telemetry::trace_start_option trace_start_option;
    trace_start_option.dispatcher = nullptr;
    trace_start_option.is_remote = true;
    trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

    new_ctx.setup_tracer(new_tracer, "task_action_player_remote_patch_jobs.on_success", std::move(trace_start_option));
    param_.user->get_user_async_jobs_manager().try_async_jobs(new_ctx);

    new_tracer.finish({0, {}});
  }

  if (patched_job_number_ > 0) {
    // 数据变更推送
    param_.user->send_all_syn_msg(get_shared_context());
  }

  return get_result();
}

int task_action_player_remote_patch_jobs::on_failed() {
  if (param_.user) {
    FWPLOGERROR(*param_.user, "do task_action_player_remote_patch_jobs failed, res: {}", get_result());
  }

  // 尝试再启动一次，启动排队后的任务
  if (is_writable_ && param_.user) {
    if (task_type_trait::get_task_id(param_.user->get_user_async_jobs_manager().remote_command_patch_task_) ==
        get_task_id()) {
      task_type_trait::reset_task(param_.user->get_user_async_jobs_manager().remote_command_patch_task_);
    }

    if (need_restart_) {
      param_.user->get_user_async_jobs_manager().reset_async_jobs_protect();
    }

    rpc::context new_ctx{rpc::context::create_without_task()};
    rpc::telemetry::tracer new_tracer;
    rpc::telemetry::trace_start_option trace_start_option;
    trace_start_option.dispatcher = nullptr;
    trace_start_option.is_remote = true;
    trace_start_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

    new_ctx.setup_tracer(new_tracer, "task_action_player_remote_patch_jobs.on_failed", std::move(trace_start_option));
    param_.user->get_user_async_jobs_manager().try_async_jobs(new_ctx);

    new_tracer.finish({0, {}});
  }

  if (patched_job_number_ > 0) {
    // 数据变更推送
    param_.user->send_all_syn_msg(get_shared_context());
  }

  return get_result();
}

void task_action_player_remote_patch_jobs::register_callbacks(
    std::unordered_map<int32_t, sync_callback_type>& sync_callbacks,
    std::unordered_map<int32_t, async_callback_type>& /*async_callbacks*/) {
  sync_callbacks[static_cast<int32_t>(PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::kDebugMessage)] =
      [](task_action_player_remote_patch_jobs& /*task_action_inst*/, player& user, int32_t /*job_type*/,
         async_job_ptr_type job_data) -> int32_t {
    FWPLOGINFO(user, "[TODO] do async action {}, message: {}", static_cast<int32_t>(job_data->action_case()),
               job_data->DebugString());
    return 0;
  };
}

int32_t task_action_player_remote_patch_jobs::do_job(int32_t job_type, const async_job_ptr_type& job_data) {
  if (!job_data) {
    return 0;
  }

  static std::unordered_map<int32_t, sync_callback_type> sync_callbacks;
  static std::unordered_map<int32_t, async_callback_type> async_callbacks;

  if (sync_callbacks.empty() && async_callbacks.empty()) {
    register_callbacks(sync_callbacks, async_callbacks);
  }

  auto iter_sync = sync_callbacks.find(static_cast<int32_t>(job_data->action_case()));
  if (iter_sync != sync_callbacks.end() && iter_sync->second) {
    return iter_sync->second(*this, *param_.user, job_type, job_data);
  }

  auto iter_async = async_callbacks.find(static_cast<int32_t>(job_data->action_case()));
  if (iter_async != async_callbacks.end() && iter_async->second) {
    auto fds = PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::descriptor()->FindFieldByNumber(
        static_cast<int32_t>(job_data->action_case()));
    std::string sub_job_name;
    if (nullptr == fds) {
      sub_job_name = atfw::util::log::format("task_action_player_remote_patch_jobs.{}",
                                             static_cast<int32_t>(job_data->action_case()));
    } else {
      sub_job_name = atfw::util::log::format("task_action_player_remote_patch_jobs.{}", fds->name());
    }
    auto fn = iter_async->second;
    auto user_ptr = param_.user;
    auto ret = rpc::async_invoke(get_shared_context(), sub_job_name,
                                 [user_ptr, job_type, job_data, fn](rpc::context& child_ctx) -> rpc::result_code_type {
                                   RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(fn(child_ctx, *user_ptr, job_type, job_data)));
                                 });

    if (ret.is_error()) {
      return *ret.get_error();
    } else {
      append_sub_task(*ret.get_success());
    }

    return 0;
  }

  FWPLOGERROR(*param_.user, "do invalid async action {}, message: {}", static_cast<int>(job_data->action_case()),
              job_data->DebugString());
  return 0;
}

void task_action_player_remote_patch_jobs::append_sub_task(task_type_trait::task_type task_inst) {
  if (task_type_trait::empty(task_inst) || task_type_trait::is_exiting(task_inst)) {
    return;
  }

  sub_tasks_.emplace_back(std::move(task_inst));
}
