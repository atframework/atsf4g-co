#include "logic/rank_mirror_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility>

#include <log/log_wrapper.h>
#include <rpc/db/local_db_interface.h>
#include <rpc/rpc_shared_message.h>

#include "config/logic_config.h"
#include "config/server_frame_build_feature.h"

#include "logic/rank.h"
#include "logic/rank_mirror_global.h"
#include "rpc/rpc_common_types.h"

#include "rpc/db/uuid.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_shared_message.h"
#include "utility/protobuf_mini_dumper.h"

rank_mirror_manager::rank_mirror_manager(rank* owner) : owner_(owner) {}

rank_mirror_manager::~rank_mirror_manager() {}

rpc::result_code_type rank_mirror_manager::init_from_db(rpc::context& ctx) {
  if (is_init_) {
    RPC_RETURN_CODE(0);
  }

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror_meta> out;
  auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror_meta::get_all(
      ctx, owner_->get_key().rank_type(), owner_->get_key().rank_instance_id(), owner_->get_key().sub_rank_type(),
      owner_->get_key().sub_rank_instance_id(), logic_config::me()->get_local_zone_id(), out));
  if (ret != 0 && ret != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    RPC_RETURN_CODE(ret);
  }

  if (ret == 0) {
    last_data_version_ = out->last_data_version();
    protobuf_copy_message(meta_data_, out->blob_data());
  }
  is_init_ = true;

  FWRLOGDEBUG(*owner_, "rank_mirror_manager::init_from_db success last_data_version:{}", last_data_version_);
  RPC_RETURN_CODE(0);
}

rpc::result_code_type rank_mirror_manager::get_mirror_data_from_db(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::rank_mirror_meta_info& mirror_info,
    std::vector<atfw::util::memory::strong_rc_ptr<rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror>>>&
        db_data) {
  const static int32_t rank_max_batch_get_num =
      logic_config::me()
          ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>()
          .rank_max_batch_get_num();
  // TODO 先分批拉取，后续看看是否需要移到tick中处理
  int32_t cur_batch_index = 0;
  std::vector<rpc::db::rank_mirror::table_key_t> keys;
  std::vector<rpc::db::rank_mirror::batch_get_result_t> out;
  while (++cur_batch_index <= mirror_info.max_slice_count()) {
    keys.push_back(rpc::db::rank_mirror::table_key_t(
        owner_->get_key().rank_type(), owner_->get_key().rank_instance_id(), owner_->get_key().sub_rank_type(),
        owner_->get_key().sub_rank_instance_id(), logic_config::me()->get_local_zone_id(), mirror_info.mirror_id(),
        cur_batch_index));
    if (cur_batch_index % rank_max_batch_get_num == 0) {
      auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror::batch_get_all(ctx, keys, out));
      if (ret != 0) {
        RPC_RETURN_CODE(ret);
      }
      for (auto& item : out) {
        if (item.result != 0) {
          continue;
        }
        db_data.push_back(item.message);
      }
      keys.clear();
      out.clear();
    }
  }

  RPC_RETURN_CODE(0);
}

std::pair<bool, int64_t> rank_mirror_manager::check_need_create_mirror(bool is_normal_save) {
  auto cur_data_version = owner_->get_data_version();
  if (cur_data_version == last_data_version_ && meta_data_.last_save_mirror().mirror_id() > 0) {
    if (!is_normal_save) {
      meta_data_.mutable_last_save_mirror()->set_is_normal_save(is_normal_save);
    }
    return std::make_pair(false, meta_data_.last_save_mirror().mirror_id());
  }
  auto it = running_mirror_map_.find(cur_data_version);
  if (it != running_mirror_map_.end() && it->second) {
    if (!is_normal_save) {
      // 如果是非正常保存，需要重新设置is_normal_save_，这样可以保存到success_mirror_中
      it->second->is_normal_save_ = is_normal_save;
    }
    return std::make_pair(false, it->second->mirror_id_);
  }
  return std::make_pair(true, 0);
}

/* 创建镜像
 * @param is_normal_save 是否是常规的定时保存还是镜像保存
 */
