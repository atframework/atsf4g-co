// Copyright 2026 atframework
// Created by atsf4g-co orbit_room module migration

#include "logic/room/orbit_room.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <config/excel/config_easy_api.h>
#include <logic/logic_server_setup.h>
#include <logic/orbit_server_manager.h>
#include <rpc/dtmq/dtmq_client_api.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <rpc/async_jobs/async_jobs.h>
#include <rpc/orbit/orbitsvrmatchsvrservice.atfw.gen.h>
#include <rpc/orbit_client_rpc/orbitclientrpcservice.atfw.gen.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/com.struct.orbit.config.pb.h>
#include <protocol/config/orbitsvr_config.pb.h>
#include <protocol/pbdesc/com.orbit.protocol.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

namespace {
const PROJECT_NAMESPACE_ID::config::orbitsvr_cfg& get_orbitsvr_cfg() noexcept {
  return logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::orbitsvr_cfg>();
}
}  // namespace

orbit_room::orbit_room(const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                       const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& room_data)
    : room_key_(room_key), room_data_(room_data) {
  channel_key_.set_channel_type(PROJECT_NAMESPACE_ID::EN_ORBIT_CHANNEL_TYPE_ROOM);
  channel_key_.set_channel_id(room_key.client_id());
  subscriber_key_ = atfw::util::string::format("orbit_room:{}", room_key.client_id());
}

void orbit_room::tick() {
  if (ready_to_destroy()) {
    return;
  }

  // 注册流程
  init_user_to_client(logic_server_get_current_tick_context());

  // 重试结算流程
  if (need_retry_settlement_) {
    need_retry_settlement_ = false;
    for (const auto& user_data : user_data_index_) {
      async_user_settlement(logic_server_get_current_tick_context(), user_data.second);
    }
  }

  auto now = util::time::time_utility::get_now();
  if (expired_timepoint_ > 0 && now > expired_timepoint_) {
    // 超时
    room_finish(logic_server_get_current_tick_context(), PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_TIMEOUT);
  }

  // 状态超时流程
  if (loading_timeout_ != 0 && now > loading_timeout_) {
    FWLOGERROR("orbit_room {} tick, status: {}, loading_timeout: {}, now: {}", get_client_id(),
               static_cast<int32_t>(room_status_), loading_timeout_, now);
    loading_timeout_ = 0;
    // Loading超时，直接结束房间
    room_finish(logic_server_get_current_tick_context(), PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_LOAD_FAILED);
  }

  // 所有人都结算完成 转为退出状态
  if (room_status_ == PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_FINISH) {
    bool all_user_settlement_finish = true;
    for (const auto& user_data : user_data_index_) {
      if (!user_data.second->settlement_finish_) {
        all_user_settlement_finish = false;
        break;
      }
    }
    if (all_user_settlement_finish) {
      set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT);
    }
  }
}

const std::string& orbit_room::get_client_id() const noexcept { return room_key_.client_id(); }
const std::string& orbit_room::get_region() const { return room_data_.region(); }
int32_t orbit_room::set_status(PROJECT_NAMESPACE_ID::EnOrbitRoomStatus v) {
  room_status_ = v;
  return 0;
}

bool orbit_room::ready_to_destroy() const { return room_status_ == PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT; }

void orbit_room::on_destroy() {
  // 回收资源 通知清理频道
  if (subscriber_) {
    auto room_ptr = shared_from_this();
    auto invoke_result = rpc::async_invoke(
        logic_server_get_current_tick_context(), "orbit_room.notify_dtmq_destroy",
        [room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
          int32_t ret = RPC_AWAIT_CODE_RESULT(room_ptr->subscriber_->send_destroy(child_ctx, nullptr, true));
          if (ret != 0) {
            FWLOGERROR("orbit_room {} notify_dtmq_destroy failed, ret: {}", room_ptr->get_client_id(), ret);
          }
          RPC_RETURN_CODE(ret);
        });
    if (invoke_result.is_error()) {
      FWLOGERROR("orbit_room {} invoke notify_dtmq_destroy failed, result: {}", room_ptr->get_client_id(),
                 *invoke_result.get_error());
    }
  }
}

