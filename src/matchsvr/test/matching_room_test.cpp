// Copyright 2026 atframework

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <logic/matching/matching_room.h>
#include <logic/matching/matching_unit.h>
#include <utility/protobuf_mini_dumper.h>

#include <cstdint>
#include <initializer_list>

#include "frame/test_macros.h"

namespace {
PROJECT_NAMESPACE_ID::DMatchingUnit make_unit(uint64_t unit_id, uint64_t user_id, uint32_t zone_id) {
  PROJECT_NAMESPACE_ID::DMatchingUnit result;
  result.set_unit_id(unit_id);
  result.add_acceptable_level_ids(1);
  result.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE);
  auto* user = result.add_users();
  user->mutable_user_key()->set_user_id(user_id);
  user->mutable_user_key()->set_zone_id(zone_id);
  return result;
}

void set_acceptable_levels(PROJECT_NAMESPACE_ID::DMatchingUnit& unit, std::initializer_list<int32_t> level_ids) {
  unit.clear_acceptable_level_ids();
  for (int32_t level_id : level_ids) {
    unit.add_acceptable_level_ids(level_id);
  }
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
  return matching_room{"matching-test", scope, 1, 100, 200};
}

bool add_unit(matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& data) {
  return room.add_unit(std::make_shared<matching_unit>(data));
}
}  // namespace

CASE_TEST(matchsvr_matching_room, rejects_duplicate_unit_or_user) {
  auto room = make_room();
  const auto unit = make_unit(1, 10001, 1);
  CASE_EXPECT_TRUE(add_unit(room, unit));
  CASE_EXPECT_FALSE(add_unit(room, unit));
  CASE_EXPECT_FALSE(add_unit(room, make_unit(2, 10001, 1)));
  CASE_EXPECT_EQ(1, room.get_user_count());
}

CASE_TEST(matchsvr_matching_room, maintains_cached_user_and_unit_size_counts) {
  auto room = make_room();
  auto pair = make_unit(1, 10001, 1);
  protobuf_copy_message(*pair.add_users()->mutable_user_key(), make_user(10002, 1));
  CASE_EXPECT_TRUE(add_unit(room, pair));
  CASE_EXPECT_TRUE(add_unit(room, make_unit(2, 10003, 1)));
  CASE_EXPECT_EQ(3, room.get_user_count());
  CASE_EXPECT_EQ(1, room.get_unit_size_counts().at(1));
  CASE_EXPECT_EQ(1, room.get_unit_size_counts().at(2));

  CASE_EXPECT_TRUE(room.remove_unit(1));
  CASE_EXPECT_EQ(1, room.get_user_count());
  CASE_EXPECT_EQ(1, room.get_unit_size_counts().at(1));
  CASE_EXPECT_EQ(0, room.get_unit_size_counts().at(2));
}

CASE_TEST(matchsvr_matching_room, keeps_runtime_unit_identity_across_room_transfer) {
  auto source = make_room();
  auto target = make_room();
  auto runtime_unit = std::make_shared<matching_unit>(make_unit(1, 10001, 1));

  CASE_EXPECT_TRUE(source.add_unit(runtime_unit));
  CASE_EXPECT_TRUE(source.find_unit(1) == runtime_unit);
  source.begin_confirmation(150);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_PENDING,
                 runtime_unit->get_data().users(0).confirm_status());

  CASE_EXPECT_TRUE(source.remove_unit(1));
  source.resume_matching(200);
  CASE_EXPECT_TRUE(target.add_unit(runtime_unit));
  CASE_EXPECT_TRUE(target.find_unit(1) == runtime_unit);
  CASE_EXPECT_TRUE(source.find_unit(1) == nullptr);
}

