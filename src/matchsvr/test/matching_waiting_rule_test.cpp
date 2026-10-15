// Copyright 2026 atframework

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_resource.h>
#include <atframework/testing/runtime.h>
#include <config/excel/config_easy_api.h>
#include <config/excel_config_matching_index.h>
#include <logic/matching/matching_logic.h>
#include <logic/matching/matching_room.h>
#include <logic/matching/matching_unit.h>
#include <utility/protobuf_mini_dumper.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "frame/test_macros.h"

namespace {
template <class TMessage>
static std::string waiting_table_bytes(const TMessage& item, const std::string& version) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code(version);
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

class waiting_rule_fixture {
 public:
  waiting_rule_fixture() {
    pool.set_id(1);
    pool.set_unit_max_size(2);
    pool.set_faction_user_max_size(2);
    pool.set_max_user_cout_limit(6);
    pool.set_max_faction_cout_limit(3);
    pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
    pool.add_rule_group_ids(10);
    pool.set_is_has_waiting_room(true);
    pool.set_waiting_room_rule_id(9100);
    pool.set_is_waiting_room_faction_can_joined(true);
    rule.set_id(9100);
    rule.add_region_limits("cn");
    auto* rank = rule.add_rules();
    rank->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
    rank->add_values(12);
    auto* level = rule.add_rules();
    level->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_ROLE_LEVEL_DIFF);
    level->add_values(2);
  }

  ~waiting_rule_fixture() {
    if (runtime.is_running()) {
      CASE_EXPECT_EQ(0, runtime.stop());
    }
  }

  bool start() {
    static uint64_t sequence = 0;
    const std::string version = "waiting-core-" + std::to_string(++sequence);
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::resource};
    options.setup_callback = [this, version](atframework::testing::runtime& test) {
      test.resource().set_version(version);
      test.resource().set_file("matching_pool.bytes", waiting_table_bytes(pool, version));
      test.resource().set_file("matching_waiting_room_rule.bytes", waiting_table_bytes(rule, version));
      PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup group;
      group.set_group_id(10);
      group.add_pool_rules(100);
      test.resource().set_file("matching_rule_group.bytes", waiting_table_bytes(group, version));
      PROJECT_NAMESPACE_ID::config::ExcelMatchingRule ordinary;
      ordinary.set_id(100);
      ordinary.set_min_user_cout_limit(minimum_initial_users);
      ordinary.set_min_faction_count_limit(minimum_initial_factions);
      ordinary.add_region_limits("cn");
      auto* rank = ordinary.add_rules();
      rank->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
      rank->add_values(1);
      test.resource().set_file("matching_rule.bytes", waiting_table_bytes(ordinary, version));
      return 0;
    };
    const int32_t result = runtime.start(options);
    CASE_EXPECT_EQ(0, result);
    CASE_EXPECT_TRUE(runtime.is_running());
    if (result != 0 || !runtime.is_running()) {
      return false;
    }
    const auto loaded = excel::get_ExcelMatchingPool_by_id(1);
    CASE_EXPECT_TRUE(!!loaded);
    if (!loaded) {
      return false;
    }
    CASE_EXPECT_TRUE(loaded->is_has_waiting_room());
    CASE_EXPECT_EQ(pool.waiting_room_rule_id(), loaded->waiting_room_rule_id());
    CASE_EXPECT_EQ(pool.max_user_cout_limit(), loaded->max_user_cout_limit());
    const auto loaded_rule = excel::get_ExcelMatchingWaitingRoomRule_by_id(rule.id());
    CASE_EXPECT_TRUE(!!loaded_rule);
    if (!loaded_rule) {
      return false;
    }
    CASE_EXPECT_EQ(rule.SerializeAsString(), loaded_rule->SerializeAsString());
    return loaded->is_has_waiting_room() && loaded->waiting_room_rule_id() == pool.waiting_room_rule_id() &&
           loaded_rule->SerializeAsString() == rule.SerializeAsString();
  }

  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool pool;
  PROJECT_NAMESPACE_ID::config::ExcelMatchingWaitingRoomRule rule;
  int32_t minimum_initial_users = 2;
  int32_t minimum_initial_factions = 1;
  atframework::testing::runtime runtime;
};