int32_t orbit_room::create(EXPLICIT_UNUSED_ATTR rpc::context& ctx, uint64_t match_server_id) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID != room_status_) {
    FWLOGERROR("orbit_room {} create failed, status: {}", get_client_id(), static_cast<int32_t>(room_status_));
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_STATUS_INVALID;
  }

  auto row = excel::get_ExcelOrbitClientTemplate_by_client_template_id(room_data_.client_template_id());
  if (row == nullptr) {
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_CLIENT_TEMPLATE_NOT_FOUND;
  }

  // 初始化流程
  create_timepoint_ = util::time::time_utility::get_now();
  if (row->room_expired_timeout() > 0) {
    expired_timepoint_ = create_timepoint_ + row->room_expired_timeout();
  }
  match_server_id_ = match_server_id;
  // Loading 超时
  loading_timeout_ = create_timepoint_ + get_orbitsvr_cfg().room_client_loading_timeout_sec();
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CREATED);

  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
  subscriber_ = rpc::dtmq::client_subscriber::create(channel_key_, subscribe_options);
  if (!subscriber_) {
    FWLOGERROR("Failed to create world chat channel {}:{}, maybe configure is missing.", channel_key_.channel_type(),
               channel_key_.channel_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }

  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_orbit_room_status(room_status_);
  *event_log.mutable_room_key() = room_key_;
  event_log.mutable_room_init()->set_client_template_id(room_data_.client_template_id());
  event_log.mutable_room_init()->set_region(room_data_.region());
  event_log.mutable_room_init()->set_match_id(room_data_.match_id());
  event_log.mutable_room_init()->set_create_timepoint(create_timepoint_);
  event_log.mutable_room_init()->set_expired_timepoint(expired_timepoint_);
  add_event_log(ctx, std::move(event_log));
  return 0;
}

rpc::result_code_type orbit_room::start_client(rpc::context& ctx, const atfw::orbit::DAgentClientStartArgs& args) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CREATED != room_status_) {
    FWLOGERROR("orbit_room {} start_client failed, status: {}", get_client_id(), static_cast<int32_t>(room_status_));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_STATUS_INVALID);
  }
  int32_t ret = RPC_AWAIT_CODE_RESULT(orbit_server_manager::me()->start_client(ctx, get_region(), args));
  if (ret != 0) {
    FWLOGERROR("orbit_room {} start_client failed, ret: {}", get_client_id(), ret);
    room_finish(ctx, PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_LOAD_FAILED);
    RPC_RETURN_CODE(ret);
  }
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING);
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int32_t orbit_room::on_client_start(EXPLICIT_UNUSED_ATTR rpc::context& ctx, const std::string& client_addr) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING != room_status_) {
    FWLOGERROR("orbit_room {} on_client_start failed, status: {}", get_client_id(), static_cast<int32_t>(room_status_));
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_STATUS_INVALID;
  }

  client_address_ = client_addr;
  loading_timeout_ = 0;
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_RUNNING);
  join_end_timepoint_ = util::time::time_utility::get_now() + get_orbitsvr_cfg().room_user_init_timeout_sec();

  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_orbit_room_status(room_status_);
  *event_log.mutable_room_key() = room_key_;
  event_log.mutable_ready_data()->set_end_join_timepoint(join_end_timepoint_);
  event_log.mutable_ready_data()->set_client_address(client_address_);
  add_event_log(ctx, std::move(event_log));

  // 通知 match_server 房间已加载完成
  auto room_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room.async_matchsvr_room_start", [room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto req = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq>(child_ctx);
        auto rsp = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp>(child_ctx);
        req->set_matching_id(room_ptr->room_data_.match_id());
        req->set_start_success(true);
        req->set_end_join_timepoint(room_ptr->join_end_timepoint_);
        req->set_expired_timepoint(room_ptr->expired_timepoint_);
        int32_t ret = 0;
        ret = RPC_AWAIT_CODE_RESULT(rpc::orbit::orbit_room_ready(child_ctx, room_ptr->match_server_id_, *req, *rsp));
        if (ret == 0) {
          ret = rsp->result();
        }
        if (ret != 0) {
          FWLOGERROR("orbit_room {} async_matchsvr_room_start failed, ret: {}", room_ptr->get_client_id(), ret);
          room_ptr->room_finish(child_ctx, PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_NOTIFY_MATCH_FAILED);
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke async_matchsvr_room_start failed, result: {}", room_ptr->get_client_id(),
               *invoke_result.get_error());
    room_ptr->room_finish(ctx, PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_NOTIFY_MATCH_FAILED);
  }
  return 0;
}

