#include "rank.h"

#include <log/log_wrapper.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/rank_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel_config_rank_index.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <utility/persistent_btree.h>
#include <utility/protobuf_mini_dumper.h>
#include <utility/rank_util.h>

#include <rpc/db/db_utils.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "logic/rank_mirror_manager.h"
#include "logic/rank_wal_handle.h"
#include "memory/rc_ptr.h"

#include "rpc/db/local_db_interface.h"
#include "rpc/rank/ranksvrservice.h"
#include "rpc/rpc_async_invoke.h"

#include <rank/logic_rank_algorithm.h>
#include <rank/logic_rank_handle.h>

#include "rpc/rpc_common_types.h"

rank::rank(const PROJECT_NAMESPACE_ID::DRankKey& rank_key, uint32_t capacity, compare_fn_t compare_fn,
           int64_t data_version)
    : capacity_(capacity),
      data_version_(data_version),

      next_daily_settlement_id_(0),
      next_custom_settlement_id_(0),
      next_settlement_timepoint_(0),
      is_saving_mirror_(false) {
  btree_ = atfw::memory::stl::make_strong_rc<rank_tree>(
      static_cast<size_t>(logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>().rank_btree_degree()),
      logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>().rank_history_version_max_count(), compare_fn);
  key_.set_rank_type(rank_key.rank_type());
  key_.set_rank_instance_id(rank_key.rank_instance_id());
  key_.set_sub_rank_type(rank_key.sub_rank_type());
  key_.set_sub_rank_instance_id(rank_key.sub_rank_instance_id());
  mirror_manager_ = atfw::memory::stl::make_strong_rc<rank_mirror_manager>(this);
  last_save_time_ = util::time::time_utility::get_now();
}

rank::~rank() {}

int rank::tick() {
  int ret = 0;
  return ret;
}

void rank::del_data_from_btree(const PROJECT_NAMESPACE_ID::rank_sort_data& score) { btree_->erase(score); }

void rank::insert_data_from_btree(const PROJECT_NAMESPACE_ID::rank_sort_data& score) {
  while (btree_->size() > capacity_) {
    auto min_key = btree_->get_min_key();
    del_data_from_btree(*min_key);
    mp_.erase(min_key->key());
    FWRLOGDEBUG(*this, "btree test del usr:{}.{}", min_key->key().user_id(), min_key->key().zone_id());
  }
  FWRLOGDEBUG(*this, "btree test insert start usr:{}.{} score:{}", score.key().user_id(), score.key().zone_id(),
              score.value().score());
  btree_->insert(score);
  FWRLOGDEBUG(*this, "btree test insert finish usr:{}.{} score:{}", score.key().user_id(), score.key().zone_id(),
              score.value().score());
}

void rank::refresh_limit_second(rpc::context& ctx, time_t now_tm) {
  if (is_slave_node() &&
      now_tm - last_heartbeat_time_ > logic_config::me()
                                          ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>()
                                          .rank_refresh_limit_second_interval()
                                          .seconds()) {
    async_heartbeat(ctx);
  }

  auto router_lock_timeout = logic_config::me()
                                 ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>()
                                 .rank_server_router_lock_timeout()
                                 .seconds();
  if (get_router_data().main_server_id() == logic_config::me()->get_local_server_id() &&
      now_tm - last_save_router_data_time_ > (router_lock_timeout - 3)) {
    async_router_lock(ctx);
  }

  if (is_main_node()) {
    auto save_interval = logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>().rank_save_interval();
    if (now_tm - last_save_time_ > save_interval) {
      async_save_rank_data(ctx);
      last_save_time_ = now_tm;
    }
  }
}

