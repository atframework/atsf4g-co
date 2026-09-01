// Copyright 2026 atframework

#include "logic/matching/matching_unit.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <set>

#include "logic/matching/matching_room.h"
#include "logic/matching/matching_utility.h"

namespace {
const std::chrono::seconds kMatchingWalRetryInterval{2};

PROJECT_NAMESPACE_ID::EnMatchingUnitLifecycleStatus convert_room_status(
    PROJECT_NAMESPACE_ID::EnMatchingRoomStatus status) noexcept {
  switch (status) {
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT;
    case PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED;
    default:
      return PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID;
  }
}
}  // namespace

matching_unit::matching_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& data)
    : wal_publisher_(create_matching_wal_publisher(*this)) {
  protobuf_copy_message(data_, data);
  protobuf_copy_message(*view_.mutable_unit(), data_);
  view_.set_status(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING);
}

bool matching_unit::is_terminal() const noexcept {
  switch (view_.status()) {
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT:
    case PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED:
      return true;
    default:
      return false;
  }
}

bool matching_unit::refresh_view_from_room(const matching_room& room) {
  PROJECT_NAMESPACE_ID::DMatchingUnitView next;
  next.set_status(convert_room_status(room.get_status()));
  protobuf_copy_message(*next.mutable_unit(), data_);
  next.set_result(room.get_result());
  next.set_expire_time(room.get_expire_time());
  next.set_last_event_id(last_event_id_);
  next.set_confirm_expire_time(room.get_confirm_expire_time());
  protobuf_copy_message(*next.mutable_orbit_room_key(), room.get_orbit_room_key());
  next.set_orbit_expired_timepoint(room.get_orbit_expired_timepoint());
  next.set_selected_level_id(room.get_selected_level_id());
  if (next.status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE ||
      next.status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED ||
      next.status() == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED) {
    next.set_faction_id(room.get_unit_faction_id(get_unit_id()));
  }
  const bool changed = next.SerializeAsString() != view_.SerializeAsString();
  view_.Swap(&next);
  return changed;
}

void matching_unit::mark_terminal(PROJECT_NAMESPACE_ID::EnMatchingUnitLifecycleStatus status, int32_t result,
                                  int64_t now) {
  view_.set_status(status);
  view_.set_result(result);
  view_.set_confirm_expire_time(0);
  terminal_time_ = now;
}

bool matching_unit::has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept {
  return matching_utility::unit_has_user(data_, user_key);
}

bool matching_unit::initialize_subscribers(
    rpc::context& ctx,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute>& routes) {
  if (!validate_subscriber_routes(routes)) {
    return false;
  }
  for (const auto& route : routes) {
    if (!subscribe(ctx, route.user_key(), route.server_id(), 0)) {
      return false;
    }
  }
  return true;
}

bool matching_unit::validate_subscriber_routes(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute>& routes) const {
  if (routes.size() != data_.users_size()) {
    return false;
  }
  std::set<std::pair<uint64_t, uint32_t>> subscribed_users;
  for (const auto& route : routes) {
    if (route.server_id() == 0 || !has_user(route.user_key()) ||
        !subscribed_users.emplace(route.user_key().user_id(), route.user_key().zone_id()).second) {
      return false;
    }
  }
  return true;
}

bool matching_unit::subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t server_id,
                              int64_t acknowledge_event_id) {
  if (!wal_publisher_ || server_id == 0 || !has_user(user_key) || acknowledge_event_id < 0 ||
      acknowledge_event_id > last_event_id_) {
    return false;
  }
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  auto subscriber = wal_publisher_->find_subscriber(user_key, wal_ctx);
  if (subscriber && subscriber->get_private_data()) {
    auto& private_data = *subscriber->get_private_data();
    private_data.set_server_id(server_id);
    private_data.set_acknowledge_event_id(std::max(private_data.acknowledge_event_id(), acknowledge_event_id));
    const auto now = atfw::util::time::time_utility::now();
    wal_publisher_->receive_subscribe_request(user_key, private_data.acknowledge_event_id(), now, wal_ctx);
    if (private_data.acknowledge_event_id() < last_event_id_ &&
        next_retry_timepoint_ == std::chrono::system_clock::time_point{}) {
      next_retry_timepoint_ = now;
    }
    return result >= 0;
  }
  auto private_data = atfw::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::DMatchingSubscriberData>();
  if (!private_data) {
    return false;
  }
  private_data->set_server_id(server_id);
  private_data->set_last_send_event_id(acknowledge_event_id);
  private_data->set_acknowledge_event_id(acknowledge_event_id);
  private_data->set_valid_event_id_bound(last_event_id_ + 1);
  const auto now = atfw::util::time::time_utility::now();
  const bool result_created = static_cast<bool>(
      wal_publisher_->create_subscriber(user_key, now, acknowledge_event_id, wal_ctx, std::move(private_data)));
  if (result_created && acknowledge_event_id < last_event_id_ &&
      next_retry_timepoint_ == std::chrono::system_clock::time_point{}) {
    next_retry_timepoint_ = now;
  }
  return result_created;
}

