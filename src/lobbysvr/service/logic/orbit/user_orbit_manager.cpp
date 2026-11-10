// Copyright 2026 atframework

#include "logic/orbit/user_orbit_manager.h"

#include <string/string_format.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>
#include <logic/logic_server_setup.h>
#include <memory/object_allocator.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h>
#include <rpc/orbit/orbitsvrservice.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <std/explicit_declare.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/async_jobs/user_async_jobs_manager.h>

#include <chrono>
#include <memory>
#include <string>

#include "data/session.h"
#include "data/user.h"
#include "logic/async_jobs/task_action_user_remote_patch_jobs.h"

namespace {
// 超过该秒数没有收到 orbitsvr 的房间心跳，认为房间已经不存在
static constexpr auto kOrbitRoomHeartbeatTimeout = std::chrono::seconds(120);
// 判定房间过期后，留给结算/清理流程的容忍时间
static constexpr auto kOrbitRoomExpireDelay = std::chrono::seconds(30);

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_shared_orbit_channel_event_callback_set() {
  rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      rpc::dtmq::client_subscriber::create_event_callback_set();

  rpc::dtmq::client_subscriber::set_event_callback_on_ready(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
        auto local_private_data = subscriber->get_local_private_data();
        if (local_private_data.empty()) {
          FWLOGERROR("user_orbit_manager on_ready callback missing local_private_data");
          return;
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        user_orbit_manager* orbit_mgr = reinterpret_cast<user_orbit_manager*>(local_private_data[0]);
        if (!orbit_mgr) {
          FWLOGERROR("user_orbit_manager on_ready callback missing orbit_mgr");
          return;
        }
        orbit_mgr->load_orbit_room_snapshot(ctx, subscriber);
        orbit_mgr->get_owner().send_all_syn_msg(logic_server_get_current_tick_context());
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelMessage& data) {
        auto local_private_data = subscriber->get_local_private_data();
        if (local_private_data.empty()) {
          FWLOGERROR("user_orbit_manager on_receive_event callback missing local_private_data");
          return;
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        user_orbit_manager* orbit_mgr = reinterpret_cast<user_orbit_manager*>(local_private_data[0]);
        if (!orbit_mgr) {
          FWLOGERROR("user_orbit_manager on_receive_event callback missing orbit_mgr");
          return;
        }
        orbit_mgr->on_receive_event(ctx, subscriber, data);
        orbit_mgr->get_owner().send_all_syn_msg(logic_server_get_current_tick_context());
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber, int64_t,
               std::chrono::system_clock::time_point) {
        auto local_private_data = subscriber->get_local_private_data();
        if (local_private_data.empty()) {
          FWLOGERROR("user_orbit_manager on_destroyed callback missing local_private_data");
          return;
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        user_orbit_manager* orbit_mgr = reinterpret_cast<user_orbit_manager*>(local_private_data[0]);
        if (!orbit_mgr) {
          FWLOGERROR("user_orbit_manager on_destroyed callback missing orbit_mgr");
          return;
        }
        orbit_mgr->on_channel_destroyed(ctx, subscriber);
        orbit_mgr->get_owner().send_all_syn_msg(logic_server_get_current_tick_context());
      });
  return ret;
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t& get_shared_orbit_channel_event_callback_set() {
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret = build_shared_orbit_channel_event_callback_set();
  return ret;
}

static bool init_user_orbit_manager_handle() {
  user::init_get_info_handle(PROJECT_NAMESPACE_ID::CSUserGetInfoReq::descriptor()->FindFieldByNumber(
                                 PROJECT_NAMESPACE_ID::CSUserGetInfoReq::kNeedUserOrbitRoomFieldNumber),
                             [](rpc::context&, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
                               auto& orbit_mgr = user_inst.get_user_orbit_manager();
                               orbit_mgr.fetch_user_data(*rsp.mutable_user_orbit_room());
                             });
  task_action_user_remote_patch_jobs::register_async_callbacks(
      static_cast<int32_t>(PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::kOrbitFinish),
      [](rpc::context& ctx, user& user_inst, int32_t /*job_type*/,
         const task_action_user_remote_patch_jobs::async_job_ptr_type& job_data) -> rpc::result_code_type {
        PROJECT_NAMESPACE_ID::user_async_job_orbit_finish& orbit_finish_data = *job_data->mutable_orbit_finish();
        RPC_AWAIT_IGNORE_VOID(
            user_inst.get_user_orbit_manager().receive_orbit_settlement(ctx, *orbit_finish_data.mutable_data()));
        RPC_RETURN_CODE(0);
      });
  task_action_user_remote_patch_jobs::register_sync_callbacks(
      static_cast<int32_t>(PROJECT_NAMESPACE_ID::user_async_jobs_blob_data::kOrbitRemoteStart),
      [](rpc::context& ctx, user& user_inst, int32_t /*job_type*/,
         const task_action_user_remote_patch_jobs::async_job_ptr_type& job_data) -> int32_t {
        const PROJECT_NAMESPACE_ID::user_async_job_orbit_remote_start& orbit_remote_start_data =
            job_data->orbit_remote_start();
        user_inst.get_user_orbit_manager().receive_orbit_remote_start(ctx, orbit_remote_start_data.arg());
        return 0;
      });
  return true;
}

}  // namespace

user_orbit_manager::user_orbit_manager(user& owner) : owner_(&owner) {
  ATFW_EXPLICIT_UNUSED_ATTR static bool init_handle = init_user_orbit_manager_handle();
}

void user_orbit_manager::refresh_feature_limit_second(rpc::context& ctx) {
  do {
    if (!subscriber_ready_) {
      // 等subscriber ready
      break;
    }
    auto now = ctx.logical_now();
    if (room_cache_.end_join_timepoint > std::chrono::system_clock::from_time_t(0) &&
        now >= room_cache_.end_join_timepoint) {
      // 结束加入时间点到了 但是没有收到加入消息
      FCTXLOGERROR(
          ctx,
          "{} user_orbit_manager refresh_feature_limit_second orbit room end join timepoint reached but no join "
          "event, clear orbit room data {}",
          *owner_, room_cache_.room_key.client_id());
      clear_orbit_room_data();
      break;
    }

    // 两分钟没收到房间心跳，认为房间已经不存在：把过期时间设到 30 秒后，交给下面的过期流程结算/清理
    if (is_orbit_room_exist()) {
      bool room_finished =
          room_data_ &&
          (room_data_->finish_event_ || PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_FINISH == room_data_->room_status_ ||
           PROJECT_NAMESPACE_ID::EN_ORBIT_ROOM_STATUS_EXIT == room_data_->room_status_);
      if (!room_finished) {
        if (last_heartbeat_timepoint_ <= std::chrono::system_clock::from_time_t(0)) {
          // 恢复房间数据后从当前时间开始算
          last_heartbeat_timepoint_ = now;
        } else if (now - last_heartbeat_timepoint_ >= kOrbitRoomHeartbeatTimeout) {
          auto next_expired_timepoint = now;
          next_expired_timepoint += kOrbitRoomExpireDelay;
          if (room_cache_.expired_timepoint <= std::chrono::system_clock::from_time_t(0) ||
              room_cache_.expired_timepoint > next_expired_timepoint) {
            FWLOGWARNING(
                "{} user_orbit_manager orbit room heartbeat timeout, last_heartbeat_timepoint: {}, expired_timepoint: "
                "{} -> {}",
                *owner_, last_heartbeat_timepoint_, room_cache_.expired_timepoint, next_expired_timepoint);
            room_cache_.expired_timepoint = next_expired_timepoint;
            mark_dirty();
          }
        }
      }
    }

    if (room_cache_.expired_timepoint > std::chrono::system_clock::from_time_t(0) &&
        now >= room_cache_.expired_timepoint) {
      // 过期了
      if (owner_->get_user_async_jobs_manager().is_async_jobs_task_running()) {
        // 等异步任务完成
        break;
      }
      if (room_data_) {
        if (!room_data_->finish_event_) {
          // 过期但是没有结算 可能是挂了
          FWLOGERROR(
              "{} user_orbit_manager refresh_feature_limit_second orbit room expired but no finish event, clear "
              "orbit room data {}",
              *owner_, room_cache_.room_key.client_id());
          clear_orbit_room_data();
          break;
        }

        // 存在结算 但是没有异步任务
        FWLOGERROR(
            "{} user_orbit_manager refresh_feature_limit_second orbit room expired but no async jobs, clear "
            "orbit room data {}",
            *owner_, room_cache_.room_key.client_id());
        clear_orbit_room_data();
        break;
      }

      // 数据移除
      FWLOGERROR(
          "{} user_orbit_manager refresh_feature_limit_second orbit room expired but no room data, clear orbit "
          "room data {}",
          *owner_, room_cache_.room_key.client_id());
      clear_orbit_room_data();
    }
  } while (false);
}

rpc::result_code_type user_orbit_manager::login_init(rpc::context& ctx) {
  subscriber_key_ = atfw::util::string::format("user:{}:{}", owner_->get_zone_id(), owner_->get_user_id());
  // 如果存在数据则创建房间
  if (is_orbit_room_exist()) {
    int32_t ret = create_room(ctx, room_cache_.room_key, room_cache_.faction_id, room_cache_.end_join_timepoint,
                              room_cache_.expired_timepoint);
    if (ret != 0) {
      FWLOGERROR("user_orbit_manager login_init create_room failed: {} for room: {}", ret,
                 room_cache_.room_key.client_id());
    }
  }
  RPC_RETURN_CODE(0);
}

void user_orbit_manager::fetch_user_data(PROJECT_NAMESPACE_ID::DOrbitRoomUserData& user_data) const {
  if (room_data_) {
    *user_data.mutable_room_key() = room_cache_.room_key;
    user_data.set_client_address(room_data_->ready_data_.client_address());
    user_data.set_init(room_data_->is_joined_);
    user_data.set_token(room_data_->init_result_.token());
    user_data.set_client_template_id(room_data_->init_data_.client_template_id());
    user_data.set_region(room_data_->init_data_.region());
    user_data.set_create_timepoint(room_data_->init_data_.create_timepoint());
  }
}

void user_orbit_manager::init_from_table_data(rpc::context&, const PROJECT_NAMESPACE_ID::table_user& user_table) {
  room_cache_.room_key = user_table.orbit_room_data().room_key();
  room_cache_.faction_id = user_table.orbit_room_data().faction_id();
  // FIXME(youxongyang): 协议使用原生Timestamp类型
  room_cache_.end_join_timepoint =
      std::chrono::system_clock::from_time_t(user_table.orbit_room_data().user_end_join_timepoint());
  room_cache_.expired_timepoint =
      std::chrono::system_clock::from_time_t(user_table.orbit_room_data().expired_timepoint());
}

int user_orbit_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user_table) const {
  *user_table.mutable_orbit_room_data()->mutable_room_key() = room_cache_.room_key;
  user_table.mutable_orbit_room_data()->set_faction_id(room_cache_.faction_id);
  user_table.mutable_orbit_room_data()->set_expired_timepoint(
      std::chrono::system_clock::to_time_t(room_cache_.expired_timepoint));
  user_table.mutable_orbit_room_data()->set_user_end_join_timepoint(
      std::chrono::system_clock::to_time_t(room_cache_.end_join_timepoint));
  return 0;
}

bool user_orbit_manager::is_orbit_room_exist() const { return !room_cache_.room_key.client_id().empty(); }

int32_t user_orbit_manager::create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                                        int32_t faction_id, std::chrono::system_clock::time_point end_join_timepoint,
                                        std::chrono::system_clock::time_point expired_timepoint) {
  mark_dirty();
  room_cache_.room_key = room_key;
  room_cache_.faction_id = faction_id;
  room_cache_.end_join_timepoint = end_join_timepoint;
  room_cache_.expired_timepoint = expired_timepoint;
  last_heartbeat_timepoint_ = ctx.logical_now();

  // 创建subscriber并订阅频道
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};
  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
  subscribe_options.auto_create_channel = false;
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(PROJECT_NAMESPACE_ID::EN_ORBIT_CHANNEL_TYPE_ROOM);
  channel_key.set_channel_id(
      rpc::dtmq::make_unicast_channel_id(PROJECT_NAMESPACE_ID::EN_ORBIT_CHANNEL_TYPE_ROOM, 0, room_key.client_id()));
  subscriber_ = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
  if (!subscriber_) {
    FWLOGERROR("Failed to create world chat channel {}:{}, maybe configure is missing.", channel_key.channel_type(),
               channel_key.channel_id());
    clear_orbit_room_data();
    return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND;
  }
  subscriber_->set_local_private_data(local_private_data);