int32_t orbit_room::init_user(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserInitData>& user_list, bool is_last_one) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_RUNNING != room_status_) {
    FWLOGERROR("orbit_room {} init_user failed, status: {}", get_client_id(), static_cast<int32_t>(room_status_));
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_STATUS_INVALID;
  }
  if (init_user_finish_) {
    FWLOGERROR("orbit_room {} init_user failed, already finish, status: {}", get_client_id(),
               static_cast<int32_t>(room_status_));
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_STATUS_INVALID;
  }
  if (join_end_timepoint_ != 0 && util::time::time_utility::get_now() > join_end_timepoint_) {
    FWLOGERROR("orbit_room {} join_users failed, join_end_timepoint: {}, now: {}", get_client_id(), join_end_timepoint_,
               util::time::time_utility::get_now());
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_JOIN_TIMEOUT;
  }

  // 先放入
  for (const auto& user_init_data : user_list) {
    orbit_room_user_data_ptr_t user_data = atfw::util::memory::make_strong_rc<orbit_room_user_data>();
    user_data->init_data_ = user_init_data;
    user_data->user_key_ = user_init_data.user_key().user_key();
    user_data_index_[user_init_data.user_key().user_key()] = user_data;
  }
  init_user_finish_ = is_last_one;
  if (is_last_one) {
    finish_user_list_.reserve(user_data_index_.size());
  }
  // 由tick注册入Client
  return 0;
}

int32_t orbit_room::init_user_to_client(rpc::context& ctx) {
  if (!task_type_trait::empty(init_to_client_task_) && !task_type_trait::is_exiting(init_to_client_task_)) {
    return 0;
  }

  if (init_to_client_finish_count_ == user_data_index_.size()) {
    return 0;
  }

  auto room_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room.user_init_to_client", [room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        auto req = rpc::make_shared_message<PROJECT_NAMESPACE_ID::OrbitClientUserInitReq>(child_ctx);
        auto rsp = rpc::make_shared_message<PROJECT_NAMESPACE_ID::OrbitClientUserInitRsp>(child_ctx);

        size_t user_count = 0;
        for (const auto& user_pair : room_ptr->user_data_index_) {
          auto user_ptr = user_pair.second;
          if (!user_ptr->init_ && !user_ptr->finish_) {
            *req->add_user_data() = user_ptr->init_data_;
            user_count++;
          }
        }
        if (user_count == 0) {
          FWLOGERROR("orbit_room {} init_user_to_client, all user already init to client", room_ptr->get_client_id());
          room_ptr->init_to_client_finish_count_ = room_ptr->user_data_index_.size();
          RPC_RETURN_CODE(0);
        }

        if (room_ptr->init_user_finish_ &&
            room_ptr->init_to_client_finish_count_ + user_count >= room_ptr->user_data_index_.size()) {
          // 都注册完成 并且这次是最后一批用户注册
          req->set_init_finish(true);
        }

        int32_t ret =
            RPC_AWAIT_CODE_RESULT(rpc::orbit_client_rpc::user_init(child_ctx, room_ptr->get_client_id(), *req, *rsp));
        if (ret == 0) {
          ret = rsp->ret_code();
        }
        if (ret != 0) {
          FWLOGERROR("orbit_room {} user_init failed, ret: {}", room_ptr->get_client_id(), ret);
          for (const auto& user_data : req->user_data()) {
            auto user_iter = room_ptr->user_data_index_.find(user_data.user_key().user_key());
            if (user_iter == room_ptr->user_data_index_.end()) {
              FWLOGERROR("orbit_room {} user_init failed, user not found, user_key: {}", room_ptr->get_client_id(),
                         user_data.user_key().user_key().user_id());
              continue;
            }
            user_iter->second->init_retry_count_++;
            if (user_iter->second->init_retry_count_ > 3) {
              FWLOGERROR("orbit_room {} user_init failed, user init retry count exceeded, user_key: {}",
                         room_ptr->get_client_id(), user_data.user_key().user_key().user_id());
              user_iter->second->finish_ = true;
              user_iter->second->finish_timepoint_ = util::time::time_utility::get_now();
              room_ptr->init_to_client_finish_count_++;
            }
          }
          RPC_RETURN_CODE(ret);
        }
        for (int i = 0; i < rsp->data_size(); ++i) {
          auto& data = rsp->data(i);
          auto user_iter = room_ptr->user_data_index_.find(data.user_key().user_key());
          if (user_iter == room_ptr->user_data_index_.end()) {
            FWLOGERROR("orbit_room {} user_init_to_client failed, user not found, user_key: {}",
                       room_ptr->get_client_id(), data.user_key().user_key().user_id());
            continue;
          }
          auto user_ptr = user_iter->second;
          user_ptr->init_result_ = data;
          user_ptr->init_ = true;
          room_ptr->init_to_client_finish_count_++;

          PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
          event_log.set_orbit_room_status(room_ptr->room_status_);
          *event_log.mutable_room_key() = room_ptr->room_key_;
          *event_log.mutable_user_init_success()->mutable_init_result() = user_ptr->init_result_;
          room_ptr->add_event_log(child_ctx, std::move(event_log));
        }
        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke add orbit_finish async job failed result: {}", get_client_id(),
               *invoke_result.get_error());
    return *invoke_result.get_error();
  }
  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    init_to_client_task_ = std::move(*invoke_result.get_success());
  }
  return 0;
}