void rank::slave_confirm_info(rpc::context& ctx, uint64_t slave_node, int64_t data_version) {
  if (!is_main_node()) {
    return;
  }
  auto it = std::find(router_data_.slave_server_ids().begin(), router_data_.slave_server_ids().end(), slave_node);
  if (it == router_data_.slave_server_ids().end()) {
    return;
  }
  if (!wal_publisher_) {
    wal_publisher_ = create_rank_publisher(*this);
  }
  int ret = 0;
  auto now = util::time::time_utility::now();
  rank_wal_publisher_context wal_ctx{ctx, ret};
  auto subscriber = wal_publisher_->find_subscriber(slave_node, wal_ctx);
  if (!subscriber) {
    rank_wal_subscriber_private_data private_data_ptr =
        util::memory::make_strong_rc<PROJECT_NAMESPACE_ID::DRankSubscriberData>();
    private_data_ptr->set_server_id(slave_node);
    protobuf_copy_message(*private_data_ptr->mutable_rank_key(), get_key());

    subscriber = wal_publisher_->create_subscriber(slave_node, now, data_version, wal_ctx, private_data_ptr);
    FWRLOGDEBUG(*this, "add slave subscriber success slave_node:{} data_version:{}", slave_node, data_version);
  } else {
    wal_publisher_->receive_subscribe_request(slave_node, data_version, now, wal_ctx);
    FWRLOGDEBUG(*this, "slave subscriber confirm slave_node:{} data_version:{}", slave_node, data_version);
  }
}

void rank::increase_data_version() { data_version_++; }

int32_t rank::update_score(const PROJECT_NAMESPACE_ID::DRankEventUpdateUserScore& data) {
  PROJECT_NAMESPACE_ID::rank_storage_data storage_data;
  protobuf_copy_message(*storage_data.mutable_custom_data(), data.custom_data());
  protobuf_copy_message(*storage_data.mutable_sort_data()->mutable_key(), data.user_key());
  storage_data.mutable_sort_data()->mutable_value()->set_score(data.score());
  protobuf_copy_message(*storage_data.mutable_sort_data()->mutable_value()->mutable_sort_fields(),
                        data.custom_data().sort_fields());
  if (storage_data.sort_data().value().submit_timepoint() == 0) {
    storage_data.mutable_sort_data()->mutable_value()->set_submit_timepoint(util::time::time_utility::get_now());
  }
  return update_score(storage_data);
}

int32_t rank::update_score(const PROJECT_NAMESPACE_ID::rank_storage_data& data) {
  if (data.sort_data().value().score() < 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_RANK_SCORE_INVALID;
  }

  auto iter = mp_.find(data.sort_data().key());
  if (iter != mp_.end()) {
    // 先删除原来的分数
    del_data_from_btree(mp_[data.sort_data().key()].sort_data());
  }
  mp_[data.sort_data().key()] = data;
  // 之前的删除操作时一个内部临时处理，不应该保存版本
  insert_data_from_btree(mp_[data.sort_data().key()].sort_data());
  increase_data_version();
  return 0;
}

int32_t rank::modify_score(const PROJECT_NAMESPACE_ID::rank_storage_data& data) {
  auto iter = mp_.find(data.sort_data().key());
  int64_t origin_score = 0;
  increase_data_version();

  if (iter != mp_.end()) {
    // 先删除原来的分数
    del_data_from_btree(mp_[data.sort_data().key()].sort_data());
    origin_score = mp_[data.sort_data().key()].sort_data().value().score();
  }
  int64_t now_score = origin_score + data.sort_data().value().score();
  if (now_score < 0) {
    now_score = 0;
  }

  if (now_score == 0) {
    // 0分自动下榜
    return 0;
  }

  mp_[data.sort_data().key()] = data;
  mp_[data.sort_data().key()].mutable_sort_data()->mutable_value()->set_score(now_score);
  insert_data_from_btree(mp_[data.sort_data().key()].sort_data());
  return 0;
}

int32_t rank::del_one_user(const PROJECT_NAMESPACE_ID::DRankUserKey& key) {
  auto iter = mp_.find(key);
  if (iter == mp_.end()) {
    // 玩家不在榜单上
    return 0;
  }
  increase_data_version();
  del_data_from_btree(mp_[key].sort_data());
  mp_.erase(key);
  return 0;
}

int32_t rank::clear_rank() {
  increase_data_version();
  btree_->clear();
  return 0;
}

int32_t rank::query_one_user_by_key(const PROJECT_NAMESPACE_ID::DRankUserKey& key,
                                    PROJECT_NAMESPACE_ID::DRankUserBoardData& output) {
  auto iter = mp_.find(key);
  if (iter == mp_.end()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_RANK_USER_NOT_FOUND;
  }
  size_t rank_no = btree_->index(iter->second.sort_data());
  rank_util::dump_rank_basic_board_from_rank_data(iter->second, output);
  output.set_rank_no(static_cast<uint32_t>(rank_no));
  return 0;
}