static PROJECT_NAMESPACE_ID::DMatchingUnit waiting_unit(uint64_t id, uint64_t user_id, int32_t size = 1) {
  PROJECT_NAMESPACE_ID::DMatchingUnit unit;
  unit.set_unit_id(id);
  unit.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE);
  unit.add_acceptable_level_ids(101);
  unit.mutable_parameter()->set_rank_level(10);
  unit.mutable_parameter()->set_role_level(20);
  for (int32_t index = 0; index < size; ++index) {
    auto* user = unit.add_users();
    user->mutable_user_key()->set_user_id(user_id + static_cast<uint64_t>(index));
    user->mutable_user_key()->set_zone_id(1);
  }
  return unit;
}

static matching_room::ptr_t ordinary_room(const PROJECT_NAMESPACE_ID::DMatchingUnit& initial) {
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_matching_pool_id(1);
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  auto room = std::make_shared<matching_room>("waiting-rule-room", scope, 101, 80, 300);
  const auto evaluation = matching_logic::check_unit_can_create_room(scope, initial, 80, initial.users_size());
  CASE_EXPECT_TRUE(evaluation.evaluation.can_join());
  if (!evaluation.evaluation.can_join()) {
    return nullptr;
  }
  const bool added = room->add_unit(std::make_shared<matching_unit>(initial));
  CASE_EXPECT_TRUE(added);
  const bool assigned = added && room->set_faction_assignments(evaluation.evaluation.faction_assignments());
  CASE_EXPECT_TRUE(assigned);
  return assigned ? room : nullptr;
}

static bool open_waiting(matching_room& room, int64_t end = 200) {
  const auto ready = matching_logic::check_room_ready(room, 100, static_cast<int32_t>(room.get_user_count()));
  CASE_EXPECT_TRUE(ready.ready());
  if (!ready.ready()) {
    return false;
  }
  room.begin_confirmation(110);
  for (const auto& value : room.get_units()) {
    for (const auto& user : value.second->get_data().users()) {
      CASE_EXPECT_TRUE(room.confirm_user(user.user_key(), true));
    }
  }
  CASE_EXPECT_TRUE(room.are_all_users_confirmed());
  const bool finalized = room.finalize_faction_ids();
  CASE_EXPECT_TRUE(finalized);
  if (!finalized) {
    return false;
  }
  room.mark_creating_battle(0x180081, 150);
  const bool opened = room.open_waiting_room(100, end);
  CASE_EXPECT_TRUE(opened);
  return opened;
}

static bool add_waiting(matching_room& room, const std::vector<matching_unit::ptr_t>& units,
                        uint32_t complete_capacity = 0, int64_t now = 101) {
  matching_logic::unit_view data;
  for (const auto& unit : units) {
    data.emplace_back(&unit->get_data());
  }
  const auto evaluation = matching_logic::check_waiting_units_can_join(room, data, complete_capacity, now);
  CASE_EXPECT_TRUE(evaluation.can_join);
  if (!evaluation.can_join) {
    return false;
  }
  const bool added = room.add_waiting_units(units, evaluation.faction_id, evaluation.faction_capacity,
                                            evaluation.creates_faction, now, now + 15);
  CASE_EXPECT_TRUE(added);
  return added;
}

static void accept_unit(const matching_unit::ptr_t& unit) {
  for (const auto& user : unit->get_data().users()) {
    CASE_EXPECT_TRUE(unit->confirm_user(user.user_key(), true));
  }
}
}  // namespace

CASE_TEST(matchsvr_waiting_rule, incomplete_factions_wait_for_full_membership_by_default) {
  waiting_rule_fixture fixture;
  fixture.minimum_initial_users = 1;
  if (!fixture.start()) {
    return;
  }
  CASE_EXPECT_FALSE(excel::get_ExcelMatchingPool_by_id(1)->allow_incomplete_factions());
  auto room = ordinary_room(waiting_unit(1, 1001));
  if (!room) {
    return;
  }
  const auto ready = matching_logic::check_room_ready(*room, 100, 1);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());
  CASE_EXPECT_EQ(2, room->get_faction_assignments().Get(0).user_capacity());
  CASE_EXPECT_EQ(1, room->get_faction_assignments().Get(0).assigned_user_count());
}