  // 初始化房间数据
  room_data_ = atfw::component::memory::stl::make_strong_rc<orbit_room_data>();
  // 恢复房间快照
  if (subscriber_->is_ready()) {
    load_orbit_room_snapshot(ctx, subscriber_);
  }

  // 设置回调
  if (!subscriber_->get_shared_event_callback_set()) {
    subscriber_->set_shared_event_callback_set(get_shared_orbit_channel_event_callback_set());
  }
  return 0;
}

int32_t user_orbit_manager::join_orbit_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                                            int32_t faction_id) {
  if (is_orbit_room_exist()) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager already in orbit room: {}", *owner_, room_cache_.room_key.client_id());
    return PROJECT_NAMESPACE_ID::EN_ERR_ORBIT_ALREADY_IN_ROOM;
  }
  // 容忍时间 需要10秒处理状态
  std::chrono::system_clock::time_point end_join_timepoint = ctx.logical_now();
  end_join_timepoint += std::chrono::seconds(10);
  // expired_timepoint 这里用 end_join_timepoint
  int32_t ret = create_room(ctx, room_key, faction_id, end_join_timepoint, end_join_timepoint);
  if (ret != 0) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager create_room failed: {} for room: {}", *owner_, ret, room_key.client_id());
    return ret;
  }

  FCTXLOGINFO(ctx, "{} user_orbit_manager joined orbit room: {}", *owner_, room_cache_.room_key.client_id());
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

