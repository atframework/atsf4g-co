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
#include <rpc/orbit/orbitsvrservice.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <std/explicit_declare.h>

#include <utility/protobuf_mini_dumper.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/user.h"
#include "data/session.h"

namespace {
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
        user_orbit_manager* orbit_mgr = reinterpret_cast<user_orbit_manager*>(local_private_data[0]);
        if (!orbit_mgr) {
          FWLOGERROR("user_orbit_manager on_ready callback missing orbit_mgr");
          return;
        }
        orbit_mgr->load_orbit_room_snapshot(ctx, subscriber);
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_event(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelMessage& data) {
        auto local_private_data = subscriber->get_local_private_data();
        if (local_private_data.empty()) {
          FWLOGERROR("user_orbit_manager on_receive_event callback missing local_private_data");
          return;
        }
        user_orbit_manager* orbit_mgr = reinterpret_cast<user_orbit_manager*>(local_private_data[0]);
        if (!orbit_mgr) {
          FWLOGERROR("user_orbit_manager on_receive_event callback missing orbit_mgr");
          return;
        }
        orbit_mgr->on_receive_event(ctx, subscriber, data);
      });
  return ret;
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t& get_shared_orbit_channel_event_callback_set() {
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret = build_shared_orbit_channel_event_callback_set();
  return ret;
}

}  // namespace

user_orbit_manager::user_orbit_manager(user& owner) : owner_(&owner) {
  static bool init_handle = false;
  if (!init_handle) {
    init_handle = true;
    user::init_get_info_handle(&PROJECT_NAMESPACE_ID::CSUserGetInfoReq::need_user_orbit_room,
                                 [](rpc::context&, PROJECT_NAMESPACE_ID::SCUserGetInfoRsp& rsp, user& user_inst) {
                                   auto& orbit_mgr = user_inst.get_user_orbit_manager();
                                   orbit_mgr.fetch_user_data(*rsp.mutable_user_orbit_room());
                                 });
  }
}

void user_orbit_manager::refresh_feature_limit_second(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {
  if (orbit_room_expired_timepoint_ > 0 && atfw::util::time::time_utility::get_now() >= orbit_room_expired_timepoint_) {
    FWLOGINFO("user_orbit_manager refresh_feature_limit_second orbit room expired, clear orbit room data");
    clear_orbit_room_data();
  }
}

rpc::result_code_type user_orbit_manager::login_init(rpc::context& ctx) {
  subscriber_key_ = atfw::util::string::format("user:{}:{}", owner_->get_zone_id(), owner_->get_user_id());
  // 如果存在数据则创建房间
  if (is_orbit_room_exist()) {
    int32_t ret = create_room(ctx, room_key_, orbit_room_expired_timepoint_);
    if (ret != 0) {
      FWLOGERROR("user_orbit_manager login_init create_room failed: {} for room: {}", ret, room_key_.client_id());
    }
  }
  RPC_RETURN_CODE(0);
}

void user_orbit_manager::fetch_user_data(PROJECT_NAMESPACE_ID::DOrbitRoomUserData& user_data) const {
  if (room_data_) {
    *user_data.mutable_room_key() = room_key_;
    user_data.set_client_address(room_data_->ready_data_.client_address());
    user_data.set_init(room_data_->is_joined_);
    user_data.set_token(room_data_->init_result_.token());
    user_data.set_client_template_id(room_data_->init_data_.client_template_id());
    user_data.set_region(room_data_->init_data_.region());
    user_data.set_create_timepoint(room_data_->init_data_.create_timepoint());
  }
}

void user_orbit_manager::init_from_table_data(rpc::context&, const PROJECT_NAMESPACE_ID::table_user& user_table) {
  room_key_ = user_table.orbit_room_data().room_key();
  orbit_room_expired_timepoint_ = user_table.orbit_room_data().expired_timepoint();
}

int user_orbit_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user) const {
  *user.mutable_orbit_room_data()->mutable_room_key() = room_key_;
  user.mutable_orbit_room_data()->set_expired_timepoint(orbit_room_expired_timepoint_);
  return 0;
}

bool user_orbit_manager::is_orbit_room_exist() const { return !room_key_.client_id().empty(); }