CASE_TEST(matchsvr_matching_room, maintains_level_intersection_and_selected_level) {
  auto room = make_room();
  auto first = make_unit(1, 10001, 1);
  set_acceptable_levels(first, {2, 1, 2});
  CASE_EXPECT_TRUE(add_unit(room, first));
  CASE_EXPECT_EQ(2, room.get_compatible_level_ids().size());
  CASE_EXPECT_EQ(1, room.get_selected_level_id());
  CASE_EXPECT_EQ(2, room.get_units().at(1)->get_data().acceptable_level_ids_size());

  auto second = make_unit(2, 10002, 1);
  set_acceptable_levels(second, {2, 3});
  CASE_EXPECT_TRUE(add_unit(room, second));
  CASE_EXPECT_EQ(1, room.get_compatible_level_ids().size());
  CASE_EXPECT_EQ(2, room.get_compatible_level_ids().front());
  CASE_EXPECT_EQ(2, room.get_selected_level_id());

  auto disjoint = make_unit(3, 10003, 1);
  set_acceptable_levels(disjoint, {3});
  CASE_EXPECT_FALSE(add_unit(room, disjoint));
  CASE_EXPECT_EQ(2, room.get_user_count());

  CASE_EXPECT_TRUE(room.remove_unit(2));
  CASE_EXPECT_EQ(2, room.get_compatible_level_ids().size());
  CASE_EXPECT_EQ(2, room.get_selected_level_id());

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(2, snapshot.selected_level_id());
}

CASE_TEST(matchsvr_matching_room, rejects_invalid_units_and_duplicate_users_inside_unit) {
  auto room = make_room();
  CASE_EXPECT_FALSE(add_unit(room, make_unit(0, 10001, 1)));

  PROJECT_NAMESPACE_ID::DMatchingUnit duplicate_users = make_unit(1, 10001, 1);
  protobuf_copy_message(*duplicate_users.add_users()->mutable_user_key(), make_user(10001, 1));
  CASE_EXPECT_FALSE(add_unit(room, duplicate_users));
  CASE_EXPECT_EQ(0, room.get_user_count());
}

CASE_TEST(matchsvr_matching_room, removes_units_only_while_room_is_active) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.remove_unit(2));
  CASE_EXPECT_TRUE(room.remove_unit(1));
  CASE_EXPECT_FALSE(room.has_unit(1));

  CASE_EXPECT_TRUE(add_unit(room, make_unit(2, 10002, 1)));
  room.mark_timeout(220);
  CASE_EXPECT_FALSE(room.remove_unit(2));
  CASE_EXPECT_FALSE(add_unit(room, make_unit(3, 10003, 1)));
}

CASE_TEST(matchsvr_matching_room, confirms_all_users_and_resumes_matching) {
  auto room = make_room();
  const auto first = make_unit(1, 10001, 1);
  const auto second = make_unit(2, 10002, 1);
  CASE_EXPECT_TRUE(add_unit(room, first));
  CASE_EXPECT_TRUE(add_unit(room, second));

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
                 room.get_units().at(1)->get_data().users(0).confirm_status());
}

CASE_TEST(matchsvr_matching_room, requires_confirmation_from_every_user_in_one_unit) {
  auto room = make_room();
  auto unit = make_unit(1, 10001, 1);
  protobuf_copy_message(*unit.add_users()->mutable_user_key(), make_user(10002, 1));
  CASE_EXPECT_TRUE(add_unit(room, unit));

  room.begin_confirmation(150);
  CASE_EXPECT_TRUE(room.confirm_user(unit.users(0).user_key(), true));
  CASE_EXPECT_FALSE(room.are_all_users_confirmed());
  CASE_EXPECT_TRUE(room.confirm_user(unit.users(1).user_key(), true));
  CASE_EXPECT_TRUE(room.are_all_users_confirmed());
}

CASE_TEST(matchsvr_matching_room, rejects_confirmation_outside_confirming_or_for_unknown_user) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.confirm_user(make_user(10001, 1), true));

  room.begin_confirmation(150);
  CASE_EXPECT_FALSE(room.confirm_user(make_user(99999, 1), true));
  CASE_EXPECT_TRUE(room.confirm_user(make_user(10001, 1), false));
  CASE_EXPECT_FALSE(room.are_all_users_confirmed());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_CONFIRM_STATUS_REFUSED,
                 room.get_units().at(1)->get_data().users(0).confirm_status());
}