void user_orbit_manager::load_orbit_room_snapshot(rpc::context& ctx,
                                                  const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (!subscriber) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager load_orbit_room_snapshot missing subscriber", *owner_);
    return;
  }
  if (subscriber != subscriber_) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager load_orbit_room_snapshot subscriber mismatch", *owner_);
    return;
  }
  subscriber_ready_ = true;
  subscriber_->query_cached_message(ctx, [this, &ctx](const atfw::dtmq::DChannelMessage& msg) {
    if (msg.detail().event().type_url().empty()) {
      return true;
    }
    PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
    if (!msg.detail().event().UnpackTo(&event_log)) {
      return true;
    }
    on_receive_event(ctx, event_log);
    return true;
  });
}

rpc::result_void_type user_orbit_manager::receive_orbit_settlement(
    rpc::context& ctx, PROJECT_NAMESPACE_ID::DOrbitUserFinishAsyncData& finish_data) {
  // 收到结算的时候 一定存在key 否则丢弃
  if (!is_orbit_room_exist()) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager receive_orbit_settlement missing orbit room key {}", *owner_,
                 room_cache_.room_key.client_id());
    RPC_RETURN_VOID;
  }
  // 结算不匹配 丢弃
  if (finish_data.room_key().client_id() != room_cache_.room_key.client_id()) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager receive_orbit_settlement room key mismatch: {}", *owner_,
                 finish_data.room_key().client_id());
    RPC_RETURN_VOID;
  }
  if (!finish_data.init_success()) {
    // 没有成功直接删除
    FCTXLOGERROR(ctx, "{} user_orbit_manager receive_orbit_settlement init failed, clear orbit room data {}", *owner_,
                 room_cache_.room_key.client_id());
    clear_orbit_room_data();
    RPC_RETURN_VOID;
  }
  // TODO(yousongyang) 处理结果
  clear_orbit_room_data();
  RPC_RETURN_VOID;
}

