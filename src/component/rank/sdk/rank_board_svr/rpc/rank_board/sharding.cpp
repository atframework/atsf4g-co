#include "sharding.h"

#include <config/extern_service_types.h>
#include <logic/logic_server_setup.h>
#include <utility/random_engine.h>
#include <xxhash.h>
#include "common.h"
#include "config/extern_service_types.h"
#include "config/server_frame_build_feature.h"
#include "log/log_wrapper.h"
#include "rpc/rank_board/rankboardservice.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_context.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

PROJECT_NAMESPACE_BEGIN

namespace rank_api {
namespace inner {

struct router_rank_info {
  bool is_io_task_running() {
    if (!task_type_trait::empty(io_task)) {
      if (task_type_trait::is_exiting(io_task)) {
        task_type_trait::reset_task(io_task);
      }
    }

    return !task_type_trait::empty(io_task);
  }

  uint64_t main_node_server_id = 0;
  std::vector<uint64_t> router_slave_node_server_ids;
  uint64_t router_version = 0;
  task_type_trait::task_type io_task;
};

class router_rank_manager : public util::design_pattern::singleton<router_rank_manager> {
 public:
  uint64_t get_rank_main_server_id(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                   const PROJECT_NAMESPACE_ID::DRankKey& rank_key) {
    return get_ranksvr_server_id_by_consistent_hash(
        static_cast<uint32_t>(::PROJECT_NAMESPACE_ID::rank_api::rank_key_hash_type()(rank_key)));
  }

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_rank_slave_server_ids(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, std::vector<uint64_t>& slave_server_ids) {
    const router_rank_info* router_info = nullptr;
    auto ret = RPC_AWAIT_CODE_RESULT(get_rank_server_router_info(ctx, rank_key, &router_info));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
    if (router_info == nullptr) {
      FWLOGERROR("not found router info rank_id ({}:{}:{}:{})", rank_key.rank_type(), rank_key.rank_instance_id(),
                 rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }
    if (router_info->router_slave_node_server_ids.empty()) {
      slave_server_ids.push_back(router_info->main_node_server_id);
    } else {
      slave_server_ids = router_info->router_slave_node_server_ids;
    }
    RPC_RETURN_CODE(0);
  }

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_rank_slave_server_random(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, uint64_t& slave_server_id) {
    std::vector<uint64_t> slave_server_ids;
    auto ret = RPC_AWAIT_CODE_RESULT(get_rank_slave_server_ids(ctx, rank_key, slave_server_ids));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
    if (slave_server_ids.empty()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SLAVE_SERVER_EMPTY);
    }
    size_t idx = util::random_engine::fast_random_between<size_t>(0, slave_server_ids.size());
    slave_server_id = slave_server_ids[idx];
    RPC_RETURN_CODE(0);
  }

 private:
  uint64_t get_ranksvr_server_id_by_consistent_hash(uint32_t rank_id) {
    if (auto common_mod = logic_server_last_common_module()) {
      auto type_index = common_mod->get_discovery_index_by_type(
          static_cast<uint64_t>(atframework::component::logic_service_type::kRankSvr));
      const atfw::atapp::protocol::atapp_metadata* selector =
          logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kRanksvrFieldNumber,
                                     logic_hpa_discovery_select_mode::kReady);
      if (type_index) {
        auto node = type_index->get_node_by_consistent_hash(int64_t(rank_id), selector);
        if (node) {
          return node->get_discovery_info().id();
        }
      }
    }
    return 0;
  }

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type get_rank_server_router_info(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, const router_rank_info** output_router_info) {
    if (output_router_info == nullptr) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }

    size_t hash_key = ::PROJECT_NAMESPACE_ID::rank_api::rank_key_hash_type()(rank_key);
    auto& router_info = router_rank_data[hash_key];
    if (router_info.is_io_task_running()) {
      auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, router_info.io_task));
      if (ret != 0) {
        RPC_RETURN_CODE(ret);
      }
      *output_router_info = &router_rank_data[hash_key];
      RPC_RETURN_CODE(0);
    }
    if (router_info.router_version > 0) {
      *output_router_info = &router_info;
      RPC_RETURN_CODE(0);
    }

    // 首次通过一致性hash
    auto server_id = get_ranksvr_server_id_by_consistent_hash(static_cast<uint32_t>(hash_key));
    if (server_id <= 0) {
      FWLOGERROR("no ranksvr available rank_id ({}:{}:{}:{}), has_key: {}", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), hash_key);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVICE_NOT_AVAILABLE);
    }

    auto invoke_task = rpc::async_invoke(
        ctx, "rank load main task", [server_id, hash_key, rank_key](rpc::context& child_ctx) -> rpc::result_code_type {
          PROJECT_NAMESPACE_ID::SSRankLoadMainReq request_body;
          PROJECT_NAMESPACE_ID::SSRankLoadMainRsp response_body;
          request_body.mutable_rank_key()->set_rank_type(rank_key.rank_type());
          request_body.mutable_rank_key()->set_rank_instance_id(rank_key.rank_instance_id());
          request_body.mutable_rank_key()->set_sub_rank_type(rank_key.sub_rank_type());
          request_body.mutable_rank_key()->set_sub_rank_instance_id(rank_key.sub_rank_instance_id());

          auto ret =
              RPC_AWAIT_CODE_RESULT(rpc::rank_board::rank_load_main(child_ctx, server_id, request_body, response_body));
          if (ret != 0) {
            RPC_RETURN_CODE(ret);
          }
          auto& tmp_router_info = router_rank_manager::me()->router_rank_data[hash_key];
          tmp_router_info.main_node_server_id = response_body.router_data().main_server_id();
          tmp_router_info.router_version = response_body.router_data().router_version();
          for (auto& slave_server_id : response_body.router_data().slave_server_ids()) {
            tmp_router_info.router_slave_node_server_ids.push_back(slave_server_id);
          }
          RPC_RETURN_CODE(0);
        });
    if (invoke_task.is_error()) {
      FWLOGERROR("rank load main task invoke failed rank_id:({}:{}:{}:{}) server:{}", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), server_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    router_info.io_task = *invoke_task.get_success();
    auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, router_info.io_task));
    if (ret != 0) {
      FWLOGERROR("rank load main task excute failed rank_id:({}:{}:{}:{}) server:{} ret:{}", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), server_id,
                 ret);
      RPC_RETURN_CODE(ret);
    }
    *output_router_info = &router_info;
    RPC_RETURN_CODE(0);
  }

 private:
  std::unordered_map<size_t, router_rank_info> router_rank_data;
};
}  // namespace inner

RANK_BOARD_SDK_API uint64_t get_rank_main_server_id(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key) {
  return inner::router_rank_manager::me()->get_rank_main_server_id(ctx, rank_key);
}

EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_rank_slave_server_ids(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, std::vector<uint64_t>& slave_server_ids) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      inner::router_rank_manager::me()->get_rank_slave_server_ids(ctx, rank_key, slave_server_ids)));
}

EXPLICIT_NODISCARD_ATTR RANK_BOARD_SDK_API rpc::result_code_type get_rank_slave_server_random(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, uint64_t& slave_server_id) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      inner::router_rank_manager::me()->get_rank_slave_server_random(ctx, rank_key, slave_server_id)));
}

}  // namespace rank_api

PROJECT_NAMESPACE_END