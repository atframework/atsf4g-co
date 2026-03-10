#include "logic/rank_manager.h"
#include "logic/rank_mirror_global.h"

#include <log/log_wrapper.h>
#include <xxhash.h>
#include "config/server_frame_build_feature.h"
#include "logic/rank.h"
#include "protocol/pbdesc/com.const.pb.h"
#include "protocol/pbdesc/com.struct.rank.pb.h"
#include "protocol/pbdesc/rank_service.pb.h"
#include "rpc/rpc_common_types.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>
#include <config/excel/config_set_ExcelRankRule.h>
#include <config/extern_service_types.h>

#include <config/logic_config.h>
#include <logic/logic_server_setup.h>
#include <rpc/db/db_utils.h>
#include <rpc/db/local_db_interface.h>
#include <time/time_utility.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include "rpc/rank/ranksvrservice.h"
#include "utility/protobuf_mini_dumper.h"

#include <atframe/etcdcli/etcd_discovery.h>
#include <logic/hpa/logic_hpa_easy_api.h>

#include <utility/random_engine.h>
#include <utility/rank_util.h>
#include <cstdint>
#include <utility>

struct rank_compare_function {
 public:
  static bool ascending_order(const PROJECT_NAMESPACE_ID::rank_sort_data& l,
                              const PROJECT_NAMESPACE_ID::rank_sort_data& r) {
    return l.value() == r.value() ? r.key() < l.key() : r.value() < l.value();
  }
  static bool descending_order(const PROJECT_NAMESPACE_ID::rank_sort_data& l,
                               const PROJECT_NAMESPACE_ID::rank_sort_data& r) {
    return l.value() == r.value() ? r.key() < l.key() : r.value() < l.value();
  }
};

size_t rank_sort_type_hash_type::operator()(const PROJECT_NAMESPACE_ID::EnRankSortType& sort_type) const {
  // TODO jijunliang 找个素数作magic number
  return static_cast<size_t>(sort_type);
}

bool rank_sort_type_equal_type::operator()(const PROJECT_NAMESPACE_ID::EnRankSortType& lhs,
                                           const PROJECT_NAMESPACE_ID::EnRankSortType& rhs) const {
  return lhs == rhs;
}

rank_manager::rank_manager() : init_(false), closing_(false) {}

void rank_manager::tick() {
  rpc::context ctx{rpc::context::create_without_task()};
  // 触发保存
  auto now_tm = util::time::time_utility::get_now();
  if (last_refresh_second_ != now_tm) {
    refresh_limit_second(ctx, now_tm);
    last_refresh_second_ = now_tm;
  }
}

int rank_manager::init() { return 0; }

void rank_manager::stop() {
  if (closing_) {
    return;
  }
  closing_ = true;

  rpc::context ctx{rpc::context::create_without_task()};
  for (auto& rank : rank_map_) {
    if (!rank.second) {
      continue;
    }
    rank.second->async_save_rank_data(ctx);
  }
}

bool rank_manager::is_closed() { return is_closing() && rank_mirror_global::me()->is_empty(); }

void rank_manager::refresh_limit_second(rpc::context& ctx, time_t now_tm) {
  // TODO 这里直接循环遍历去执行了，有一定的风险
  for (auto& rank : rank_map_) {
    if (!rank.second) {
      continue;
    }
    rank.second->refresh_limit_second(ctx, now_tm);
  }
}

rank_ptr_type rank_manager::get_rank(const PROJECT_NAMESPACE_ID::DRankKey& rank_key) const {
  auto it = rank_map_.find(rank_key);
  if (it != rank_map_.end() && it->second) {
    return it->second;
  }
  return nullptr;
}