int32_t rank::query_one_user_by_score(ATFW_EXPLICIT_UNUSED_ATTR const PROJECT_NAMESPACE_ID::rank_sort_score& key,
                                      ATFW_EXPLICIT_UNUSED_ATTR PROJECT_NAMESPACE_ID::rank_data& output) {
  return 0;
}

int32_t rank::query_rank_top(uint32_t from, uint32_t count, PROJECT_NAMESPACE_ID::DRankQueryRspData& output) {
  if (from == 0 || count == 0) {
    return 0;
  }

  if (count > logic_config::me()->get_server_cfg().rank().query_max_count()) {
    count = logic_config::me()->get_server_cfg().rank().query_max_count();
  }

  std::vector<util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::rank_sort_data>> result;
  btree_->batch_query(static_cast<size_t>(from), static_cast<size_t>(count), result);

  FWRLOGDEBUG(*this, "query_rank_top count {}", result.size());

  for (auto& unit : result) {
    if (mp_.find(unit->key()) != mp_.end()) {
      auto data = output.mutable_rank_records()->Add();
      if (data) {
        rank_util::dump_rank_basic_board_from_rank_data(mp_[unit->key()], *data);
        data->set_rank_no(from);
      }
      from++;
    }
  }
  output.set_rank_total_count(static_cast<uint32_t>(btree_->size()));
  return 0;
}

int32_t rank::query_one_user_by_rank_no(int32_t rank_no, PROJECT_NAMESPACE_ID::rank_data& output) {
  auto iter = btree_->at(static_cast<size_t>(rank_no));
  if (iter == btree_->end()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_RANK_NO_NOT_FOUND;
  }
  auto mp_iter = mp_.find((*iter).key());
  if (mp_iter == mp_.end()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_RANK_NO_HAS_BUT_NO_FOUND_DETAIL_DATA;
  }
  protobuf_copy_message(*output.mutable_data(), mp_iter->second);
  output.set_rank_no(static_cast<uint32_t>(rank_no));
  return 0;
}

int32_t rank::query_rank_user_front_back(const PROJECT_NAMESPACE_ID::DRankUserKey& key, uint32_t count,
                                         PROJECT_NAMESPACE_ID::DRankQueryRspData& output) {
  if (2 * count > logic_config::me()->get_server_cfg().rank().query_max_count()) {
    count = logic_config::me()->get_server_cfg().rank().query_max_count() / 2;
  }
  auto iter = mp_.find(key);
  if (iter == mp_.end()) {
    // 不在榜上
    return PROJECT_NAMESPACE_ID::EN_ERR_RANK_USER_NOT_FOUND;
  }
  uint32_t rank_no = static_cast<uint32_t>(btree_->index(iter->second.sort_data()));
  auto start_no = rank_no > count ? rank_no - count : 1;
  auto real_count = start_no == 1 ? (count + rank_no) : (2 * count + 1);
  FWRLOGDEBUG(*this, "query_rank_user_front_back user:{} rank_no:{} start_no:{} count {} real_count:{}", key.user_id(),
              rank_no, start_no, count, real_count);
  return query_rank_top(start_no, real_count, output);
}

void rank::fetch_rank_data(google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::rank_data>& output) {
  std::vector<util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::rank_sort_data>> result;
  btree_->get_preorder_traversal(result);
  uint32_t rank_no = 0;
  for (const auto& unit : result) {
    rank_no++;
    if (mp_.find(unit->key()) != mp_.end()) {
      auto ptr = output.Add();
      if (ptr) {
        ptr->set_rank_no(rank_no);
        protobuf_copy_message(*ptr->mutable_data(), mp_[unit->key()]);
      }
    }
  }
  return;
}

void rank::async_save_rank_data(rpc::context& ctx) {
  auto rank_ptr = shared_from_this();
  auto invoke_task =
      rpc::async_invoke(ctx, "rank.async_save_rank_data", [rank_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rank_ptr->save_rank_data(child_ctx)));
      });
  if (invoke_task.is_error()) {
    // FWRLOGERROR(*this, "async_save_rank_data failed, invoke_task is error");
  }
}

