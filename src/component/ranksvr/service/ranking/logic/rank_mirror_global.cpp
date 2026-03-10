#include <logic/rank_mirror_global.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <rpc/db/local_db_interface.h>
#include "log/log_wrapper.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"

#include <logic/rank_manager.h>

int rank_mirror_global::init() { return 0; }
void rank_mirror_global::tick() {
  rpc::context ctx{rpc::context::create_without_task()};
  async_tick_dump(ctx);
}

void rank_mirror_global::add_failed_task(const dump_mirror_task_ptr& task) {
  // 失败的任务重新放到尾部重新执行
  if (!task || !task->mirror_ptr_) {
    return;
  }
  // 重置部分数据
  task->cur_rank_no_ = 0;
  task->cur_slice_index_ = 1;
  task->iter_ = task->mirror_ptr_->begin();

  task_list_.push_back(task);
}

void rank_mirror_global::add_dump_task(const dump_mirror_task_ptr& task) {
  if (task_list_.size() >= logic_config::me()
                                   ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>()
                                   .rank_dunmp_task_max_num() +
                               1) {
    FWLOGWARNING("rank_mirror_global.add_dump_task failed, task_list_ size:{} reach max num", task_list_.size());
  }
  if (!task) {
    return;
  }
  task_list_.push_back(task);
}

bool rank_mirror_global::is_dump_task_running() {
  if (!task_type_trait::empty(dump_task_)) {
    if (task_type_trait::is_exiting(dump_task_)) {
      task_type_trait::reset_task(dump_task_);
    }
  }

  return !task_type_trait::empty(dump_task_);
}

void rank_mirror_global::async_tick_dump(rpc::context& ctx) {
  if (task_list_.empty() || is_dump_task_running()) {
    return;
  }
  auto invoke_task = rpc::async_invoke(ctx, "rank_mirror_global.async_tick_dump",
                                       [](rpc::context& child_ctx) -> rpc::result_code_type {
                                         RPC_AWAIT_IGNORE_RESULT(rank_mirror_global::me()->tick_dump(child_ctx));
                                         RPC_RETURN_CODE(0);
                                       });
  if (invoke_task.is_error()) {
    FWLOGERROR("rank_mirror_global.async_tick_dump invoke_task failed");
  }
  if (invoke_task.is_success()) {
    dump_task_ = std::move(*invoke_task.get_success());
  }
}

rpc::result_code_type rank_mirror_global::tick_dump(rpc::context& ctx) {
  if (task_list_.empty()) {
    RPC_RETURN_CODE(0);
  }

  int32_t tick_io_max = 5;
  auto rank_per_slice_max_count = logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>().rank_slice_max_count();
  if (rank_per_slice_max_count <= 0) {
    rank_per_slice_max_count = 1;
  }
  FWLOGDEBUG("rank_mirror_global.tick_dump task_list size:{} rank_per_slice_max_count:{}", task_list_.size(),
             rank_per_slice_max_count);

  int32_t cur_io_num = 0;
  std::list<dump_mirror_task_ptr> failed_task_list;
  while (!task_list_.empty()) {
    auto task = task_list_.front();
    if (!task || !task->mirror_ptr_) {
      task_list_.pop_front();
      continue;
    }

    int ret = 0;
    auto tmp = task->total_rank_size_ % rank_per_slice_max_count;
    auto max_slice_count = tmp == 0 ? task->total_rank_size_ / rank_per_slice_max_count
                                    : task->total_rank_size_ / rank_per_slice_max_count + 1;
    while (task->cur_slice_index_ <= max_slice_count && cur_io_num < tick_io_max) {
      auto begin_rank_no = task->cur_rank_no_;

      rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror> db_data;
      db_data->set_rank_type(task->rank_key_.rank_type());
      db_data->set_rank_instance_id(task->rank_key_.rank_instance_id());
      db_data->set_sub_rank_type(task->rank_key_.sub_rank_type());
      db_data->set_sub_rank_instance_id(task->rank_key_.sub_rank_instance_id());
      db_data->set_zone_id(logic_config::me()->get_local_zone_id());
      db_data->set_mirror_id(task->mirror_id_);
      db_data->set_slice_index(task->cur_slice_index_);
      db_data->set_max_slice_count(max_slice_count);
      db_data->set_per_slice_count(rank_per_slice_max_count);
      db_data->set_rank_total_size(task->total_rank_size_);
      db_data->set_data_version(task->data_version_);

      while ((task->cur_rank_no_ - begin_rank_no < rank_per_slice_max_count) &&
             task->iter_ != task->mirror_ptr_->end()) {
        auto rank_info = db_data->mutable_blob_data()->mutable_data()->Add();
        if (rank_info) {
          rank_info->set_rank_no(static_cast<uint32_t>(++task->cur_rank_no_));
          protobuf_copy_message(*rank_info->mutable_data()->mutable_sort_data(), *task->iter_);
        }
        task->iter_++;
      }

      cur_io_num++;
      ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror::replace(ctx, std::move(db_data)));
      if (ret == 0) {
        FWLOGDEBUG("({}:{}:{}:{}) mirror_id:{} max_slice:{} cur_slice:{} rank_mirror_global.tick_dump replace success",
                   task->rank_key_.rank_type(), task->rank_key_.rank_instance_id(), task->rank_key_.sub_rank_type(),
                   task->rank_key_.sub_rank_instance_id(), task->mirror_id_, max_slice_count, task->cur_slice_index_);
      } else {
        FWLOGERROR("({}:{}:{}:{}) mirror_id:{} max_slice:{} cur_slice:{} rank_mirror_global.tick_dump replace error:{}",
                   task->rank_key_.rank_type(), task->rank_key_.rank_instance_id(), task->rank_key_.sub_rank_type(),
                   task->rank_key_.sub_rank_instance_id(), task->mirror_id_, max_slice_count, task->cur_slice_index_,
                   ret);
      }

      task->cur_slice_index_++;
      if (ret != 0) {
        break;
      }
    }
    if (ret == 0 && task->cur_slice_index_ <= max_slice_count) {
      // 下一次tick继续
      break;
    }
    task_list_.pop_front();
    bool dump_success = ret == 0;
    if (!dump_success) {
      add_failed_task(task);
    }
    auto rank = rank_manager::me()->get_rank(task->rank_key_);
    if (rank && rank->is_main_node()) {
      if (dump_success) {
        RPC_AWAIT_IGNORE_RESULT(rank->get_mirror_manager()->dump_mirror_success(ctx, task));
      }
      FWLOGINFO("({}:{}:{}:{}) mirror_id:{} dump mirror finish:{}", task->rank_key_.rank_type(),
                task->rank_key_.rank_instance_id(), task->rank_key_.sub_rank_type(),
                task->rank_key_.sub_rank_instance_id(), task->mirror_id_, dump_success ? "true" : "false");
    } else {
      // 尽量让这种情况不出现。有镜像dump未完成的排行榜主节点不让迁移
      FWLOGERROR("({},{}) mirror_id:{} dump mirror finish:{} but is not main", task->rank_key_.rank_type(),
                 task->rank_key_.rank_instance_id(), task->mirror_id_, dump_success ? "true" : "false");
    }
  }
  for (auto& task : failed_task_list) {
    add_failed_task(task);
  }

  RPC_RETURN_CODE(0);
}