CASE_TEST(matchsvr_waiting_rule, incomplete_initial_faction_preserves_capacity_for_waiting_fill) {
  waiting_rule_fixture fixture;
  fixture.pool.set_allow_incomplete_factions(true);
  fixture.minimum_initial_users = 1;
  if (!fixture.start()) {
    return;
  }
  CASE_EXPECT_TRUE(excel::get_ExcelMatchingPool_by_id(1)->allow_incomplete_factions());
  auto room = ordinary_room(waiting_unit(1, 1001));
  if (!room || !open_waiting(*room)) {
    return;
  }
  const int32_t initial_faction_id = room->get_unit_faction_id(1);
  CASE_EXPECT_GT(initial_faction_id, 0);
  CASE_EXPECT_EQ(1, room->get_waiting_factions().size());
  CASE_EXPECT_EQ(2, room->get_waiting_factions().at(initial_faction_id).user_capacity);
  CASE_EXPECT_EQ(1, room->get_successful_user_count());
  auto candidate = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  if (!add_waiting(*room, {candidate})) {
    return;
  }
  CASE_EXPECT_EQ(initial_faction_id, room->get_unit_faction_id(2));
  accept_unit(candidate);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(2) == std::vector<uint64_t>{2}));
  candidate->mark_initializing();
  CASE_EXPECT_TRUE(room->complete_waiting_unit(2, 102));
  CASE_EXPECT_EQ(2, room->get_successful_user_count());
  CASE_EXPECT_EQ(0, room->get_reserved_user_count());
  CASE_EXPECT_EQ(1, room->get_waiting_factions().size());
  CASE_EXPECT_EQ(2, room->get_waiting_factions().at(initial_faction_id).user_capacity);
  CASE_EXPECT_EQ(2, room->get_waiting_factions().at(initial_faction_id).successful_user_count);
  CASE_EXPECT_EQ(initial_faction_id, candidate->get_view().faction_id());
}

CASE_TEST(matchsvr_waiting_rule, incomplete_faction_option_keeps_minimum_user_threshold) {
  waiting_rule_fixture fixture;
  fixture.pool.set_allow_incomplete_factions(true);
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001));
  if (!room) {
    return;
  }
  const auto ready = matching_logic::check_room_ready(*room, 100, 1);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());
}

CASE_TEST(matchsvr_waiting_rule, incomplete_faction_option_keeps_minimum_faction_threshold) {
  waiting_rule_fixture fixture;
  fixture.pool.set_allow_incomplete_factions(true);
  fixture.minimum_initial_users = 1;
  fixture.minimum_initial_factions = 2;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room) {
    return;
  }
  const auto ready = matching_logic::check_room_ready(*room, 100, 2);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());
  const auto candidate = waiting_unit(2, 2001);
  const auto joined = matching_logic::check_unit_can_join(*room, candidate, 100, 3);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (!joined.evaluation.can_join()) {
    return;
  }
  CASE_EXPECT_TRUE(room->add_unit(std::make_shared<matching_unit>(candidate)));
  CASE_EXPECT_TRUE(room->set_faction_assignments(joined.evaluation.faction_assignments()));
  CASE_EXPECT_EQ(2, room->get_faction_assignments().size());
  CASE_EXPECT_TRUE(matching_logic::check_room_ready(*room, 100, 3).ready());
}