int32_t user_orbit_manager::create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                                        int64_t expired_timepoint) {
  mark_dirty();
  room_key_ = room_key;
  orbit_room_expired_timepoint_ = expired_timepoint;

  // 创建subscriber并订阅频道
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(this)};
  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key_};
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(PROJECT_NAMESPACE_ID::EN_ORBIT_CHANNEL_TYPE_ROOM);
  channel_key.set_channel_id(room_key_.client_id());
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

rpc::result_code_type user_orbit_manager::join_orbit_room(rpc::context& ctx,
                                                          const PROJECT_NAMESPACE_ID::DOrbitRoomKey& room_key,
                                                          uint64_t orbit_server_id, int64_t expired_timepoint) {
  if (is_orbit_room_exist()) {
    FWLOGERROR("user_orbit_manager already in orbit room: {}", room_key_.client_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_ORBIT_ALREADY_IN_ROOM);
  }
  int32_t ret = create_room(ctx, room_key, expired_timepoint);
  if (ret != 0) {
    FWLOGERROR("user_orbit_manager create_room failed: {} for room: {}", ret, room_key.client_id());
    RPC_RETURN_CODE(ret);
  }

  FWLOGINFO("user_orbit_manager joined orbit room: {}", room_key_.client_id());
  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

void user_orbit_manager::load_orbit_room_snapshot(rpc::context& ctx, rpc::dtmq::client_subscriber::ptr_t subscriber) {
  if (!subscriber) {
    FWLOGERROR("user_orbit_manager load_orbit_room_snapshot missing subscriber");
    return;
  }
  if (subscriber != subscriber_) {
    FWLOGERROR("user_orbit_manager load_orbit_room_snapshot subscriber mismatch");
    return;
  }
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

void user_orbit_manager::receive_orbit_settlement(
    ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
    ATFW_EXPLICIT_UNUSED_ATTR const PROJECT_NAMESPACE_ID::DOrbitUserFinishAsyncData& finish_data) {
  // 结算 用户TODO
  clear_orbit_room_data();
}

void user_orbit_manager::on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                          const ::atfw::dtmq::DChannelMessage& data) {
  if (!subscriber) {
    FWLOGERROR("user_orbit_manager load_orbit_room_snapshot missing subscriber");
    return;
  }
  if (subscriber != subscriber_) {
    FWLOGERROR("user_orbit_manager load_orbit_room_snapshot subscriber mismatch");
    return;
  }
  if (data.detail().event().type_url().empty()) {
    FWLOGERROR("user_orbit_manager on_receive_event missing type_url");
    return;
  }
  PROJECT_NAMESPACE_ID::DOrbitRoomEventLog event_log;
  if (!data.detail().event().UnpackTo(&event_log)) {
    FWLOGERROR("user_orbit_manager on_receive_event failed to unpack event");
    return;
  }
  on_receive_event(ctx, event_log);
}

void user_orbit_manager::on_receive_event(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                          const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog& event_log) {
  room_data_->room_status_ = event_log.orbit_room_status();
  switch (event_log.event_case()) {
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kRoomInit:
      room_data_->init_data_ = event_log.room_init();
      orbit_room_expired_timepoint_ = event_log.room_init().expired_timepoint();
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
        mark_dirty();
      }
      break;
    case PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::kClientExit:
      room_data_->exit_info_ = event_log.client_exit().exit_info();
      break;
    default:
      break;
  }
}

void user_orbit_manager::clear_orbit_room_data() {
  room_key_.Clear();
  subscriber_ = nullptr;
  room_data_ = nullptr;
  orbit_room_expired_timepoint_ = 0;
  mark_dirty();
}

void user_orbit_manager::mark_dirty() {
  dirty_ = true;
  owner_->insert_dirty_handle_if_not_exists(
      reinterpret_cast<uintptr_t>(this), "user.user_orbit_manager.mark_dirty", [](gsl::string_view, user&) {
        user::dirty_sync_handle_t handle;
        handle.build_fn = [](user& user_inst, user::dirty_message_container& output) {
          if (!user_inst.get_user_orbit_manager().dirty_) {
            return;
          }
          if (!output.user_dirty) {
            output.user_dirty = gsl::make_unique<PROJECT_NAMESPACE_ID::SCUserDirtyChgSync>();
          }
          auto& orbit_mgr = user_inst.get_user_orbit_manager();
          orbit_mgr.fetch_user_data(*output.user_dirty->mutable_dirty_orbit_rooms()->mutable_data());
        };
        handle.clear_fn = [](user& user_inst) { user_inst.get_user_orbit_manager().dirty_ = false; };
        return handle;
      });
}