void user_orbit_manager::receive_orbit_remote_start(rpc::context& ctx,
                                                    const PROJECT_NAMESPACE_ID::DOrbitRemoteStartArg& arg) {
  auto sync_message = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SCOrbitRemoteStartSync>(ctx);
  *sync_message->mutable_arg() = arg;
  auto sess = owner_->get_session();
  if (sess) {
    int32_t send_result = rpc::lobbysvrclientservice::send_orbit_remote_start_sync(ctx, *sync_message, *sess).unwrap();
    if (send_result < 0) {
      FCTXLOGERROR(ctx, "{} send orbit remote start failed, error={}({})", *owner_, send_result,
                   protobuf_mini_dumper_get_error_msg(send_result));
    }
  }
}

void user_orbit_manager::on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                          const ::atfw::dtmq::DChannelMessage& data) {
  if (!subscriber) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager load_orbit_room_snapshot missing subscriber", *owner_);
    return;
  }
  if (subscriber != subscriber_) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager load_orbit_room_snapshot subscriber mismatch", *owner_);
    return;
  }
  if (data.detail().event().type_url().empty()) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager on_receive_event missing type_url", *owner_);
    return;
  }
  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  if (!data.detail().event().UnpackTo(&event_log)) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager on_receive_event failed to unpack event", *owner_);
    return;
  }
  on_receive_event(ctx, event_log);
}

