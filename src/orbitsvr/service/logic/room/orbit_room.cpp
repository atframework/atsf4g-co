// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

#include "logic/room/orbit_room.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_utils.h>

#include <rpc/async_jobs/async_jobs.h>
#include <rpc/orbit_client_rpc/orbitclientrpcservice.atfw.gen.h>

#include "logic/room/orbit_room_wal_handle.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.orbit.protocol.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

namespace {
// 阶段超时（秒）：步骤 4 先使用常量占位，后续接入配置
constexpr int64_t kOrbitRoomClientLoadingTimeoutSec = 60;
constexpr int64_t kOrbitRoomUserInitTimeoutSec = 60;
}  // namespace

orbit_room::orbit_room(const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                       const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& room_data)
    : room_key_(room_key), room_data_(room_data) {
  wal_handle_ = std::make_shared<orbit_room_wal_handle>(*this);
}

orbit_room::~orbit_room() = default;

const std::string& orbit_room::get_client_id() const noexcept { return room_key_.client_id(); }

const PROJECT_NAMESPACE_ID::DOrbitRoomKey& orbit_room::get_room_key() const noexcept { return room_key_; }

const PROJECT_NAMESPACE_ID::DOrbitRoomInitData& orbit_room::get_room_data() const noexcept { return room_data_; }

PROJECT_NAMESPACE_ID::EnOrbitRoomStatus orbit_room::get_status() const noexcept { return status_; }

int32_t orbit_room::set_status(PROJECT_NAMESPACE_ID::EnOrbitRoomStatus v) {
  status_ = v;
  return 0;
}

int32_t orbit_room::create(rpc::context& ctx) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_INVALID != status_) {
    FWLOGERROR("orbit_room {} create failed, status: {}", get_client_id(), static_cast<int32_t>(status_));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  create_timepoint_ = util::time::time_utility::get_now();
  status_end_timepoint_ = create_timepoint_ + kOrbitRoomClientLoadingTimeoutSec;
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING);

  // 写 start_loading 事件
  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_event_id(wal_handle_->alloc_event_id());
  event_log.mutable_timepoint()->set_seconds(create_timepoint_);
  event_log.set_orbit_room_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING);
  event_log.mutable_room_key()->CopyFrom(room_key_);
  event_log.mutable_start_loading();  // 空事件体：开始加载 Client

  wal_handle_->add_event_log(ctx, std::move(event_log));
  wal_handle_->broadcast_events(ctx);
  return 0;
}

int32_t orbit_room::on_client_start(rpc::context& ctx, const std::string& client_addr) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADING != status_) {
    FWLOGERROR("orbit_room {} on_client_start failed, status: {}", get_client_id(), static_cast<int32_t>(status_));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  client_address_ = client_addr;
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADED);
  status_end_timepoint_ = util::time::time_utility::get_now() + kOrbitRoomUserInitTimeoutSec;

  // 写 finish_loading 事件
  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_event_id(wal_handle_->alloc_event_id());
  event_log.mutable_timepoint()->set_seconds(util::time::time_utility::get_now());
  event_log.set_orbit_room_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADED);
  event_log.mutable_room_key()->CopyFrom(room_key_);
  event_log.mutable_finish_loading()->set_client_address(client_addr);

  wal_handle_->add_event_log(ctx, std::move(event_log));
  wal_handle_->broadcast_events(ctx);
  return 0;
}

int32_t orbit_room::join_users(
    rpc::context& ctx, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserInitData>& users) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_CLIENT_LOADED != status_) {
    FWLOGERROR("orbit_room {} join_users failed, status: {}", get_client_id(), static_cast<int32_t>(status_));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  for (const auto& user : users) {
    user_index_[user.user_key()] = user;
  }
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_INITING);

  // 异步调 Client user_init，成功后写 user_init_success 事件 -> USER_RUNNING
  auto room_ptr = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "orbit_room.join_users", [room_ptr](rpc::context& child_ctx) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::OrbitClientUserInitReq req;
        for (const auto& pair : room_ptr->user_index_) {
          *req.add_user_data() = pair.second;
        }
        req.set_init_finish(true);

        PROJECT_NAMESPACE_ID::OrbitClientUserInitRsp rsp;
        int32_t ret =
            RPC_AWAIT_CODE_RESULT(rpc::orbit_client_rpc::user_init(child_ctx, room_ptr->get_client_id(), req, rsp));
        if (ret != 0 || rsp.ret_code() != 0) {
          FWLOGERROR("orbit_room {} user_init failed, ret: {}, rsp ret: {}", room_ptr->get_client_id(), ret,
                     rsp.ret_code());
          room_ptr->on_client_end(child_ctx);
          RPC_RETURN_CODE(ret != 0 ? ret : rsp.ret_code());
        }

        // 逐个用户写 user_init_success 事件（回填 token）
        for (const auto& result : rsp.data()) {
          room_ptr->user_init_result_index_[result.user_key()] = result;

          PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
          event_log.set_event_id(room_ptr->wal_handle_->alloc_event_id());
          event_log.mutable_timepoint()->set_seconds(util::time::time_utility::get_now());
          event_log.set_orbit_room_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_INITING);
          event_log.mutable_room_key()->CopyFrom(room_ptr->room_key_);
          event_log.mutable_user_init_success()->mutable_init_result()->CopyFrom(result);
          room_ptr->wal_handle_->add_event_log(child_ctx, std::move(event_log));
        }

        room_ptr->set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_RUNNING);
        room_ptr->wal_handle_->broadcast_events(child_ctx);
        RPC_RETURN_CODE(0);
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("orbit_room {} invoke join_users failed, result: {}", get_client_id(),
               *invoke_result.get_error());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
  }
  return 0;
}