rpc::result_code_type rank_manager::mutable_rank(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                 rank_ptr_type& out_rank) {
  {
    auto rank_ptr = get_rank(rank_key);
    if (rank_ptr) {
      out_rank = rank_ptr;
      RPC_RETURN_CODE(0);
    }
  }
  auto& rank_ptr = rank_map_[rank_key];
  if (!rank_ptr) {
    auto rank_cfg =
        excel::get_ExcelRankDefine_by_rank_type_rank_instance_id(rank_key.rank_type(), rank_key.rank_instance_id());
    if (!rank_cfg) {
      FWLOGERROR("upgrade_rank_to_main rule config not found rank({}:{}:{}:{}) not found", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_CONFIG_NOT_FOUND);
    }

    rank::compare_fn_t compare_fn = get_compare_fn(rank_cfg->sort_type());
    rank_ptr = atfw::memory::stl::make_strong_rc<rank>(rank_key, rank_cfg->capacity(), compare_fn, 0);
    FWLOGDEBUG("rank create success rank({}:{}:{}:{})", rank_key.rank_type(), rank_key.rank_instance_id(),
               rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
  }
  if (!rank_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }

  if (rank_ptr->is_io_task_running()) {
    auto ret = RPC_AWAIT_CODE_RESULT(rank_ptr->await_io_task(ctx));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  if (!rank_ptr->is_init()) {
    auto ret = RPC_AWAIT_CODE_RESULT(rank_ptr->init_rank_from_db(ctx));
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
  }
  out_rank = rank_ptr;
  RPC_RETURN_CODE(0);
}

rpc::result_code_type rank_manager::mutable_main_rank(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                      rank_ptr_type& out_rank) {
  rank_ptr_type rank_ptr = nullptr;
  auto res = RPC_AWAIT_TYPE_RESULT(mutable_rank(ctx, rank_key, rank_ptr));
  if (res != 0) {
    RPC_RETURN_CODE(res);
  }
  if (!rank_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }

  out_rank = rank_ptr;

  auto timeout = logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>().rank_server_router_lock_timeout().seconds();
  auto now_tm = util::time::time_utility::get_now();
  if (now_tm - rank_ptr->get_last_save_router_data_time() > timeout) {
    FWLOGDEBUG("cur rank router has timeout rank({}:{}:{}:{})", rank_key.rank_type(), rank_key.rank_instance_id(),
               rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
    uint64_t version = 0;
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> rank_router_rsp(ctx);
    auto ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_router::get_all(
        ctx, rank_key.rank_type(), rank_key.rank_instance_id(), rank_key.sub_rank_type(),
        rank_key.sub_rank_instance_id(), logic_config::me()->get_local_zone_id(), rank_router_rsp, version));
    FWLOGDEBUG("get rank({}:{}:{}:{}) router from db ret:{}", rank_key.rank_type(), rank_key.rank_instance_id(),
               rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), ret);
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = RPC_AWAIT_CODE_RESULT(upgrade_rank_to_main(ctx, rank_key, version));
      RPC_RETURN_CODE(ret);
    }
    if (ret != 0) {
      RPC_RETURN_CODE(ret);
    }
    if (now_tm - rank_router_rsp->router_save_timepoint() > timeout) {
      // 抢占成功
      std::pair<uint64_t, int64_t> highest_data_version_slave_node = std::make_pair(0, 0);
      RPC_AWAIT_IGNORE_RESULT(rank_manager::me()->check_slave_and_highest_data_version_slave(
          ctx, rank_key, *rank_router_rsp, highest_data_version_slave_node));
      FWLOGDEBUG("get highest slave node rank({}:{}:{}:{}) node:{} version:{}", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(),
                 highest_data_version_slave_node.first, highest_data_version_slave_node.second);
      if (rank_router_rsp->router_main_node_id() == logic_config::me()->get_local_server_id()) {
        FWLOGDEBUG("rank({}:{}:{}:{}) main is cur node node:{} version:{} {}", rank_key.rank_type(),
                   rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(),
                   rank_router_rsp->router_main_node_id(), rank_ptr->get_data_version(),
                   highest_data_version_slave_node.second);
        if (rank_ptr->get_data_version() >= highest_data_version_slave_node.second) {
          ret = RPC_AWAIT_CODE_RESULT(upgrade_rank_to_main(ctx, rank_key, version));
          RPC_RETURN_CODE(ret);
        }
      }

      if (highest_data_version_slave_node.first == 0) {
        FWLOGDEBUG("no slave rank({}:{}:{}:{}) cur node upgrade main:{}", rank_key.rank_type(),
                   rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(),
                   logic_config::me()->get_local_server_id());
        ret = RPC_AWAIT_CODE_RESULT(upgrade_rank_to_main(ctx, rank_key, version));
        RPC_RETURN_CODE(ret);
      } else {
        // 从节点升级为主节点
        FWLOGDEBUG("rank({}:{}:{}:{}) highest slave:{} upgrade main version:{}", rank_key.rank_type(),
                   rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(),
                   highest_data_version_slave_node.first, highest_data_version_slave_node.second);
        PROJECT_NAMESPACE_ID::table_rank_router tmp_rank_router;
        tmp_rank_router.CopyFrom(*rank_router_rsp);
        auto& db_slave_nodes = *tmp_rank_router.mutable_blob_data()->mutable_slave_nodes();
        auto it = std::find(db_slave_nodes.begin(), db_slave_nodes.end(), highest_data_version_slave_node.first);
        if (it != db_slave_nodes.end()) {
          db_slave_nodes.erase(it);
        }
        tmp_rank_router.set_router_main_node_id(highest_data_version_slave_node.first);
        tmp_rank_router.set_router_save_timepoint(now_tm);
        {
          rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> replace_db_router(ctx);
          replace_db_router->CopyFrom(tmp_rank_router);
          ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_router::replace(ctx, std::move(replace_db_router), version));
        }
        if (ret != 0) {
          RPC_RETURN_CODE(ret);
        }
        if (highest_data_version_slave_node.first == logic_config::me()->get_local_server_id()) {
          RPC_AWAIT_IGNORE_RESULT(rank_ptr->switch_to_main(ctx, tmp_rank_router, version));
        } else {
          rank_ptr->set_router_data(tmp_rank_router, version);
        }
        RPC_RETURN_CODE(0);
      }
    } else {
      FWLOGDEBUG("db router is valid rank({}:{}:{}:{}) main_node:{} cur_node:{}", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(),
                 rank_router_rsp->router_main_node_id(), logic_config::me()->get_local_server_id());
      if (rank_router_rsp->router_main_node_id() == logic_config::me()->get_local_server_id()) {
        RPC_AWAIT_IGNORE_RESULT(rank_ptr->switch_to_main(ctx, *rank_router_rsp, version));
      } else {
        rank_ptr->set_router_data(*rank_router_rsp, version);
      }
    }
  } else {
    FWLOGDEBUG("rank router is valid rank({}:{}:{}:{})", rank_key.rank_type(), rank_key.rank_instance_id(),
               rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
  }
  RPC_RETURN_CODE(0);
}