int32_t orbit_room::on_user_finish(
    rpc::context& ctx,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results) {
  if (results.empty()) {
    FWLOGWARNING("orbit_room {} on_user_finish with empty results, room will exit directly", get_client_id());
    return PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_USER_FINISH_RESULT_EMPTY;
  }

  for (const auto& result : results) {
    auto user_iter = user_data_index_.find(result.user_key().user_key());
    if (user_iter == user_data_index_.end()) {
      FWLOGERROR("orbit_room {} on_user_finish failed, user not found, user_key: {}:{}", get_client_id(),
                 result.user_key().user_key().user_id(), result.user_key().user_key().zone_id());
      continue;
    }
    auto user_ptr = user_iter->second;
    if (user_ptr->finish_) {
      FWLOGERROR("orbit_room {} on_user_finish failed, user already finish, user_key: {}:{}", get_client_id(),
                 result.user_key().user_key().user_id(), result.user_key().user_key().zone_id());
      continue;
    }

    user_ptr->finish_result_ = result;
    user_ptr->finish_ = true;
    user_ptr->finish_timepoint_ = util::time::time_utility::get_now();
    finish_user_list_.push_back(result.user_key().user_key());
    async_user_settlement(ctx, user_ptr);
  }
  return 0;
}

int32_t orbit_room::on_client_end(rpc::context& ctx, atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code) {
  FWLOGINFO("orbit_room {} on_client_end, reason: {}, exit_code: {}", get_client_id(), static_cast<int>(exit_reason),
            exit_code);

  PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason room_exit_reason =
      PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_UNKNOWN;
  switch (exit_reason) {
    case atfw::orbit::EN_CLIENT_EXIT_REASON_CRASH:
      room_exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_CRASH;
      break;
    case atfw::orbit::EN_CLIENT_EXIT_REASON_HEARTBEAT_TIMEOUT:
    case atfw::orbit::EN_CLIENT_EXIT_REASON_STARTUP_TIMEOUT:
      room_exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_TIMEOUT;
      break;
    case atfw::orbit::EN_CLIENT_EXIT_STARTUP_FAILED:
      room_exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_LOAD_FAILED;
      break;
    case atfw::orbit::EN_CLIENT_EXIT_REASON_NORMAL:
    case atfw::orbit::EN_CLIENT_EXIT_REASON_UNSPECIFIED:
    default:
      room_exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_UNKNOWN;
      break;
  }
  return room_finish(ctx, room_exit_reason);
}

