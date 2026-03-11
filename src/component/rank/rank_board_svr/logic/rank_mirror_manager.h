#pragma once

#include <dispatcher/task_manager.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_common_types.h>

#include "config/server_frame_build_feature.h"

#include <rpc/rpc_shared_message.h>

#include <utility/persistent_btree.h>
#include <cstdint>
#include <unordered_map>
#include <utility>

#include "logic/rank_type.h"
#include "memory/rc_ptr.h"

class rank;

struct dump_mirror_task {
  rank_tree::mirror_pointer mirror_ptr_ = nullptr;
  int64_t mirror_id_ = 0;
  int32_t cur_slice_index_ = 1;  // slice index从1开始
  rank_mirror::iterator iter_;
  int64_t data_version_ = 0;
  PROJECT_NAMESPACE_ID::DRankKey rank_key_;
  int32_t total_rank_size_ = 0;
  int32_t cur_rank_no_ = 0;
  bool is_normal_save_ = false;
};
using dump_mirror_task_ptr = util::memory::strong_rc_ptr<dump_mirror_task>;

class rank_mirror_manager {
 public:
  rank_mirror_manager(rank* owner);
  ~rank_mirror_manager();

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type init_from_db(rpc::context& ctx);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_mirror(rpc::context& ctx, int64_t& mirror_id,
                                                              bool is_normal_save);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type dump_mirror_success(rpc::context& ctx, const dump_mirror_task_ptr task);

  bool check_mirror_dump_finish(int64_t mirror_id) const;

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_mirror_data_from_db(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::rank_mirror_meta_info& mirror_info,
      std::vector<atfw::util::memory::strong_rc_ptr<rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror>>>& db_data);

  const PROJECT_NAMESPACE_ID::table_rank_mirror_meta_data& get_rank_mirror_meta_data() const { return meta_data_; }

  
 private:
  std::pair<bool, int64_t> check_need_create_mirror(bool is_normal_save);

  void check_and_remove_mirror();
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type do_remove_mirror(rpc::context& ctx);

 private:
  rank* owner_ = nullptr;
  bool is_init_ = false;

  int64_t last_data_version_ = 0;  // 最后一次成功保存的镜像数据版本号
  PROJECT_NAMESPACE_ID::table_rank_mirror_meta_data meta_data_;

  std::unordered_map<int64_t, dump_mirror_task_ptr> running_mirror_map_;
};