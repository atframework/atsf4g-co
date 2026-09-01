// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "logic/matching/matching_wal_handle.h"

class matching_room;

namespace rpc {
class context;
}

// 一轮玩家匹配的稳定运行时对象。Room 只负责撮合，订阅、事件序列和玩家视图均归 Unit 所有。
class matching_unit : public std::enable_shared_from_this<matching_unit> {
 public:
  using ptr_t = std::shared_ptr<matching_unit>;

  struct subscriber_route {
    uint64_t server_id = 0;
    int64_t acknowledge_event_id = 0;
  };

  explicit matching_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& data);

  uint64_t get_unit_id() const noexcept { return data_.unit_id(); }
  const PROJECT_NAMESPACE_ID::DMatchingUnit& get_data() const noexcept { return data_; }
  const PROJECT_NAMESPACE_ID::DMatchingUnitView& get_view() const noexcept { return view_; }
  int64_t get_last_event_id() const noexcept { return last_event_id_; }
  int64_t get_terminal_time() const noexcept { return terminal_time_; }
  bool is_terminal() const noexcept;

  // Room 可替换，但 Unit 对象、订阅者和事件游标在整个匹配生命周期内不变。
  void bind_room(const std::shared_ptr<matching_room>& room) noexcept { room_ = room; }
  std::shared_ptr<matching_room> get_room() const noexcept { return room_.lock(); }

  // 从当前 Room 复制业务视图；迁房前后视图相同则不会产生外部事件。
  bool refresh_view_from_room(const matching_room& room);
  void mark_terminal(PROJECT_NAMESPACE_ID::EnMatchingUnitLifecycleStatus status, int32_t result, int64_t now);
  void set_terminal_time(int64_t value) noexcept { terminal_time_ = value; }

  // 创建时的路由必须且只覆盖 Unit 全部成员。
  bool initialize_subscribers(
      rpc::context& ctx,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute>& routes);
  bool validate_subscriber_routes(
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute>& routes) const;
  bool subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t server_id,
                 int64_t acknowledge_event_id);
  bool acknowledge(rpc::context& ctx, uint64_t server_id,
                   const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingUserEventAck>& user_acks);
  std::optional<subscriber_route> get_subscriber_route(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  // 只发布 Unit 语义事件。事件中始终携带发布时的完整 Unit 视图。
  void publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::EnMatchingUnitEventType event_type);
  bool is_retry_due(std::chrono::system_clock::time_point now) const noexcept;
  void retry_pending(rpc::context& ctx);

 private:
  friend class matching_room;

  bool has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept;

  PROJECT_NAMESPACE_ID::DMatchingUnit data_;
  PROJECT_NAMESPACE_ID::DMatchingUnitView view_;
  std::weak_ptr<matching_room> room_;
  int64_t last_event_id_ = 0;
  int64_t terminal_time_ = 0;
  std::chrono::system_clock::time_point next_retry_timepoint_{};
  matching_wal_log_operator::strong_ptr<matching_wal_publisher> wal_publisher_;
};