CASE_TEST(matchsvr_matching_room, extends_expiry_monotonically_and_resets_confirmation_state) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
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

CASE_TEST(matchsvr_matching_room, maintains_internal_factions_until_confirmation) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  CASE_EXPECT_TRUE(add_unit(room, make_unit(2, 10002, 1)));
  room.set_result_template_id(88);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* faction = assignments.Add();
  faction->set_user_capacity(2);
  faction->add_unit_ids(1);
  faction->add_unit_ids(2);
  CASE_EXPECT_TRUE(room.set_faction_assignments(assignments));
  CASE_EXPECT_EQ(2, room.get_faction_assignments().Get(0).assigned_user_count());
  CASE_EXPECT_EQ(1, room.get_faction_count_by_capacity().at(2));
  CASE_EXPECT_EQ(1, room.get_completed_faction_count());
  CASE_EXPECT_EQ(0, room.get_pending_faction_user_count());
  CASE_EXPECT_TRUE(room.get_fill_enabled_faction_capacities().find(2) !=
                   room.get_fill_enabled_faction_capacities().end());

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> invalid_assignments;
  auto* invalid_faction = invalid_assignments.Add();
  invalid_faction->set_user_capacity(1);
  invalid_faction->add_unit_ids(999);
  CASE_EXPECT_FALSE(room.set_faction_assignments(invalid_assignments));
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(1, room.get_completed_faction_count());
  CASE_EXPECT_EQ(0, room.get_pending_faction_user_count());

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.faction_assignments_size());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  PROJECT_NAMESPACE_ID::DMatchingUnitView player_view;
  const auto first_unit = room.find_unit(1);
  CASE_EXPECT_TRUE(first_unit != nullptr);
  if (first_unit == nullptr) {
    return;
  }
  first_unit->refresh_view_from_room(room);
  protobuf_copy_message(player_view, first_unit->get_view());
  CASE_EXPECT_EQ(1, player_view.unit().unit_id());
  CASE_EXPECT_EQ(1, player_view.unit().users_size());
  CASE_EXPECT_EQ(0, player_view.faction_id());

  room.begin_confirmation(300);
  room.dump(snapshot);
  CASE_EXPECT_EQ(1, snapshot.faction_assignments_size());
  first_unit->refresh_view_from_room(room);
  protobuf_copy_message(player_view, first_unit->get_view());
  CASE_EXPECT_EQ(0, player_view.faction_id());
  CASE_EXPECT_TRUE(room.remove_unit(1));
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().Get(0).unit_ids_size());
  CASE_EXPECT_EQ(2, room.get_faction_assignments().Get(0).unit_ids(0));
  CASE_EXPECT_EQ(1, room.get_faction_assignments().Get(0).assigned_user_count());
  CASE_EXPECT_EQ(0, room.get_completed_faction_count());
  CASE_EXPECT_EQ(1, room.get_pending_faction_user_count());

  room.resume_matching(400);
  CASE_EXPECT_EQ(0, room.get_result_template_id());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  room.dump(snapshot);
  CASE_EXPECT_EQ(0, snapshot.faction_assignments_size());
}