CASE_TEST(matchsvr_waiting_rule, balanced_policy_creates_factions_then_balances_successful_and_reserved_users) {
  waiting_rule_fixture fixture;
  fixture.pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_BALANCED);
  fixture.pool.set_faction_user_max_size(4);
  fixture.pool.set_max_user_cout_limit(12);
  fixture.pool.set_allow_incomplete_factions(true);
  fixture.minimum_initial_users = 1;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001));
  if (!room || !open_waiting(*room)) {
    return;
  }
  const int32_t initial_faction = room->get_unit_faction_id(1);
  auto second = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  if (!add_waiting(*room, {second})) {
    return;
  }
  const int32_t second_faction = room->get_unit_faction_id(2);
  CASE_EXPECT_GT(second_faction, initial_faction);
  if (second_faction <= initial_faction) {
    return;
  }
  accept_unit(second);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(2) == std::vector<uint64_t>{2}));
  second->mark_initializing();
  CASE_EXPECT_TRUE(room->complete_waiting_unit(2, 102));
  auto third = std::make_shared<matching_unit>(waiting_unit(3, 3001));
  if (!add_waiting(*room, {third})) {
    return;
  }
  const int32_t third_faction = room->get_unit_faction_id(3);
  CASE_EXPECT_GT(third_faction, second_faction);
  if (third_faction <= second_faction) {
    return;
  }
  accept_unit(third);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(3) == std::vector<uint64_t>{3}));
  third->mark_initializing();
  CASE_EXPECT_TRUE(room->complete_waiting_unit(3, 102));
  auto pair = std::make_shared<matching_unit>(waiting_unit(4, 4001, 2));
  if (!add_waiting(*room, {pair})) {
    return;
  }
  // 同人数时按固定 ID 决定落点；后续选择必须计入这两人的预留。
  CASE_EXPECT_EQ(initial_faction, room->get_unit_faction_id(4));
  auto fifth = std::make_shared<matching_unit>(waiting_unit(5, 5001));
  if (!add_waiting(*room, {fifth})) {
    return;
  }
  CASE_EXPECT_EQ(second_faction, room->get_unit_faction_id(5));
  auto sixth = std::make_shared<matching_unit>(waiting_unit(6, 6001));
  if (!add_waiting(*room, {sixth})) {
    return;
  }
  CASE_EXPECT_EQ(third_faction, room->get_unit_faction_id(6));
  CASE_EXPECT_EQ(3, room->get_waiting_factions().size());
  CASE_EXPECT_EQ(3, room->get_successful_user_count());
  CASE_EXPECT_EQ(4, room->get_reserved_user_count());
  CASE_EXPECT_EQ(initial_faction, room->get_unit_faction_id(1));
  CASE_EXPECT_EQ(second_faction, room->get_unit_faction_id(2));
  CASE_EXPECT_EQ(third_faction, room->get_unit_faction_id(3));
  for (const auto& entry : room->get_waiting_factions()) {
    CASE_EXPECT_EQ(4, entry.second.user_capacity);
  }
}

CASE_TEST(matchsvr_waiting_rule, priority_policy_fills_existing_faction_before_creating_another) {
  waiting_rule_fixture fixture;
  fixture.pool.set_faction_user_max_size(4);
  fixture.pool.set_max_user_cout_limit(12);
  fixture.pool.set_allow_incomplete_factions(true);
  fixture.minimum_initial_users = 1;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room) {
    return;
  }
  // 取消或配置切换可以留下多个未满 faction；保留成员，只比较本次加入的落点。
  CASE_EXPECT_TRUE(room->add_unit(std::make_shared<matching_unit>(waiting_unit(2, 2001))));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* first_assignment = assignments.Add();
  first_assignment->set_user_capacity(4);
  first_assignment->set_assigned_user_count(2);
  first_assignment->add_unit_ids(1);
  auto* second_assignment = assignments.Add();
  second_assignment->set_user_capacity(4);
  second_assignment->set_assigned_user_count(1);
  second_assignment->add_unit_ids(2);
  CASE_EXPECT_TRUE(room->set_faction_assignments(assignments));
  if (!open_waiting(*room)) {
    return;
  }
  const int32_t first_faction = room->get_unit_faction_id(1);
  const int32_t second_faction = room->get_unit_faction_id(2);
  const auto candidate = waiting_unit(3, 3001, 2);
  const auto evaluated = matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101);
  CASE_EXPECT_TRUE(evaluated.can_join);
  CASE_EXPECT_FALSE(evaluated.creates_faction);
  CASE_EXPECT_EQ(first_faction, evaluated.faction_id);
  CASE_EXPECT_EQ(4, evaluated.faction_capacity);
  CASE_EXPECT_EQ(2, room->get_waiting_factions().size());
  CASE_EXPECT_EQ(second_faction, room->get_unit_faction_id(2));
  CASE_EXPECT_EQ(0, room->get_reserved_user_count());
}

