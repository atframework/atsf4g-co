
#include "rank.h"

#include "rpc/rank/sharding.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/repeated_field.h>
#include <protocol/pbdesc/rank_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/task_manager.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>

#include <rpc/db/local_db_interface.h>
#include <utility/protobuf_mini_dumper.h>
#include <utility/rank_util.h>

#include <rpc/db/db_utils.h>
#include <rpc/rpc_common_types.h>
#include <rpc/rpc_utils.h>

#include "config/server_frame_build_feature.h"
#include "ranksvrservice.h"

namespace rpc {

namespace rank {

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_special_one(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    PROJECT_NAMESPACE_ID::DRankUserBoardData& output) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetSpecifyRankReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetSpecifyRankRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_user_key(), user);
  protobuf_copy_message(*request_body->mutable_rank_key(), rank);

  uint64_t destination_server_id = 0;
  auto ret = RPC_AWAIT_CODE_RESULT(
      PROJECT_NAMESPACE_ID::rank_api::get_rank_slave_server_random(ctx, rank, destination_server_id));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_get_special(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  protobuf_copy_message(output, response_body->rank_records());
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_top(rpc::context& ctx,
                                                                   const PROJECT_NAMESPACE_ID::DRankKey& rank,
                                                                   uint32_t start_no, uint32_t count,
                                                                   PROJECT_NAMESPACE_ID::DRankQueryRspData& output) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetTopReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetTopRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_rank_key(), rank);
  request_body->set_start_no(start_no);
  request_body->set_count(count);

  uint64_t destination_server_id = 0;
  auto ret = RPC_AWAIT_CODE_RESULT(
      PROJECT_NAMESPACE_ID::rank_api::get_rank_slave_server_random(ctx, rank, destination_server_id));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_get_top(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  protobuf_copy_message(output, *response_body->mutable_data());
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_special_one_front_back(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank, const PROJECT_NAMESPACE_ID::DRankUserKey& user,
    uint32_t count, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBoardData>& output) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetUserFrontBackReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankGetUserFrontBackRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_rank_key(), rank);
  protobuf_copy_message(*request_body->mutable_user_key(), user);
  request_body->set_count(count);

