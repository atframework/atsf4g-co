// Copyright 2026 atframework

#include <cstdint>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <logic/matching/matching_room.h>

#include "frame/test_macros.h"

namespace {
PROJECT_NAMESPACE_ID::DMatchingUnit make_unit(uint64_t unit_id, uint64_t user_id, uint32_t zone_id) {
  PROJECT_NAMESPACE_ID::DMatchingUnit result;
  result.set_unit_id(unit_id);
  auto* user = result.add_users();
  user->mutable_user_key()->set_user_id(user_id);
  user->mutable_user_key()->set_zone_id(zone_id);
  return result;
}

PROJECT_NAMESPACE_ID::DUserIDKey make_user(uint64_t user_id, uint32_t zone_id) {
  PROJECT_NAMESPACE_ID::DUserIDKey result;
  result.set_user_id(user_id);
  result.set_zone_id(zone_id);
  return result;
}

matching_room make_room() {
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_matching_pool_id(1);
  return matching_room{"matching-test", scope, 100, 200};
}
}  // namespace

CASE_TEST(matchsvr_matching_room, rejects_duplicate_unit_or_user) {
  auto room = make_room();
  const auto unit = make_unit(1, 10001, 1);
  CASE_EXPECT_TRUE(room.add_unit(unit));
  CASE_EXPECT_FALSE(room.add_unit(unit));
  CASE_EXPECT_FALSE(room.add_unit(make_unit(2, 10001, 1)));
  CASE_EXPECT_EQ(1, room.get_user_count());
}

CASE_TEST(matchsvr_matching_room, rejects_invalid_units_and_duplicate_users_inside_unit) {
  auto room = make_room();
  CASE_EXPECT_FALSE(room.add_unit(make_unit(0, 10001, 1)));

  PROJECT_NAMESPACE_ID::DMatchingUnit duplicate_users = make_unit(1, 10001, 1);
  duplicate_users.add_users()->mutable_user_key()->CopyFrom(make_user(10001, 1));
  CASE_EXPECT_FALSE(room.add_unit(duplicate_users));
  CASE_EXPECT_EQ(0, room.get_user_count());
}

CASE_TEST(matchsvr_matching_room, removes_units_only_while_room_is_active) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.remove_unit(2));
  CASE_EXPECT_TRUE(room.remove_unit(1));
  CASE_EXPECT_FALSE(room.has_unit(1));

  CASE_EXPECT_TRUE(room.add_unit(make_unit(2, 10002, 1)));
  room.mark_timeout(220);
  CASE_EXPECT_FALSE(room.remove_unit(2));
  CASE_EXPECT_FALSE(room.add_unit(make_unit(3, 10003, 1)));
}

CASE_TEST(matchsvr_matching_room, confirms_all_users_and_resumes_matching) {
  auto room = make_room();
  const auto first = make_unit(1, 10001, 1);
  const auto second = make_unit(2, 10002, 1);
  CASE_EXPECT_TRUE(room.add_unit(first));
  CASE_EXPECT_TRUE(room.add_unit(second));

  room.begin_confirmation(150);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, room.get_status());
  CASE_EXPECT_FALSE(room.are_all_users_confirmed());
  CASE_EXPECT_TRUE(room.confirm_user(first.users(0).user_key(), true));
  CASE_EXPECT_TRUE(room.confirm_user(second.users(0).user_key(), true));
  CASE_EXPECT_TRUE(room.are_all_users_confirmed());

  room.resume_matching(300);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, room.get_status());
  CASE_EXPECT_EQ(300, room.get_expire_time());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING,
                 room.get_units().at(1).users(0).confirm_status());
}

CASE_TEST(matchsvr_matching_room, requires_confirmation_from_every_user_in_one_unit) {
  auto room = make_room();
  auto unit = make_unit(1, 10001, 1);
  unit.add_users()->mutable_user_key()->CopyFrom(make_user(10002, 1));
  CASE_EXPECT_TRUE(room.add_unit(unit));

  room.begin_confirmation(150);
  CASE_EXPECT_TRUE(room.confirm_user(unit.users(0).user_key(), true));
  CASE_EXPECT_FALSE(room.are_all_users_confirmed());
  CASE_EXPECT_TRUE(room.confirm_user(unit.users(1).user_key(), true));
  CASE_EXPECT_TRUE(room.are_all_users_confirmed());
}

CASE_TEST(matchsvr_matching_room, rejects_confirmation_outside_confirming_or_for_unknown_user) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.confirm_user(make_user(10001, 1), true));

  room.begin_confirmation(150);
  CASE_EXPECT_FALSE(room.confirm_user(make_user(99999, 1), true));
  CASE_EXPECT_TRUE(room.confirm_user(make_user(10001, 1), false));
  CASE_EXPECT_FALSE(room.are_all_users_confirmed());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_REFUSED,
                 room.get_units().at(1).users(0).confirm_status());
}

CASE_TEST(matchsvr_matching_room, extends_expiry_monotonically_and_resets_confirmation_state) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  room.extend_expire_time(150);
  CASE_EXPECT_EQ(200, room.get_expire_time());
  room.extend_expire_time(250);
  CASE_EXPECT_EQ(250, room.get_expire_time());

  room.set_result_template_id(88);
  room.begin_confirmation(300);
  room.resume_matching(400);
  CASE_EXPECT_EQ(0, room.get_confirm_expire_time());
  CASE_EXPECT_EQ(0, room.get_result_template_id());
  CASE_EXPECT_EQ(400, room.get_expire_time());
}

CASE_TEST(matchsvr_matching_room, exports_terminal_snapshot) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  room.mark_creating_battle(0);
  room.mark_finished(220);

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED, snapshot.status());
  CASE_EXPECT_EQ(220, room.get_terminal_time());
  CASE_EXPECT_EQ(1, snapshot.units_size());
}

CASE_TEST(matchsvr_matching_room, accepts_orbit_ready_only_once_for_selected_server) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.begin_orbit_ready());

  room.mark_creating_battle(0x1234);
  CASE_EXPECT_EQ(0x1234, room.get_orbit_server_id());
  CASE_EXPECT_TRUE(room.begin_orbit_ready());
  CASE_EXPECT_FALSE(room.begin_orbit_ready());

  room.mark_finished(220);
  CASE_EXPECT_FALSE(room.begin_orbit_ready());
}

CASE_TEST(matchsvr_matching_room, marks_timeout_units_cancelled) {
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
  room.mark_timeout(220);

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, snapshot.status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED, snapshot.units(0).status());
  CASE_EXPECT_EQ(220, room.get_terminal_time());
}

CASE_TEST(matchsvr_matching_room, exports_failed_and_cancelled_results) {
  {
    auto room = make_room();
    CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 1)));
    room.mark_failed(-123, 230);

    PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
    room.dump(snapshot);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, snapshot.status());
    CASE_EXPECT_EQ(-123, snapshot.result());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED, snapshot.units(0).status());
  }

  {
    auto room = make_room();
    CASE_EXPECT_TRUE(room.add_unit(make_unit(2, 10002, 1)));
    room.mark_cancelled(240);

    PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
    room.dump(snapshot);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED, snapshot.status());
    CASE_EXPECT_EQ(240, room.get_terminal_time());
  }
}