CASE_TEST(matchsvr_waiting_rule, balanced_policy_does_not_override_disabled_existing_faction_fill) {
  waiting_rule_fixture fixture;
  fixture.pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_BALANCED);
  fixture.pool.set_max_faction_cout_limit(2);
  fixture.pool.set_allow_incomplete_factions(true);
  fixture.pool.set_is_waiting_room_faction_can_joined(false);
  fixture.minimum_initial_users = 1;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001));
  if (!room || !open_waiting(*room)) {
    return;
  }
  auto second = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  if (!add_waiting(*room, {second})) {
    return;
  }
  CASE_EXPECT_NE(room->get_unit_faction_id(1), room->get_unit_faction_id(2));
  const auto third = waiting_unit(3, 3001);
  const auto evaluated = matching_logic::check_waiting_units_can_join(*room, {&third}, 0, 101);
  CASE_EXPECT_FALSE(evaluated.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL, evaluated.result);
  CASE_EXPECT_EQ(2, room->get_user_count());
}

CASE_TEST(matchsvr_waiting_rule, absolute_room_deadline_rejects_new_and_pending_units) {
  waiting_rule_fixture fixture;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room || !open_waiting(*room)) {
    return;
  }
  auto candidate = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  CASE_EXPECT_TRUE(matching_logic::check_waiting_units_can_join(*room, {&candidate->get_data()}, 0, 100).can_join);
  CASE_EXPECT_TRUE(matching_logic::check_waiting_units_can_join(*room, {&candidate->get_data()}, 0, 199).can_join);
  if (!add_waiting(*room, {candidate}, 0, 199)) {
    return;
  }
  CASE_EXPECT_EQ(200, candidate->get_view().confirm_expire_time());
  accept_unit(candidate);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(2) == std::vector<uint64_t>{2}));
  candidate->mark_initializing();
  CASE_EXPECT_FALSE(room->complete_waiting_unit(2, 200));
  CASE_EXPECT_EQ(2, room->get_successful_user_count());
  CASE_EXPECT_EQ(1, room->get_reserved_user_count());
  const auto later = waiting_unit(3, 3001);
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&later}, 0, 200).can_join);
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&later}, 0, 201).can_join);
}

CASE_TEST(matchsvr_waiting_rule, missing_reference_does_not_admit_units) {
  waiting_rule_fixture fixture;
  fixture.pool.set_waiting_room_rule_id(9199);
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room || !open_waiting(*room)) {
    return;
  }
  CASE_EXPECT_FALSE(!!excel::get_matching_waiting_room_rule(1));
  const auto candidate = waiting_unit(2, 2001);
  const auto evaluated = matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 105);
  CASE_EXPECT_FALSE(evaluated.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND, evaluated.result);
  CASE_EXPECT_EQ(0, room->get_reserved_user_count());
}

CASE_TEST(matchsvr_waiting_rule, uses_independent_parameter_rules_and_fixed_level_with_bidirectional_bans) {
  waiting_rule_fixture fixture;
  if (!fixture.start()) {
    return;
  }
  auto initial = waiting_unit(1, 1001, 2);
  initial.add_ban_users()->set_user_id(9001);
  initial.mutable_ban_users(0)->set_zone_id(1);
  auto room = ordinary_room(initial);
  if (!room) {
    return;
  }
  auto candidate = waiting_unit(2, 2001);
  candidate.mutable_parameter()->set_rank_level(22);
  candidate.mutable_parameter()->set_role_level(22);
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(*room, candidate, 100, 2).evaluation.can_join());
  if (!open_waiting(*room)) {
    return;
  }
  CASE_EXPECT_TRUE(matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101).can_join);
  candidate.mutable_parameter()->set_rank_level(23);
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101).can_join);
  candidate.mutable_parameter()->set_rank_level(22);
  candidate.mutable_parameter()->set_role_level(23);
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101).can_join);
  candidate.mutable_parameter()->set_role_level(22);
  candidate.set_acceptable_level_ids(0, 102);
  const auto wrong_level = matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101);
  CASE_EXPECT_FALSE(wrong_level.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_SAME_LEVEL, wrong_level.result);
  candidate.set_acceptable_level_ids(0, 101);
  protobuf_copy_message(*candidate.add_ban_users(), initial.users(0).user_key());
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101).can_join);
  candidate.clear_ban_users();
  candidate.mutable_users(0)->mutable_user_key()->set_user_id(9001);
  CASE_EXPECT_FALSE(matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 101).can_join);
  CASE_EXPECT_EQ(101, room->get_selected_level_id());
}