rpc::result_code_type rank_mirror_manager::create_mirror(rpc::context& ctx, int64_t& mirror_id, bool is_normal_save) {
  check_and_remove_mirror();
  // 如果跟上一次镜像的数据版本号相同，直接返回上一次的镜像id
  auto check_ret = check_need_create_mirror(is_normal_save);
  if (!check_ret.first) {
    mirror_id = check_ret.second;
    FWRLOGDEBUG(*owner_, "create rank mirror success exist mirror_id:{} data_version:{} is_normal_save:{}", mirror_id,
                owner_->get_data_version(), is_normal_save ? "true" : "false");
    RPC_RETURN_CODE(0);
  }

  auto total_rank_size = owner_->get_tree()->size();
  auto cur_data_version = owner_->get_data_version();
  auto cur_rank_mirror = owner_->get_tree()->create_mirror();

  if (!cur_rank_mirror) {
    FWRLOGERROR(*owner_, "create mirror failed, total_rank_size: {}, data_version: {}", total_rank_size,
                cur_data_version);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
  mirror_id = RPC_AWAIT_TYPE_RESULT(
      rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_RANK_MIRROR_ID));
  if (mirror_id <= 0) {
    FWRLOGERROR(*owner_, "generate global unique id failed, total_rank_size : {}, data_version: {}, mirror_id: {}",
                total_rank_size, cur_data_version, mirror_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_MIRROR_ALLOC_FAILED);
  }
  auto mirror_task = util::memory::make_strong_rc<dump_mirror_task>();
  mirror_task->mirror_ptr_ = cur_rank_mirror;
  mirror_task->mirror_id_ = mirror_id;
  mirror_task->data_version_ = cur_data_version;
  mirror_task->cur_slice_index_ = 1;
  mirror_task->iter_ = cur_rank_mirror->begin();
  protobuf_copy_message(mirror_task->rank_key_, owner_->get_key());
  mirror_task->total_rank_size_ = static_cast<int32_t>(total_rank_size);
  mirror_task->is_normal_save_ = is_normal_save;

  running_mirror_map_[cur_data_version] = mirror_task;

  rank_mirror_global::me()->add_dump_task(mirror_task);
  FWRLOGDEBUG(*owner_, "create rank mirror task success mirror_id:{} data_version:{} is_normal_save:{}", mirror_id,
              cur_data_version, is_normal_save ? "true" : "false");
  RPC_RETURN_CODE(0);
}

rpc::result_code_type rank_mirror_manager::dump_mirror_success(rpc::context& ctx, const dump_mirror_task_ptr task) {
  if (!task) {
    RPC_RETURN_CODE(0);
  }
  {
    auto it = running_mirror_map_.find(task->data_version_);
    if (it == running_mirror_map_.end() || it->second == nullptr) {
      FWRLOGERROR(*owner_, "dump mirror finish failed, mirror_id:{} not found", task->mirror_id_);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_MIRROR_NOT_FOUND);
    }
    // 删除镜像
    running_mirror_map_.erase(it);
  }
  if (meta_data_.last_save_mirror().mirror_id() > 0) {
    if (meta_data_.last_save_mirror().is_normal_save()) {
      // 直接remove
      auto mirror_meta = meta_data_.add_removing_mirror();
      protobuf_copy_message(*mirror_meta, meta_data_.last_save_mirror());
      FWRLOGDEBUG(*owner_, "add mirror to removing list mirror_id:{}", meta_data_.last_save_mirror().mirror_id());
    } else {
      // 放到success_mirror里面
      auto mirror_meta = meta_data_.add_success_mirror();
      protobuf_copy_message(*mirror_meta, meta_data_.last_save_mirror());
      FWRLOGDEBUG(*owner_, "add mirror to success list mirror_id:{}", meta_data_.last_save_mirror().mirror_id());
    }
  }

  meta_data_.mutable_last_save_mirror()->set_mirror_id(task->mirror_id_);
  meta_data_.mutable_last_save_mirror()->set_max_slice_count(task->cur_slice_index_);
  meta_data_.mutable_last_save_mirror()->set_is_normal_save(task->is_normal_save_);
  last_data_version_ = task->data_version_;

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror_meta> db_data;
  db_data->set_rank_type(owner_->get_key().rank_type());
  db_data->set_rank_instance_id(owner_->get_key().rank_instance_id());
  db_data->set_sub_rank_type(owner_->get_key().sub_rank_type());
  db_data->set_sub_rank_instance_id(owner_->get_key().sub_rank_instance_id());
  db_data->set_zone_id(logic_config::me()->get_local_zone_id());
  db_data->set_last_data_version(last_data_version_);
  protobuf_copy_message(*db_data->mutable_blob_data(), meta_data_);

  auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror_meta::replace(ctx, std::move(db_data)));
  if (ret != 0) {
    FWRLOGERROR(*owner_, "TABLE_rank_mirror_meta_DEF mirror_id:{} data_version:{} replace failed ret:{}",
                task->mirror_id_, last_data_version_, ret);
    RPC_RETURN_CODE(ret);
  }
  FWRLOGDEBUG(*owner_, "TABLE_rank_mirror_meta_DEF mirror_id:{} data_version:{} replace success", task->mirror_id_,
              last_data_version_);

  RPC_RETURN_CODE(0);
}

bool rank_mirror_manager::check_mirror_dump_finish(int64_t mirror_id) const {
  if (meta_data_.last_save_mirror().mirror_id() == mirror_id) {
    return true;
  }
  auto& success_mirror_list = meta_data_.success_mirror();
  for (auto it = success_mirror_list.rbegin(); it != success_mirror_list.rend(); ++it) {
    if (it->mirror_id() == mirror_id) {
      return true;
    }
  }
  return false;
}

void rank_mirror_manager::check_and_remove_mirror() {
  const static int32_t max_mirror_count =
      logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>().max_mirror_count();
  if (meta_data_.success_mirror_size() <= max_mirror_count) {
    return;
  }
}

rpc::result_code_type rank_mirror_manager::do_remove_mirror(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {
  RPC_RETURN_CODE(0);
}