rpc::result_code_type rank::save_rank_data(rpc::context& ctx) {
  int64_t mirror_id = 0;
  auto ret = RPC_AWAIT_CODE_RESULT(mirror_manager_->create_mirror(ctx, mirror_id, true));
  if (ret == 0) {
    FWRLOGDEBUG(*this, "save rank data mirror:{}", mirror_id);
  } else {
    FWRLOGERROR(*this, "save rank data mirror failed, ret {}", ret);
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type rank::init_rank_from_db(rpc::context& ctx) {
  if (is_init()) {
    RPC_RETURN_CODE(0);
  }

  if (is_io_task_running()) {
    RPC_AWAIT_IGNORE_RESULT(await_io_task(ctx));
  }

  auto rank_ptr = shared_from_this();
  auto invoke_task =
      rpc::async_invoke(ctx, "rank.init_rank_from_db", [rank_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(rank_ptr->mirror_manager_->init_from_db(child_ctx));
        if (ret != 0) {
          RPC_RETURN_CODE(ret);
        }
        auto& mirror_meta_data = rank_ptr->mirror_manager_->get_rank_mirror_meta_data();
        if (mirror_meta_data.last_save_mirror().mirror_id() > 0) {
          std::vector<atfw::util::memory::strong_rc_ptr<rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_mirror>>>
              db_data;
          ret = RPC_AWAIT_CODE_RESULT(rank_ptr->mirror_manager_->get_mirror_data_from_db(
              child_ctx, mirror_meta_data.last_save_mirror(), db_data));
          if (ret != 0) {
            FWRLOGDEBUG(*rank_ptr.get(), "get_mirror_data_from_db failed mirror:{} ret:{}",
                        mirror_meta_data.last_save_mirror().mirror_id(), ret);
            RPC_RETURN_CODE(ret);
          }
          // 恢复榜单数据
          for (const auto& table : db_data) {
            for (const auto& unit : table->get()->blob_data().data()) {
              rank_ptr->update_score(unit.data());
            }
          }
          if (!db_data.empty()) {
            rank_ptr->data_version_ = db_data[0]->get()->data_version();
          }
        }

        rank_ptr->is_init_ = true;
        FWRLOGDEBUG(*rank_ptr.get(), "load rank from db success mirror_id:{}",
                    mirror_meta_data.last_save_mirror().mirror_id());
        RPC_RETURN_CODE(0);
      });
  if (invoke_task.is_error()) {
    // FWRLOGERROR(*this, "init_rank_from_db failed, invoke_task is error, invoke_task");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
  }
  if (invoke_task.is_success()) {
    io_task_ = std::move(*invoke_task.get_success());
  }
  auto ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx));
  RPC_RETURN_CODE(ret);
}

bool rank::is_main_node() const {
  auto now_tm = util::time::time_utility::get_now();
  auto timeout = logic_config::me()
                     ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_ranking_cfg>()
                     .rank_server_router_lock_timeout()
                     .seconds();

  return logic_config::me()->get_local_server_id() == router_data_.main_server_id() &&
         now_tm - last_save_router_data_time_ < timeout;
}

bool rank::is_slave_node() const {
  auto cur_server_id = logic_config::me()->get_local_server_id();
  auto it = std::find_if(router_data_.slave_server_ids().begin(), router_data_.slave_server_ids().end(),
                         [cur_server_id](uint64_t server_id) -> bool { return server_id == cur_server_id; });
  return it != router_data_.slave_server_ids().end();
}

bool rank::is_readable() const { return is_main_node() || is_slave_node(); }

const PROJECT_NAMESPACE_ID::DRankRouterData& rank::get_router_data() const { return router_data_; }

rpc::result_code_type rank::switch_to_main(rpc::context& ctx, const PROJECT_NAMESPACE_ID::table_rank_router& db_router,
                                           int32_t db_router_version) {
  set_router_data(db_router, db_router_version);
  RPC_AWAIT_IGNORE_RESULT(notify_switch_to_slave(ctx));

  if (!wal_publisher_) {
    wal_publisher_ = create_rank_publisher(*this);
  }
  RPC_RETURN_CODE(0);
}