rank::compare_fn_t rank_manager::get_compare_fn(PROJECT_NAMESPACE_ID::EnRankSortType sort_type) {
  if (compare_mp_.empty()) {
    compare_mp_.insert(std::make_pair(PROJECT_NAMESPACE_ID::EN_RANK_SORT_TYPE_ASCENDING_ORDER,
                                      rank_compare_function::ascending_order));

    compare_mp_.insert(std::make_pair(PROJECT_NAMESPACE_ID::EN_RANK_SORT_TYPE_DESCENDING_ORDER,
                                      rank_compare_function::descending_order));
  }

  if (compare_mp_.find(sort_type) != compare_mp_.end()) {
    FWLOGDEBUG("get sort compare fn success {}", static_cast<int>(sort_type));
    return compare_mp_[sort_type];
  }
  FWLOGERROR("get sort compare fn finish {}", static_cast<int>(sort_type));
  return nullptr;
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type rank_manager::modify_score(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
    const PROJECT_NAMESPACE_ID::DRankUserKey& user_key, int64_t score,
    const PROJECT_NAMESPACE_ID::DRankCustomData& custom_data) {
  auto rank_iter = rank_map_.find(rank_key);
  if (rank_iter == rank_map_.end() || !rank_iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_NOT_EXIST);
  }

  PROJECT_NAMESPACE_ID::rank_storage_data storage_data;
  protobuf_copy_message(*storage_data.mutable_custom_data(), custom_data);
  protobuf_copy_message(*storage_data.mutable_sort_data()->mutable_key(), user_key);
  storage_data.mutable_sort_data()->mutable_value()->set_score(score);
  protobuf_copy_message(*storage_data.mutable_sort_data()->mutable_value()->mutable_sort_fields(),
                        custom_data.sort_fields());
  if (storage_data.sort_data().value().submit_timepoint() == 0) {
    storage_data.mutable_sort_data()->mutable_value()->set_submit_timepoint(util::time::time_utility::get_now());
  }
  int32_t ret = rank_iter->second->modify_score(storage_data);

  RPC_RETURN_CODE(ret);
}