int32_t orbit_room::room_finish(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_FINISH == room_status_) {
    return 0;
  }
  // 处理exit_reason
  if (exit_reason == PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_UNKNOWN) {
    // 通过房间状态选取
    switch (room_status_) {
      case PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID:
      case PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CREATED:
      case PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING:
        exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_LOAD_FAILED;
        break;
      case PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_RUNNING:
        exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_FINISH;
        break;
      default:
        exit_reason = PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_UNKNOWN;
        break;
    }
  }

  // 检查是否是Running 如果不是则需要通知Matchsvr
  if (room_status_ != PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_RUNNING && match_server_id_ != 0) {
    auto room_ptr = shared_from_this();
    auto invoke_result = rpc::async_invoke(
        ctx, "orbit_room.async_matchsvr_room_start_failed",
        [room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
          auto req = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq>(child_ctx);
          auto rsp = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp>(child_ctx);
          req->set_matching_id(room_ptr->room_data_.match_id());
          req->set_start_success(false);
          int32_t ret = 0;
          ret = RPC_AWAIT_CODE_RESULT(rpc::orbit::orbit_room_ready(child_ctx, room_ptr->match_server_id_, *req, *rsp));
          if (ret == 0) {
            ret = rsp->result();
          }
          if (ret != 0) {
            FWLOGERROR("orbit_room {} async_matchsvr_room_start_failed failed, ret: {}", room_ptr->get_client_id(),
                       ret);
          }
          RPC_RETURN_CODE(ret);
        });
    if (invoke_result.is_error()) {
      FWLOGERROR("orbit_room {} invoke async_matchsvr_room_start_failed failed, result: {}", room_ptr->get_client_id(),
                 *invoke_result.get_error());
    }
  }

  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_FINISH);
  exit_reason_ = exit_reason;

  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_orbit_room_status(room_status_);
  *event_log.mutable_room_key() = room_key_;
  event_log.mutable_client_exit()->mutable_exit_info()->set_exit_reason(exit_reason);
  add_event_log(ctx, std::move(event_log));

  // 结算未结算的用户
  for (auto& user_data : user_data_index_) {
    if (user_data.second->init_ && !user_data.second->finish_) {
      user_data.second->finish_ = true;
      user_data.second->finish_timepoint_ = util::time::time_utility::get_now();
      FWLOGWARNING("orbit_room {} room_finish, user not finish, user_key: {}:{}", get_client_id(),
                   user_data.first.user_id(), user_data.first.zone_id());
    }
  }
  return 0;
}

void orbit_room::async_user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr) {
  if (!user_ptr->finish_) {
    return;
  }
  if (user_ptr->settlement_finish_) {
    return;
  }
  if (user_ptr->settlement_retry_count_ >= 3) {
    return;
  }
  if (!task_type_trait::empty(user_ptr->settlement_task_) && !task_type_trait::is_exiting(user_ptr->settlement_task_)) {
    return;
  }

  auto room_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room.async_user_settlement", [user_ptr, room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        int32_t ret = RPC_AWAIT_CODE_RESULT(room_ptr->user_settlement(child_ctx, user_ptr));
        if (ret != 0) {
          FWLOGERROR("orbit_room {} add orbit_finish async job failed for user {},{} ret: {}",
                     room_ptr->get_client_id(), user_ptr->user_key_.user_id(), user_ptr->user_key_.zone_id(), ret);
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke add orbit_finish async job failed for user {},{} result: {}",
               room_ptr->get_client_id(), user_ptr->user_key_.user_id(), user_ptr->user_key_.zone_id(),
               *invoke_result.get_error());
    return;
  }
  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    user_ptr->settlement_task_ = std::move(*invoke_result.get_success());
  }
}