void rank::set_router_data(const PROJECT_NAMESPACE_ID::table_rank_router& db_router, int32_t db_router_version) {
  router_data_.set_main_server_id(db_router.router_main_node_id());
  protobuf_copy_message(*router_data_.mutable_slave_server_ids(), db_router.blob_data().slave_nodes());

  last_save_router_data_time_ = db_router.router_save_timepoint();
  router_data_.set_router_version(db_router_version);
}

void rank::switch_to_slave(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DRankRouterData& router_data) {
  protobuf_copy_message(router_data_, router_data);
  async_heartbeat(ctx);
}

rpc::result_code_type rank::notify_switch_to_slave(rpc::context& ctx) {
  if (!is_main_node()) {
    RPC_RETURN_CODE(0);
  }

  std::vector<task_type_trait::task_type> pending_tasks;
  const auto& router_data = get_router_data();
  auto& rank_key = get_key();
  auto data_version = data_version_;
  auto self_ptr = this->shared_from_this();
  for (auto& slave_node : router_data_.slave_server_ids()) {
    auto invoke_task = rpc::async_invoke(
        ctx, "rank.switch_to_slave",
        [self_ptr, data_version, rank_key, router_data, slave_node](rpc::context& child_ctx) -> rpc::result_code_type {
          PROJECT_NAMESPACE_ID::SSRankSwitchToSlaveReq req;
          PROJECT_NAMESPACE_ID::SSRankSwitchToSlaveRsp rsp;
          protobuf_copy_message(*req.mutable_rank_key(), rank_key);
          protobuf_copy_message(*req.mutable_router_data(), router_data);
          req.set_data_version(data_version);

          auto ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_switch_to_slave(child_ctx, slave_node, req, rsp));
          if (ret != 0) {
            FWRLOGERROR(*self_ptr.get(), "switch to slave failed cur_node:{} slave:{} ret {}",
                        logic_config::me()->get_local_server_id(), slave_node, ret);
          }
          RPC_RETURN_CODE(ret);
        });
    if (invoke_task.is_error()) {
      FWRLOGERROR(*this, "switch to slave invoke task failed cur_node:{} slave:{}",
                  logic_config::me()->get_local_server_id(), slave_node);
    }
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
  RPC_RETURN_CODE(0);
}

void rank::async_heartbeat(rpc::context& ctx) {
  auto main_server_id = get_router_data().main_server_id();
  if (main_server_id <= 0) {
    FWRLOGERROR(*this, "main_server_id is invalid, main_server_id {}", main_server_id);
    return;
  }
  if (is_heartbeat_running_) {
    return;
  }
  is_heartbeat_running_ = true;
  auto rank_ptr = this->shared_from_this();
  auto invoke_task = rpc::async_invoke(
      ctx, "rank.heartbeat", [main_server_id, rank_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::SSRankHeartbeatReq req;
        PROJECT_NAMESPACE_ID::SSRankHeartbeatRsp rsp;
        req.set_data_version(rank_ptr->get_data_version());
        protobuf_copy_message(*req.mutable_rank_key(), rank_ptr->get_key());
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::rank::rank_heartbeat(child_ctx, main_server_id, req, rsp));
        if (ret != 0) {
          FWRLOGERROR(*rank_ptr.get(), "heartbeat failed main:{} slave:{} ret {}", main_server_id,
                      logic_config::me()->get_local_server_id(), ret);
        }
        rank_ptr->last_heartbeat_time_ = util::time::time_utility::get_now();
        RPC_RETURN_CODE(0);
      });
  is_heartbeat_running_ = false;
  if (invoke_task.is_error()) {
    FWRLOGERROR(*this, "invoke task failed main:{} slave:{}", main_server_id,
                logic_config::me()->get_local_server_id());
  }
}