// 查询排行榜接口
EXPLICIT_NODISCARD_ATTR rpc::result_code_type rank_manager::query_one_user(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
    const PROJECT_NAMESPACE_ID::DRankUserKey& sort_key, PROJECT_NAMESPACE_ID::DRankUserBoardData& output) {
  auto rank_iter = rank_map_.find(rank_key);
  if (rank_iter == rank_map_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_NOT_EXIST);
  }
  int32_t ret = 0;

  rank_iter->second->query_one_user_by_key(sort_key, output);
  RPC_RETURN_CODE(ret);
}
EXPLICIT_NODISCARD_ATTR rpc::result_code_type rank_manager::query_top(rpc::context& ctx,
                                                                      const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                                      uint32_t from, uint32_t count,
                                                                      PROJECT_NAMESPACE_ID::DRankQueryRspData& output) {
  auto rank_iter = rank_map_.find(rank_key);
  if (rank_iter == rank_map_.end()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_NOT_EXIST);
  }
  int32_t ret = rank_iter->second->query_rank_top(from, count, output);
  RPC_RETURN_CODE(ret);
}

std::vector<uint64_t> rank_manager::get_slave_nodes(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
                                                    uint64_t main_node) {
  std::vector<uint64_t> slave_nodes;
  auto mod = logic_server_last_common_module();
  if (!mod) {
    FWLOGERROR("select_teamsvr_match tsf4g_migrate_get_tbupp_handle nullptr");
    return slave_nodes;
  }

  util::memory::strong_rc_ptr<atapp::etcd_discovery_set> index_by_type = mod->get_discovery_index_by_type(
      static_cast<uint64_t>(atframework::component::logic_service_type::kRankSvr));
  if (!index_by_type) {
    FWLOGERROR("select_teamsvr_match get_all_nodes_of_type nullptr");
    return slave_nodes;
  }

  auto sorted_nodes = index_by_type->get_sorted_nodes(
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kRanksvrFieldNumber,
                                 logic_hpa_discovery_select_mode::kReady));
  if (sorted_nodes.empty()) {
    FWLOGERROR("select_teamsvr_match sorted_nodes.empty()");
    return slave_nodes;
  }
  {
    auto it =
        std::find_if(sorted_nodes.begin(), sorted_nodes.end(), [&](atapp::etcd_discovery_node::ptr_t ptr) -> bool {
          if (!ptr || ptr->get_discovery_info().id() != main_node) {
            return false;
          }
          return true;
        });
    if (it != sorted_nodes.end()) {
      sorted_nodes.erase(it);
    }
  }
  auto slave_node_cfg_num =
      logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>().rank_slave_num();
  if (slave_node_cfg_num <= 0) {
    slave_node_cfg_num = 1;
  }
  if (slave_node_cfg_num >= sorted_nodes.size()) {
    for (auto& ptr : sorted_nodes) {
      slave_nodes.push_back(ptr->get_discovery_info().id());
    }
    return slave_nodes;
  }

  while (slave_nodes.size() < slave_node_cfg_num && !sorted_nodes.empty()) {
    size_t node_num = sorted_nodes.size();
    size_t idx = util::random_engine::fast_random_between<size_t>(0, node_num);
    auto discovery_node_ptr = sorted_nodes[idx];
    if (!discovery_node_ptr || discovery_node_ptr->get_discovery_info().id() == 0) {
      sorted_nodes.erase(sorted_nodes.begin() + static_cast<int>(idx));
      continue;
    }
    slave_nodes.push_back(discovery_node_ptr->get_discovery_info().id());
    sorted_nodes.erase(sorted_nodes.begin() + static_cast<int>(idx));
    FWLOGDEBUG("rank({}:{}:{}:{}) main_node:{} add slave node:{}", rank_key.rank_type(), rank_key.rank_instance_id(),
               rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), main_node,
               discovery_node_ptr->get_discovery_info().id());
  }

  return slave_nodes;
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type rank_manager::upgrade_rank_to_main(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key, int32_t db_router_version) {
  auto main_server_node = logic_config::me()->get_local_server_id();
  PROJECT_NAMESPACE_ID::table_rank_router new_db_router;
  new_db_router.set_rank_type(rank_key.rank_type());
  new_db_router.set_rank_instance_id(rank_key.rank_instance_id());
  new_db_router.set_sub_rank_type(rank_key.sub_rank_type());
  new_db_router.set_sub_rank_instance_id(rank_key.sub_rank_instance_id());
  new_db_router.set_zone_id(logic_config::me()->get_local_zone_id());
  new_db_router.set_router_save_timepoint(util::time::time_utility::get_now());
  new_db_router.set_router_main_node_id(main_server_node);
  auto slave_nodes = rank_manager::me()->get_slave_nodes(ctx, rank_key, main_server_node);
  if (slave_nodes.empty()) {
    FWLOGWARNING("save rank router failed, no slave node rank({}:{}:{}:{})", rank_key.rank_type(),
                 rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
  } else {
    for (auto& slave_node : slave_nodes) {
      new_db_router.mutable_blob_data()->add_slave_nodes(slave_node);
    }
  }

  uint64_t new_db_router_version = db_router_version;
  auto ret = 0;
  {
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> tmp_db_router(ctx);
    tmp_db_router->CopyFrom(new_db_router);
    ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_router::replace(ctx, std::move(tmp_db_router), new_db_router_version));
  }
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  rank_ptr_type rank_ptr = nullptr;
  ret = RPC_AWAIT_CODE_RESULT(mutable_rank(ctx, rank_key, rank_ptr));
  if (ret != 0) {
    FWLOGDEBUG("upgrade_rank_to_main failed, rank({}:{}:{}:{}) ret:{}", rank_key.rank_type(),
               rank_key.rank_instance_id(), rank_key.sub_rank_type(), rank_key.sub_rank_instance_id(), ret);
    RPC_RETURN_CODE(ret);
  }

  RPC_AWAIT_IGNORE_RESULT(rank_ptr->switch_to_main(ctx, new_db_router, new_db_router_version));
  FWLOGDEBUG("upgrade_rank_to_main success, rank({}:{}:{}:{})", rank_key.rank_type(), rank_key.rank_instance_id(),
             rank_key.sub_rank_type(), rank_key.sub_rank_instance_id());
  RPC_RETURN_CODE(0);
}