CASE_TEST(matchsvr_waiting_rule, pool_fill_permission_does_not_override_exclusive_unit) {
  waiting_rule_fixture fixture;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room || !open_waiting(*room)) {
    return;
  }
  auto partial = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  if (!add_waiting(*room, {partial})) {
    return;
  }
  accept_unit(partial);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(2) == std::vector<uint64_t>{2}));
  partial->mark_initializing();
  CASE_EXPECT_TRUE(room->complete_waiting_unit(2, 102));
  const int32_t partial_faction_id = room->get_unit_faction_id(2);
  auto candidate = waiting_unit(3, 3001);
  const auto filling = matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 103);
  CASE_EXPECT_TRUE(filling.can_join);
  CASE_EXPECT_FALSE(filling.creates_faction);
  CASE_EXPECT_EQ(partial_faction_id, filling.faction_id);
  candidate.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  const auto exclusive = matching_logic::check_waiting_units_can_join(*room, {&candidate}, 0, 103);
  CASE_EXPECT_TRUE(exclusive.can_join);
  CASE_EXPECT_TRUE(exclusive.creates_faction);
  CASE_EXPECT_EQ(1, exclusive.faction_capacity);
  CASE_EXPECT_EQ(2, room->get_waiting_factions().at(partial_faction_id).user_capacity);
}

CASE_TEST(matchsvr_waiting_rule, completed_snapshot_releases_runtime_and_allows_same_room_reentry) {
  waiting_rule_fixture fixture;
  if (!fixture.start()) {
    return;
  }
  auto initial = waiting_unit(1, 1001, 2);
  auto room = ordinary_room(initial);
  if (!room) {
    return;
  }
  std::weak_ptr<matching_unit> retained = room->find_unit(1);
  if (!open_waiting(*room)) {
    return;
  }
  CASE_EXPECT_TRUE(retained.expired());
  CASE_EXPECT_EQ(0, room->get_units().size());
  CASE_EXPECT_EQ(1, room->get_admitted_units().size());
  CASE_EXPECT_EQ(2, room->get_successful_user_count());
  const auto admitted_before = room->get_admitted_units().at(1).SerializeAsString();
  const int32_t initial_faction_id = room->get_unit_faction_id(1);
  initial.set_unit_id(2);
  initial.mutable_parameter()->set_rank_level(23);
  const auto incompatible = matching_logic::check_waiting_units_can_join(*room, {&initial}, 0, 101);
  CASE_EXPECT_FALSE(incompatible.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND, incompatible.result);
  initial.mutable_parameter()->set_rank_level(10);
  auto returning = std::make_shared<matching_unit>(initial);
  if (!add_waiting(*room, {returning})) {
    return;
  }
  CASE_EXPECT_EQ(2, room->get_successful_user_count());
  CASE_EXPECT_EQ(2, room->get_reserved_user_count());
  CASE_EXPECT_EQ(4, room->get_user_count());
  CASE_EXPECT_EQ(1, room->get_admitted_units().size());
  CASE_EXPECT_NE(initial_faction_id, room->get_unit_faction_id(2));
  initial.set_unit_id(3);
  const auto parallel = matching_logic::check_waiting_units_can_join(*room, {&initial}, 0, 101);
  CASE_EXPECT_FALSE(parallel.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT, parallel.result);
  CASE_EXPECT_EQ(2, room->get_reserved_user_count());
  accept_unit(returning);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(2) == std::vector<uint64_t>{2}));
  returning->mark_initializing();
  CASE_EXPECT_TRUE(room->complete_waiting_unit(2, 102));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED, returning->get_view().status());
  CASE_EXPECT_EQ(0, room->get_units().size());
  CASE_EXPECT_EQ(2, room->get_admitted_units().size());
  CASE_EXPECT_EQ(4, room->get_successful_user_count());
  CASE_EXPECT_EQ(0, room->get_reserved_user_count());
  CASE_EXPECT_EQ(4, room->get_user_count());
  CASE_EXPECT_EQ(admitted_before, room->get_admitted_units().at(1).SerializeAsString());
  CASE_EXPECT_EQ(returning->get_data().SerializeAsString(), room->get_admitted_units().at(2).SerializeAsString());
  CASE_EXPECT_EQ(initial_faction_id, room->get_unit_faction_id(1));
}