int32_t orbit_room::on_user_finish(
    rpc::context& ctx, const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_RUNNING != status_) {
    FWLOGERROR("orbit_room {} on_user_finish failed, status: {}", get_client_id(), static_cast<int32_t>(status_));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  if (results.empty()) {
    FWLOGWARNING("orbit_room {} on_user_finish with empty results, room will exit directly", get_client_id());
  }

  const int64_t finish_timepoint = util::time::time_utility::get_now();
  user_finish_results_.assign(results.begin(), results.end());
  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_FINISH);

  // 逐个用户写 user_finish 事件（104）
  for (const auto& result : results) {
    PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
    event_log.set_event_id(wal_handle_->alloc_event_id());
    event_log.mutable_timepoint()->set_seconds(finish_timepoint);
    event_log.set_orbit_room_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_USER_FINISH);
    event_log.mutable_room_key()->CopyFrom(room_key_);
    event_log.mutable_user_finish()->mutable_user_key()->CopyFrom(result.user_key());
    wal_handle_->add_event_log(ctx, std::move(event_log));
  }
  wal_handle_->broadcast_events(ctx);

  // 组装房间完整结算上下文（每个玩家都携带全量 init 数据 + finish 结果）
  PROJECT_NAMESPACE_ID::DOrbitUserFinishAsyncData async_data;
  async_data.mutable_room_key()->CopyFrom(room_key_);
  async_data.set_start_timepoint(create_timepoint_);
  async_data.set_finish_timepoint(finish_timepoint);
  async_data.mutable_exit_info()->set_exit_reason(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_USER_FINISH);
  for (const auto& pair : user_index_) {
    *async_data.add_user_init_datas() = pair.second;
  }
  for (const auto& result : results) {
    *async_data.add_user_finish_results() = result;
  }

  // 每个玩家各落一个 orbit_finish 异步任务（步骤 5 核心；结算消费方后续自主实现）
  auto room_ptr = shared_from_this();
  for (const auto& result : results) {
    const PROJECT_NAMESPACE_ID::DUserIDKey user_key = result.user_key();
    auto invoke_result = rpc::async_invoke(
        ctx, "orbit_room.on_user_finish.add_job",
        [room_ptr, user_key, async_data](rpc::context& child_ctx) -> rpc::result_code_type {
          rpc::shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data> async_job{child_ctx};
          async_job->mutable_orbit_finish()->mutable_data()->CopyFrom(async_data);

          int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(child_ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL,
                                                                         user_key.user_id(), user_key.zone_id(),
                                                                         async_job));
          if (ret != 0) {
            FWLOGERROR("orbit_room {} add orbit_finish async job failed for user {},{} ret: {}",
                       room_ptr->get_client_id(), user_key.user_id(), user_key.zone_id(), ret);
          }
          RPC_RETURN_CODE(ret);
        });
    if (invoke_result.is_error()) {
      FWLOGERROR("orbit_room {} invoke add orbit_finish async job failed for user {},{} result: {}", get_client_id(),
                 user_key.user_id(), user_key.zone_id(), *invoke_result.get_error());
    }
  }

  // 全部用户结束：写 client_exit 事件（USER_FINISH）-> EXIT，内存清理由 manager tick 完成
  return on_client_end(ctx, PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_EXIT_REASON_USER_FINISH);
}

int32_t orbit_room::on_client_end(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnOrbitRoomExitReason exit_reason) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT == status_) {
    return 0;
  }

  set_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT);

  // 写 client_exit 事件
  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  event_log.set_event_id(wal_handle_->alloc_event_id());
  event_log.mutable_timepoint()->set_seconds(util::time::time_utility::get_now());
  event_log.set_orbit_room_status(PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT);
  event_log.mutable_room_key()->CopyFrom(room_key_);
  event_log.mutable_client_exit()->mutable_exit_info()->set_exit_reason(exit_reason);

  wal_handle_->add_event_log(ctx, std::move(event_log));
  wal_handle_->broadcast_events(ctx);
  return 0;
}

void orbit_room::dump(PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData& out) const {
  PROJECT_NAMESPACE_ID::DOrbitRoomRunningData* running = out.mutable_running_data();
  running->set_room_status(status_);
  running->set_status_end_timepoint(status_end_timepoint_);
  running->mutable_room_key()->CopyFrom(room_key_);
  running->set_create_timepoint(create_timepoint_);
  running->set_client_address(client_address_);
  if (room_data_.has_map_data()) {
    running->mutable_map_data()->CopyFrom(room_data_.map_data());
  }
  for (const auto& pair : user_init_result_index_) {
    *running->add_user_init() = pair.second;
  }
  for (const auto& result : user_finish_results_) {
    *running->add_user_finish() = result.user_key();
  }
}

int32_t orbit_room::tick(rpc::context& ctx, int64_t now) {
  if (PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT == status_) {
    return 0;
  }
  if (status_end_timepoint_ > 0 && now >= status_end_timepoint_) {
    FWLOGWARNING("orbit_room {} timeout, status: {}", get_client_id(), static_cast<int32_t>(status_));
    return on_client_end(ctx);
  }
  return 0;
}