rpc::result_code_type rank_manager::check_slave_and_highest_data_version_slave(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankKey& rank_key,
    const PROJECT_NAMESPACE_ID::table_rank_router& new_db_router, std::pair<uint64_t, int64_t>& highest_slave_node) {
  std::vector<task_type_trait::task_type> pending_tasks;

  std::map<int64_t, std::vector<uint64_t>> output_slave_data_version;
  for (auto& slave_node : new_db_router.blob_data().slave_nodes()) {
    auto invoke_task = rpc::async_invoke(
        ctx, "rank_manager.check_slave_and_get_data_version", [&](rpc::context& child_ctx) -> rpc::result_code_type {
          PROJECT_NAMESPACE_ID::SSRankCheckSlaveReq req_body;
          protobuf_copy_message(*req_body.mutable_rank_key(), rank_key);

          PROJECT_NAMESPACE_ID::SSRankCheckSlaveRsp rsp_body;
          auto ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_check_slave(child_ctx, slave_node, req_body, rsp_body));
          if (ret != 0) {
            FWLOGERROR("rank_manager.check_slave_and_get_data_version invoke failed rank({}:{}:{}:{}) err:{}",
                       rank_key.rank_type(), rank_key.rank_instance_id(), rank_key.sub_rank_type(),
                       rank_key.sub_rank_instance_id(), ret);
          }
          if (rsp_body.is_slave()) {
            output_slave_data_version[rsp_body.data_version()].push_back(slave_node);
          }
          RPC_RETURN_CODE(ret);
        });
    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_tasks.emplace_back(std::move(*invoke_task.get_success()));
      }
    }
  }
  if (pending_tasks.empty()) {
    RPC_RETURN_CODE(0);
  }
  auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(ctx, pending_tasks));
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  for (auto it = output_slave_data_version.rbegin(); it != output_slave_data_version.rend(); it++) {
    if (it->second.empty()) {
      continue;
    }
    auto idx = util::random_engine::fast_random_between<size_t>(0, it->second.size());
    highest_slave_node = std::make_pair(it->second[idx], it->first);
    break;
  }

  RPC_RETURN_CODE(0);
}