rpc::result_code_type orbit_room::user_settlement(rpc::context& ctx, orbit_room_user_data_ptr_t user_ptr) {
  if (!user_ptr->finish_) {
    FWLOGERROR("orbit_room {} user_settlement failed, user not finish, user_key: {}:{}", get_client_id(),
               user_ptr->init_data_.user_key().user_key().user_id(),
               user_ptr->init_data_.user_key().user_key().zone_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_USER_STATUS_INVALID);
  }
  if (user_ptr->settlement_finish_) {
    FWLOGERROR("orbit_room {} user_settlement failed, user settlement already done, user_key: {}:{}", get_client_id(),
               user_ptr->init_data_.user_key().user_key().user_id(),
               user_ptr->init_data_.user_key().user_key().zone_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ORBIT_ROOM_USER_SETTLEMENT_ALREADY_FINISH);
  }

  auto req = rpc::make_shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data>(ctx);
  PROJECT_NAMESPACE_ID::DOrbitUserFinishAsyncData& async_data = *req->mutable_orbit_finish()->mutable_data();
  req->set_action_uuid(atfw::util::string::format("{}-{}-{}", get_client_id(),
                                                  user_ptr->init_data_.user_key().user_key().user_id(),
                                                  user_ptr->init_data_.user_key().user_key().zone_id()));
  *async_data.mutable_room_key() = room_key_;
  async_data.set_start_timepoint(create_timepoint_);
  async_data.set_finish_timepoint(user_ptr->finish_timepoint_);
  async_data.mutable_exit_info()->set_exit_reason(exit_reason_);
  async_data.set_init_success(user_ptr->init_);
  if (user_ptr->init_) {
    // 没有init成功就不需要其余数据了
    for (const auto& pair : user_data_index_) {
      *async_data.add_user_init_datas() = pair.second->init_data_;
    }

    bool self_find = false;
    for (const auto& user_key : finish_user_list_) {
      if (self_find) {
        break;
      }
      if (user_key.user_id() == user_ptr->init_data_.user_key().user_key().user_id() &&
          user_key.zone_id() == user_ptr->init_data_.user_key().user_key().zone_id()) {
        self_find = true;
      }
      auto user_iter = user_data_index_.find(user_key);
      if (user_iter == user_data_index_.end()) {
        FWLOGERROR("orbit_room {} user_settlement failed, user not found in index, user_key: {}:{}", get_client_id(),
                   user_key.user_id(), user_key.zone_id());
        continue;
      }
      *async_data.add_user_finish_results() = user_iter->second->finish_result_;
    }
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(
      ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL, user_ptr->init_data_.user_key().user_key().user_id(),
      user_ptr->init_data_.user_key().user_key().zone_id(), req));
  if (ret != 0) {
    FWLOGERROR("orbit_room {} add orbit_finish async job failed for user {},{} ret: {}", get_client_id(),
               user_ptr->init_data_.user_key().user_key().user_id(),
               user_ptr->init_data_.user_key().user_key().zone_id(), ret);
    user_ptr->settlement_retry_count_++;
    if (user_ptr->settlement_retry_count_ >= 3) {
      FWLOGERROR("orbit_room {} user_settlement failed too many times for user {},{}", get_client_id(),
                 user_ptr->init_data_.user_key().user_key().user_id(),
                 user_ptr->init_data_.user_key().user_key().zone_id());
    } else {
      need_retry_settlement_ = true;
    }
  } else {
    user_ptr->settlement_finish_ = true;
  }
  RPC_RETURN_CODE(ret);
}

int32_t orbit_room::add_event_log(rpc::context& ctx, PROJECT_NAMESPACE_ID::DOrbitRoomEventLog&& event_log) {
  rpc::context::message_holder<atfw::dtmq::channel_subscriber> sender_info{ctx};
  rpc::context::message_holder<atfw::dtmq::DChannelMessageDetail> message_detail{ctx};

  sender_info->set_subscriber_key(
      atfw::util::log::format("orbit_server:{}", logic_config::me()->get_local_server_id()));
  if (!message_detail->mutable_event()->PackFrom(event_log)) {
    FWLOGERROR("orbit_room {} pack DOrbitRoomEventLog failed", get_client_id());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room.add_event_log",
      [sender = std::move(*sender_info), channel_key = channel_key_,
       detail = std::move(*message_detail)](rpc::context& child_ctx) mutable -> rpc::result_code_type {
        int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(child_ctx, std::move(sender), channel_key,
                                                                    std::move(detail), nullptr, nullptr, true, false));
        if (ret != 0) {
          FWLOGERROR("orbit_room {} send_message failed, ret: {}", channel_key.channel_id(), ret);
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke send_message failed, result: {}", get_client_id(), *invoke_result.get_error());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }
  return 0;
}