bool matching_unit::acknowledge(
    rpc::context& ctx, uint64_t server_id,
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingUserEventAck>& user_acks) {
  if (!wal_publisher_ || server_id == 0 || user_acks.empty()) {
    return false;
  }
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  std::set<std::pair<uint64_t, uint32_t>> acknowledged_users;
  std::vector<matching_wal_publisher::subscriber_pointer> subscribers;
  subscribers.reserve(static_cast<size_t>(user_acks.size()));
  for (const auto& ack : user_acks) {
    auto subscriber = wal_publisher_->find_subscriber(ack.user_key(), wal_ctx);
    if (ack.event_id() < 0 || ack.event_id() > last_event_id_ ||
        !acknowledged_users.emplace(ack.user_key().user_id(), ack.user_key().zone_id()).second || !subscriber ||
        !subscriber->get_private_data() || subscriber->get_private_data()->server_id() != server_id) {
      return false;
    }
    subscribers.emplace_back(std::move(subscriber));
  }
  for (int index = 0; index < user_acks.size(); ++index) {
    const auto& ack = user_acks.Get(index);
    const auto& subscriber = subscribers[static_cast<size_t>(index)];
    auto& private_data = *subscriber->get_private_data();
    private_data.set_acknowledge_event_id(std::max(private_data.acknowledge_event_id(), ack.event_id()));
    const auto now = atfw::util::time::time_utility::now();
    wal_publisher_->receive_subscribe_request(ack.user_key(), private_data.acknowledge_event_id(), now, wal_ctx);
    if (private_data.acknowledge_event_id() < last_event_id_ &&
        next_retry_timepoint_ == std::chrono::system_clock::time_point{}) {
      next_retry_timepoint_ = now;
    }
  }
  return result >= 0;
}

std::optional<matching_unit::subscriber_route> matching_unit::get_subscriber_route(
    const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  if (!wal_publisher_) {
    return std::nullopt;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  auto subscriber = wal_publisher_->find_subscriber(user_key, wal_ctx);
  if (!subscriber || !subscriber->get_private_data()) {
    return std::nullopt;
  }
  return subscriber_route{subscriber->get_private_data()->server_id(),
                          subscriber->get_private_data()->acknowledge_event_id()};
}

void matching_unit::publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnMatchingUnitEventType event_type) {
  if (!wal_publisher_ || event_type == PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_INVALID) {
    return;
  }
  const int64_t next_event_id = last_event_id_ + 1;
  PROJECT_NAMESPACE_ID::DMatchingUnitEventLog event_log;
  event_log.set_event_id(next_event_id);
  event_log.set_event_type(event_type);
  protobuf_copy_message(*event_log.mutable_view(), view_);
  event_log.mutable_view()->set_last_event_id(next_event_id);
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  const auto now = atfw::util::time::time_utility::now();
  auto log = wal_publisher_->allocate_log(now, event_type, wal_ctx, std::move(event_log));
  if (wal_publisher_->emplace_back_log(std::move(log), wal_ctx) < matching_wal_result_code::kOk) {
    FCTXLOGERROR(ctx, "append matching Unit WAL failed, unit_id={}, event_id={}", get_unit_id(), next_event_id);
    return;
  }
  last_event_id_ = next_event_id;
  view_.set_last_event_id(last_event_id_);
  wal_publisher_->broadcast(wal_ctx);
  wal_publisher_->tick(now, wal_ctx);
  next_retry_timepoint_ = now + kMatchingWalRetryInterval;
}

bool matching_unit::is_retry_due(std::chrono::system_clock::time_point now) const noexcept {
  return next_retry_timepoint_ != std::chrono::system_clock::time_point{} && now >= next_retry_timepoint_;
}

void matching_unit::retry_pending(rpc::context& ctx) {
  if (!wal_publisher_) {
    return;
  }
  const auto now = atfw::util::time::time_utility::now();
  if (next_retry_timepoint_ != std::chrono::system_clock::time_point{} && now < next_retry_timepoint_) {
    return;
  }
  int32_t result = 0;
  matching_wal_context wal_ctx{ctx, result};
  std::vector<std::pair<PROJECT_NAMESPACE_ID::DUserIDKey, int64_t>> pending_subscribers;
  auto subscribers = wal_publisher_->get_subscribe_manager().all_range();
  for (auto iter = subscribers.first; iter != subscribers.second; ++iter) {
    if (!iter->second || !iter->second->get_private_data() ||
        iter->second->get_private_data()->acknowledge_event_id() >= last_event_id_) {
      continue;
    }
    pending_subscribers.emplace_back(iter->first, iter->second->get_private_data()->acknowledge_event_id());
  }
  for (const auto& subscriber : pending_subscribers) {
    wal_publisher_->receive_subscribe_request(subscriber.first, subscriber.second, now, wal_ctx);
  }
  if (pending_subscribers.empty()) {
    next_retry_timepoint_ = std::chrono::system_clock::time_point{};
    return;
  }
  wal_publisher_->tick(now, wal_ctx);
  next_retry_timepoint_ = now + kMatchingWalRetryInterval;
}