  uint64_t destination_server_id = 0;
  auto ret = RPC_AWAIT_CODE_RESULT(
      PROJECT_NAMESPACE_ID::rank_api::get_rank_slave_server_random(ctx, rank, destination_server_id));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      rpc::rank::rank_get_special_one_front_back(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  protobuf_copy_message(output, response_body->data().rank_records());
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type update_score(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    int64_t score, const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankSetScoreReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankSetScoreRsp> response_body{ctx};
  auto req_data = request_body->mutable_data();

  protobuf_copy_message(*req_data->mutable_user_key(), user);
  protobuf_copy_message(*request_body->mutable_rank_key(), rank);
  protobuf_copy_message(*req_data->mutable_custom_data(), custom_data);
  req_data->set_score(score);

  uint64_t destination_server_id = PROJECT_NAMESPACE_ID::rank_api::get_rank_main_server_id(ctx, rank);

  int32_t ret =
      RPC_AWAIT_CODE_RESULT(rpc::rank::rank_set_score(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type modify_score(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankUserKey& user, const PROJECT_NAMESPACE_ID::DRankKey& rank,
    int64_t score, const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankSetScoreReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankSetScoreRsp> response_body{ctx};
  auto req_data = request_body->mutable_data();

  protobuf_copy_message(*req_data->mutable_user_key(), user);
  protobuf_copy_message(*request_body->mutable_rank_key(), rank);
  protobuf_copy_message(*req_data->mutable_custom_data(), custom_data);
  req_data->set_score(score);

  uint64_t destination_server_id = PROJECT_NAMESPACE_ID::rank_api::get_rank_main_server_id(ctx, rank);

  int32_t ret =
      RPC_AWAIT_CODE_RESULT(rpc::rank::rank_set_score(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type remove_one(rpc::context& ctx,
                                                                      const PROJECT_NAMESPACE_ID::DRankUserKey& user,
                                                                      const PROJECT_NAMESPACE_ID::DRankKey& rank) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankDelUserReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankDelUserRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_data()->mutable_user_key(), user);
  protobuf_copy_message(*request_body->mutable_rank_key(), rank);

  uint64_t destination_server_id = PROJECT_NAMESPACE_ID::rank_api::get_rank_main_server_id(ctx, rank);

  int32_t ret =
      RPC_AWAIT_CODE_RESULT(rpc::rank::rank_del_one_user(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type clear_rank(rpc::context& ctx,
                                                                      const PROJECT_NAMESPACE_ID::DRankKey& rank) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankClearReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankClearRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_rank_key(), rank);

  uint64_t destination_server_id = PROJECT_NAMESPACE_ID::rank_api::get_rank_main_server_id(ctx, rank);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_clear(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  RPC_RETURN_CODE(response_body->result());
}

EXPLICIT_NODISCARD_ATTR RANK_RPC_API rpc::result_code_type get_top_from_mirror(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank, uint32_t zone_id, uint32_t start_no, uint32_t count,
    int64_t mirror_id, PROJECT_NAMESPACE_ID::DRankQueryRspData& output) {
  // 从镜像拉取数据，先拉取0切片获取基本数据
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror> mirror_rank{ctx};
  auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror::get_all(ctx, rank.rank_type(), rank.rank_instance_id(),
                                                                 rank.sub_rank_type(), rank.sub_rank_instance_id(),
                                                                 zone_id, mirror_id, 0, mirror_rank));
  if (ret != 0) {
    FWLOGERROR(
        "fetch_rank_total_count TABLE_RANK_MIRROR_DEF get failed rank ({}:{}:{}:{}), zone_id {}, mirror_id {}"
        "res: {}",
        rank.rank_type(), rank.rank_instance_id(), rank.sub_rank_type(), rank.sub_rank_instance_id(), zone_id,
        mirror_id, ret);

    RPC_RETURN_TYPE(ret);
  }
  output.set_rank_total_count(output.rank_total_count());

  int32_t slice_count = mirror_rank->per_slice_count();
  uint32_t end_no = start_no + count - 1;
  int32_t start_slice_index = static_cast<int32_t>((start_no - 1)) / slice_count;
  int32_t end_slice_index = static_cast<int32_t>((end_no - 1)) / slice_count;

  std::vector<rpc::db::rank_mirror::table_key_t> keys;
  std::vector<rpc::db::rank_mirror::batch_get_result_t> out;
  for (int32_t slice_index = start_slice_index; slice_index <= end_slice_index; ++slice_index) {
    keys.push_back(rpc::db::rank_mirror::table_key_t(rank.rank_type(), rank.rank_instance_id(), rank.sub_rank_type(),
                                                     rank.sub_rank_instance_id(), zone_id, mirror_id, slice_index));
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_mirror::batch_get_all(ctx, keys, out));
  if (ret != 0 || out.size() == 0) {
    FWLOGERROR("rank {}:{}, get_rank_data_from_db batch_get failed, ret {}", rank.rank_type(), rank.rank_instance_id(),
               ret);
    RPC_RETURN_CODE(ret);
  }

  for (const auto& table : out) {
    if (table.result != 0 || !table.message) {
      FWLOGERROR("rank ({}:{}:{}:{}), get_rank_data_from_db batch_get result failed, ret {}", rank.rank_type(),
                 rank.rank_instance_id(), rank.sub_rank_type(), rank.sub_rank_instance_id(), table.result);
      continue;
    }
    for (const auto& unit : table.message->get()->blob_data().data()) {
      if (unit.rank_no() >= start_no && unit.rank_no() <= end_no) {
        auto ptr = output.mutable_rank_records()->Add();
        if (ptr) {
          rank_util::dump_rank_basic_board_from_rank_data(unit.data(), *ptr);
          ptr->set_rank_no(unit.rank_no());
        }
        continue;
      }
      if (unit.rank_no() > end_no) {
        break;
      }
    }
  }
  RPC_RETURN_TYPE(0);
}

RANK_RPC_API rpc::result_code_type make_new_mirror(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank,
                                                   int64_t& mirror_id) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankMakeNewMirrorReq> request_body{ctx};
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankMakeNewMirrorRsp> response_body{ctx};

  protobuf_copy_message(*request_body->mutable_rank_key(), rank);

  uint64_t destination_server_id = PROJECT_NAMESPACE_ID::rank_api::get_rank_main_server_id(ctx, rank);

  int32_t ret =
      RPC_AWAIT_CODE_RESULT(rpc::rank::rank_make_new_mirror(ctx, destination_server_id, *request_body, *response_body));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  mirror_id = response_body->mirror_id();
  RPC_RETURN_CODE(0);
}

}  // namespace rank
}  // namespace rpc