CASE_TEST(matchsvr_matching_room, finalizes_battle_faction_ids_from_membership_once) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(10, 10010, 1)));
  CASE_EXPECT_TRUE(add_unit(room, make_unit(11, 10011, 1)));
  CASE_EXPECT_TRUE(add_unit(room, make_unit(20, 10020, 1)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* later_faction = assignments.Add();
  later_faction->set_user_capacity(1);
  later_faction->add_unit_ids(20);
  auto* earlier_faction = assignments.Add();
  earlier_faction->set_user_capacity(2);
  earlier_faction->add_unit_ids(10);
  earlier_faction->add_unit_ids(11);
  CASE_EXPECT_TRUE(room.set_faction_assignments(assignments));

  room.begin_confirmation(300);
  PROJECT_NAMESPACE_ID::DMatchingUnitView player_view;
  const auto first_unit = room.find_unit(10);
  const auto second_unit = room.find_unit(20);
  CASE_EXPECT_TRUE(first_unit != nullptr);
  CASE_EXPECT_TRUE(second_unit != nullptr);
  if (first_unit == nullptr || second_unit == nullptr) {
    return;
  }
  first_unit->refresh_view_from_room(room);
  protobuf_copy_message(player_view, first_unit->get_view());
  CASE_EXPECT_EQ(0, player_view.faction_id());
  CASE_EXPECT_TRUE(room.finalize_faction_ids());
  CASE_EXPECT_TRUE(room.finalize_faction_ids());
  CASE_EXPECT_EQ(1001, room.get_unit_faction_id(10));
  CASE_EXPECT_EQ(1001, room.get_unit_faction_id(11));
  CASE_EXPECT_EQ(1002, room.get_unit_faction_id(20));

  room.mark_creating_battle(0x1234, 400);
  first_unit->refresh_view_from_room(room);
  protobuf_copy_message(player_view, first_unit->get_view());
  CASE_EXPECT_EQ(1001, player_view.faction_id());
  second_unit->refresh_view_from_room(room);
  protobuf_copy_message(player_view, second_unit->get_view());
  CASE_EXPECT_EQ(1002, player_view.faction_id());
}

CASE_TEST(matchsvr_matching_room, exports_terminal_snapshot) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  room.mark_creating_battle(0, 210);
  room.mark_finished(220);

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FINISHED, snapshot.status());
  CASE_EXPECT_EQ(220, room.get_terminal_time());
  CASE_EXPECT_EQ(1, snapshot.units_size());
}

CASE_TEST(matchsvr_matching_room, accepts_orbit_ready_only_once_for_selected_server) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  CASE_EXPECT_FALSE(room.begin_orbit_ready(0x1234));

  room.mark_creating_battle(0x1234, 230);
  CASE_EXPECT_EQ(0x1234, room.get_orbit_server_id());
  CASE_EXPECT_EQ(230, room.get_battle_create_expire_time());
  CASE_EXPECT_FALSE(room.begin_orbit_ready(0x5678));
  CASE_EXPECT_TRUE(room.begin_orbit_ready(0x1234));
  CASE_EXPECT_TRUE(room.is_orbit_ready_processing());
  CASE_EXPECT_FALSE(room.begin_orbit_ready(0x1234));

  room.mark_finished(220);
  CASE_EXPECT_EQ(0, room.get_battle_create_expire_time());
  CASE_EXPECT_FALSE(room.begin_orbit_ready(0x1234));
}

CASE_TEST(matchsvr_matching_room, marks_timeout_units_cancelled) {
  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* faction = assignments.Add();
  faction->set_user_capacity(1);
  faction->add_unit_ids(1);
  CASE_EXPECT_TRUE(room.set_faction_assignments(assignments));
  room.mark_timeout(220);

  PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
  room.dump(snapshot);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, snapshot.status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED, snapshot.units(0).status());
  CASE_EXPECT_EQ(220, room.get_terminal_time());
  CASE_EXPECT_EQ(0, snapshot.faction_assignments_size());
}

CASE_TEST(matchsvr_matching_room, exports_failed_and_cancelled_results) {
  {
    auto room = make_room();
    CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 1)));
    room.mark_failed(-123, 230);

    PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
    room.dump(snapshot);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, snapshot.status());
    CASE_EXPECT_EQ(-123, snapshot.result());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_STATUS_CANCELLED, snapshot.units(0).status());
  }

  {
    auto room = make_room();
    CASE_EXPECT_TRUE(add_unit(room, make_unit(2, 10002, 1)));
    room.mark_cancelled(240);

    PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot snapshot;
    room.dump(snapshot);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED, snapshot.status());
    CASE_EXPECT_EQ(240, room.get_terminal_time());
  }
}