void rank::async_router_lock(rpc::context& ctx) {
  auto rank_ptr = this->shared_from_this();
  auto invoke_task =
      rpc::async_invoke(ctx, "rank.router_lock", [rank_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto now_tm = util::time::time_utility::get_now();
        auto& router_data = rank_ptr->get_router_data();
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> new_db_router(child_ctx);
        new_db_router->set_rank_type(rank_ptr->get_key().rank_type());
        new_db_router->set_rank_instance_id(rank_ptr->get_key().rank_instance_id());
        new_db_router->set_sub_rank_type(rank_ptr->get_key().sub_rank_type());
        new_db_router->set_sub_rank_instance_id(rank_ptr->get_key().sub_rank_instance_id());
        new_db_router->set_zone_id(logic_config::me()->get_local_zone_id());
        new_db_router->set_router_save_timepoint(now_tm);
        new_db_router->set_router_main_node_id(router_data.main_server_id());
        for (auto& slave_id : router_data.slave_server_ids()) {
          new_db_router->mutable_blob_data()->add_slave_nodes(slave_id);
        }
        int ret = 0;
        {
          uint64_t version = router_data.router_version();
          uint64_t old_version = version;
          {
            rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> replace_db_router(child_ctx);
            protobuf_copy_message(*replace_db_router, *new_db_router);
            ret =
                RPC_AWAIT_CODE_RESULT(rpc::db::rank_router::replace(child_ctx, std::move(replace_db_router), version));
          }
          if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION) {
            uint64_t new_version = 0;
            rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_router> tmp_db_router;
            auto tmp_ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_router::get_all(
                child_ctx, rank_ptr->get_key().rank_type(), rank_ptr->get_key().rank_instance_id(),
                rank_ptr->get_key().sub_rank_type(), rank_ptr->get_key().sub_rank_instance_id(),
                logic_config::me()->get_local_zone_id(), tmp_db_router, new_version));
            if (tmp_ret == 0) {
              FWRLOGDEBUG(*rank_ptr.get(), "rank router cur_version:{} db_version:{}", version, new_version);
            } else {
              FWRLOGDEBUG(*rank_ptr.get(), "rank router cur_version:{} get db failed err:{} ", version, tmp_ret);
            }
          }
          if (ret != 0) {
            FWRLOGDEBUG(*rank_ptr.get(), "rank router repalce failed version:{} ret:{}", version, ret);
            RPC_RETURN_CODE(ret);
          } else {
            rank_ptr->router_data_.set_router_version(version);
            rank_ptr->last_save_router_data_time_ = now_tm;
            FWRLOGDEBUG(*rank_ptr.get(), "rank router change version:{} -> {} tm:{}", old_version, version,
                        static_cast<uint64_t>(now_tm));
          }
          RPC_RETURN_CODE(0);
        }
      });
  if (invoke_task.is_error()) {
    FWRLOGERROR(*this, "lock invoke task failed cur_node:{}", logic_config::me()->get_local_server_id());
  }
}

void rank::broadcast_events(rpc::context& ctx, PROJECT_NAMESPACE_ID::DRankEventLog&& log) {
  if (!wal_publisher_) {
    return;
  }
  if (log.event_id() <= 0) {
    log.set_event_id(get_data_version());
  }

  int32_t ret = 0;
  rank_wal_publisher_context wal_ctx{ctx, ret};
  auto new_log = wal_publisher_->allocate_log(
      util::time::time_utility::now(), PROJECT_NAMESPACE_ID::DRankEventLog::EVENT_NOT_SET, wal_ctx, std::move(log));
  auto res = wal_publisher_->emplace_back_log(std::move(new_log), wal_ctx);
  if (res < util::distributed_system::wal_result_code::kOk) {
    FWRLOGERROR(*this, "emplace_back_log failed version:{} ret {}", get_data_version(), static_cast<int>(res))
    return;
  }
  wal_publisher_->broadcast(wal_ctx);

  // Recycle expired logs
  wal_publisher_->tick(util::time::time_utility::now(), wal_ctx);
  FWRLOGDEBUG(*this, "emplace_back_log finish version:{}", get_data_version())
}

bool rank::is_task_running(task_type_trait::task_type& task) {
  if (!task_type_trait::empty(task)) {
    if (task_type_trait::is_exiting(task)) {
      task_type_trait::reset_task(task);
    }
  }

  return !task_type_trait::empty(task);
}

bool rank::is_io_task_running() { return is_task_running(io_task_); }
rpc::result_code_type rank::await_io_task(rpc::context& ctx) {
  if (!is_io_task_running()) {
    RPC_RETURN_CODE(0);
  }
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
  if (!task_type_trait::empty(io_task_) && task_type_trait::is_exiting(io_task_)) {
    task_type_trait::reset_task(io_task_);
  }

  RPC_RETURN_CODE(ret);
}