CASE_TEST(matchsvr_waiting_rule, reservations_and_faction_ids_survive_partial_initialization_failure) {
  waiting_rule_fixture fixture;
  if (!fixture.start()) {
    return;
  }
  auto room = ordinary_room(waiting_unit(1, 1001, 2));
  if (!room || !open_waiting(*room)) {
    return;
  }
  const int32_t initial_faction_id = room->get_unit_faction_id(1);
  auto first = std::make_shared<matching_unit>(waiting_unit(2, 2001));
  auto second = std::make_shared<matching_unit>(waiting_unit(3, 3001));
  if (!add_waiting(*room, {first, second}, 2)) {
    return;
  }
  const int32_t new_faction_id = room->get_unit_faction_id(2);
  CASE_EXPECT_NE(initial_faction_id, new_faction_id);
  CASE_EXPECT_EQ(new_faction_id, room->get_unit_faction_id(3));
  CASE_EXPECT_EQ(2, room->get_waiting_confirmation_cohort(2).size());
  accept_unit(first);
  CASE_EXPECT_TRUE(room->release_waiting_confirmation_cohort(2).empty());
  CASE_EXPECT_TRUE(room->get_waiting_factions().at(new_faction_id).confirming_creation);
  accept_unit(second);
  CASE_EXPECT_TRUE((room->release_waiting_confirmation_cohort(3) == std::vector<uint64_t>{2, 3}));
  CASE_EXPECT_FALSE(room->get_waiting_factions().at(new_faction_id).confirming_creation);
  first->mark_initializing();
  second->mark_initializing();
  auto another = std::make_shared<matching_unit>(waiting_unit(4, 4001, 2));
  if (!add_waiting(*room, {another}, 2)) {
    return;
  }
  CASE_EXPECT_EQ(2, room->get_successful_user_count());
  CASE_EXPECT_EQ(4, room->get_reserved_user_count());
  CASE_EXPECT_EQ(6, room->get_user_count());
  const auto overflow = waiting_unit(5, 5001);
  const auto rejected = matching_logic::check_waiting_units_can_join(*room, {&overflow}, 0, 102);
  CASE_EXPECT_FALSE(rejected.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL, rejected.result);
  CASE_EXPECT_TRUE(room->complete_waiting_unit(2, 103));
  CASE_EXPECT_TRUE(room->remove_waiting_unit(3));
  CASE_EXPECT_EQ(3, room->get_successful_user_count());
  CASE_EXPECT_EQ(2, room->get_reserved_user_count());
  CASE_EXPECT_EQ(5, room->get_user_count());
  CASE_EXPECT_EQ(initial_faction_id, room->get_unit_faction_id(1));
  CASE_EXPECT_EQ(new_faction_id, room->get_unit_faction_id(2));
  CASE_EXPECT_EQ(2, room->get_waiting_factions().at(new_faction_id).user_capacity);
  CASE_EXPECT_EQ(1, room->get_waiting_factions().at(new_faction_id).successful_user_count);
  const auto filling = matching_logic::check_waiting_units_can_join(*room, {&overflow}, 0, 104);
  CASE_EXPECT_TRUE(filling.can_join);
  CASE_EXPECT_FALSE(filling.creates_faction);
  CASE_EXPECT_EQ(new_faction_id, filling.faction_id);
}