void user_orbit_manager::on_channel_destroyed(rpc::context& ctx,
                                              const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (!subscriber) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager on_channel_destroyed missing subscriber", *owner_);
    return;
  }
  if (subscriber != subscriber_) {
    FCTXLOGERROR(ctx, "{} user_orbit_manager on_channel_destroyed subscriber mismatch", *owner_);
    return;
  }
  // 过期时间设置为当前时间加容忍值
  subscriber_ready_ = true;
  room_cache_.expired_timepoint = ctx.logical_now();
  room_cache_.expired_timepoint += kOrbitRoomExpireDelay;
  FCTXLOGWARNING(ctx, "{} user_orbit_manager on_channel_destroyed room {}", *owner_, room_cache_.room_key.client_id());
}

int32_t user_orbit_manager::dump_orbit_init_data(PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail& /*init_data*/) {
  return PROJECT_NAMESPACE_ID::EN_SUCCESS;
}

void user_orbit_manager::on_receive_event(rpc::context& ctx,
                                          const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog& event_log) {
  room_data_->room_status_ = event_log.orbit_room_status();
  switch (event_log.event_case()) {
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kRoomInit:
      room_data_->init_data_ = event_log.room_init();
      room_cache_.expired_timepoint = std::chrono::system_clock::from_time_t(event_log.room_init().expired_timepoint());
      mark_dirty();
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kReadyData:
      room_data_->ready_data_ = event_log.ready_data();
      mark_dirty();
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kUserInitSuccess:
      if (event_log.user_init_success().init_result().user_key().user_key().user_id() == owner_->get_user_id() &&
          event_log.user_init_success().init_result().user_key().user_key().zone_id() == owner_->get_zone_id()) {
        room_data_->init_result_ = event_log.user_init_success().init_result();
        room_data_->is_joined_ = true;
        room_cache_.end_join_timepoint = std::chrono::system_clock::from_time_t(0);
        mark_dirty();
      }
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kUserFinish:
      if (event_log.user_finish().user_key().user_id() == owner_->get_user_id() &&
          event_log.user_finish().user_key().zone_id() == owner_->get_zone_id()) {
        room_data_->finish_event_ = true;
        mark_dirty();
      }
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kClientExit:
      room_data_->exit_info_ = event_log.client_exit().exit_info();
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kRoomHeartbeat:
      last_heartbeat_timepoint_ = ctx.logical_now();
      break;
    default:
      break;
  }
}

void user_orbit_manager::clear_orbit_room_data() {
  room_cache_.room_key.Clear();
  subscriber_ = nullptr;
  room_data_ = nullptr;
  room_cache_.end_join_timepoint = std::chrono::system_clock::from_time_t(0);
  room_cache_.expired_timepoint = std::chrono::system_clock::from_time_t(0);
  last_heartbeat_timepoint_ = std::chrono::system_clock::from_time_t(0);
  subscriber_ready_ = false;
  mark_dirty();
}

void user_orbit_manager::mark_dirty() {
  dirty_ = true;
  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user.user_orbit_manager.mark_dirty", [](gsl::string_view, user&) {
        user::dirty_sync_handle_t handle;
        handle.build_fn = [](rpc::context&, user& user_inst, user::dirty_message_container& output) {
          if (!user_inst.get_user_orbit_manager().dirty_) {
            return;
          }
          if (!output.user_dirty) {
            output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
          }
          auto& orbit_mgr = user_inst.get_user_orbit_manager();
          orbit_mgr.fetch_user_data(*output.user_dirty->mutable_dirty_orbit_rooms()->mutable_data());
        };
        handle.clear_fn = [](rpc::context&, user& user_inst) { user_inst.get_user_orbit_manager().dirty_ = false; };
        return handle;
      });
}
