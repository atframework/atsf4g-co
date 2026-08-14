// Copyright 2026 atframework

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_resource.h>
#include <atframework/testing/runtime.h>

#include <logic/matching/matching_logic.h>
#include <logic/matching/matching_manager.h>
#include <logic/matching/matching_room.h>

#include <rpc/rpc_context.h>

#include <time/time_utility.h>

#include "frame/test_macros.h"

namespace {
template <class TMessage>
std::string make_table_bytes(const TMessage& item) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("matchsvr-unit-test");
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

template <class TMessage>
std::string make_table_bytes(std::initializer_list<TMessage> items) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("matchsvr-unit-test");
  for (const auto& item : items) {
    blocks.add_data_block(item.SerializeAsString());
  }
  return blocks.SerializeAsString();
}

std::string make_empty_table_bytes() {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("matchsvr-unit-test");
  return blocks.SerializeAsString();
}

void seed_matching_tables(atframework::testing::mock_resource& resource) {
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool pool;
  pool.set_id(1);
  pool.set_user_upper(4);
  pool.set_team_max_size(2);
  pool.add_rule_group_ids(10);
  pool.set_search_timeout_seconds(120);
  pool.set_confirm_timeout_seconds(15);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool convergence_pool;
  convergence_pool.set_id(2);
  convergence_pool.set_user_upper(3);
  convergence_pool.set_team_max_size(1);
  convergence_pool.add_rule_group_ids(20);
  convergence_pool.set_search_timeout_seconds(120);
  convergence_pool.set_confirm_timeout_seconds(15);
  resource.set_file("matching_pool.bytes", make_table_bytes({pool, convergence_pool}));

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup group;
  group.set_group_id(10);
  group.set_global_user_lower(0);
  group.set_global_user_upper(100);
  group.add_pool_rules(100);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup convergence_group;
  convergence_group.set_group_id(20);
  convergence_group.set_global_user_lower(0);
  convergence_group.set_global_user_upper(100);
  convergence_group.add_pool_rules(200);
  convergence_group.add_pool_rules(201);
  resource.set_file("matching_rule_group.bytes", make_table_bytes({group, convergence_group}));

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule rule;
  rule.set_id(100);
  rule.mutable_time_limit()->set_min(0);
  rule.mutable_time_limit()->set_max(60);
  rule.add_result_template_ids(1000);
  rule.add_result_template_ids(1001);
  rule.set_min_total_user(2);
  rule.set_start_battle_min_user(2);
  auto* rank_rule = rule.add_rules();
  rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  rank_rule->add_values(5);
  auto* force_limit = rule.add_force_type_limits();
  force_limit->set_force_type(1);
  force_limit->set_count(1);
  rule.add_region_limits("cn");

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule strict_rule;
  strict_rule.set_id(200);
  strict_rule.mutable_time_limit()->set_min(0);
  strict_rule.mutable_time_limit()->set_max(60);
  strict_rule.add_result_template_ids(2000);
  strict_rule.set_min_total_user(3);
  strict_rule.set_start_battle_min_user(3);
  auto* strict_rank_rule = strict_rule.add_rules();
  strict_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  strict_rank_rule->add_values(5);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule relaxed_rule;
  relaxed_rule.set_id(201);
  relaxed_rule.mutable_time_limit()->set_min(61);
  relaxed_rule.add_result_template_ids(2000);
  relaxed_rule.set_min_total_user(3);
  relaxed_rule.set_start_battle_min_user(3);
  relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  resource.set_file("matching_rule.bytes", make_table_bytes({rule, strict_rule, relaxed_rule}));

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate result_template;
  result_template.set_id(1000);
  auto* team = result_template.add_team_template();
  team->set_user_number(1);
  team->set_count(2);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate alternate_result_template;
  alternate_result_template.set_id(1001);
  auto* alternate_team = alternate_result_template.add_team_template();
  alternate_team->set_user_number(1);
  alternate_team->set_count(2);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate convergence_template;
  convergence_template.set_id(2000);
  auto* convergence_team = convergence_template.add_team_template();
  convergence_team->set_user_number(1);
  convergence_team->set_count(3);
  resource.set_file("matching_result_template.bytes",
                    make_table_bytes({result_template, alternate_result_template, convergence_template}));

  resource.set_file("const.bytes", make_empty_table_bytes());
  resource.set_file("dtmq_channel_type.bytes", make_empty_table_bytes());
  resource.set_file("item_type.bytes", make_empty_table_bytes());
  resource.set_file("level.bytes", make_empty_table_bytes());
  resource.set_file("rank_define.bytes", make_empty_table_bytes());
  resource.set_file("rank_period_reward_pool.bytes", make_empty_table_bytes());
  resource.set_file("rank_rule.bytes", make_empty_table_bytes());
  resource.set_file("UeSource_Inventory.bytes", make_empty_table_bytes());
  resource.set_version("matchsvr-unit-test-v1");
}

PROJECT_NAMESPACE_ID::DMatchingUnit make_unit(uint64_t unit_id, uint64_t user_id, int32_t rank_level,
                                              int32_t force_type = 0) {
  PROJECT_NAMESPACE_ID::DMatchingUnit result;
  result.set_unit_id(unit_id);
  result.mutable_parameter()->set_rank_level(rank_level);
  result.mutable_parameter()->set_force_type(force_type);
  auto* user = result.add_users();
  user->mutable_user_key()->set_user_id(user_id);
  user->mutable_user_key()->set_zone_id(1);
  result.mutable_captain_user_key()->CopyFrom(user->user_key());
  return result;
}

matching_room make_room() {
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(1);
  return matching_room{"logic-test", scope, 100, 300};
}

bool start_runtime(atframework::testing::runtime& runtime) {
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::resource};
  options.working_directory = MATCHSVR_TEST_WORKING_DIRECTORY;
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_matching_tables(rt.resource());
    return 0;
  };
  if (runtime.start(options) == 0 && runtime.is_running()) {
    return true;
  }
  CASE_MSG_INFO() << "runtime start failed: " << runtime.get_diagnostic() << '\n';
  return false;
}

PROJECT_NAMESPACE_ID::SSMatchingCreateReq make_create_request(uint64_t unit_id, uint64_t user_id, int32_t rank_level,
                                                              int32_t force_type = 0, int32_t matching_pool_id = 1) {
  PROJECT_NAMESPACE_ID::SSMatchingCreateReq result;
  result.mutable_scope()->set_level_type(1);
  result.mutable_scope()->set_region("cn");
  result.mutable_scope()->set_battle_version("1.0");
  result.mutable_scope()->set_matching_pool_id(matching_pool_id);
  result.mutable_unit()->CopyFrom(make_unit(unit_id, user_id, rank_level, force_type));
  result.mutable_operator_user()->CopyFrom(result.unit().captain_user_key());
  return result;
}
}  // namespace

CASE_TEST(matchsvr_matching_logic, validates_units_against_pool_and_captain_contract) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto valid = make_unit(1, 10001, 10);
  CASE_EXPECT_EQ(0, matching_logic::validate_unit(1, valid));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND, matching_logic::validate_unit(999, valid));

  auto invalid = valid;
  invalid.set_unit_id(0);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));
  invalid = valid;
  invalid.mutable_captain_user_key()->set_user_id(99999);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));
  invalid = valid;
  invalid.add_users()->CopyFrom(invalid.users(0));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));
  invalid = valid;
  invalid.add_users()->mutable_user_key()->set_user_id(10002);
  invalid.mutable_users(1)->mutable_user_key()->set_zone_id(1);
  invalid.add_users()->mutable_user_key()->set_user_id(10003);
  invalid.mutable_users(2)->mutable_user_key()->set_zone_id(1);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, applies_capacity_rank_template_and_force_limits) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 10, 1)));

  auto accepted = matching_logic::check_unit_can_join(room, make_unit(2, 10002, 14, 2), 110, 2);
  CASE_EXPECT_TRUE(accepted.can_join);
  CASE_EXPECT_EQ(1000, accepted.result_template_id);
  CASE_EXPECT_EQ(0, accepted.result);

  auto rank_rejected = matching_logic::check_unit_can_join(room, make_unit(3, 10003, 16, 2), 110, 2);
  CASE_EXPECT_FALSE(rank_rejected.can_join);
  auto force_rejected = matching_logic::check_unit_can_join(room, make_unit(4, 10004, 12, 1), 110, 2);
  CASE_EXPECT_FALSE(force_rejected.can_join);

  auto oversized = make_unit(5, 10005, 12, 2);
  for (uint64_t user_id = 10006; user_id <= 10008; ++user_id) {
    oversized.add_users()->mutable_user_key()->set_user_id(user_id);
  }
  auto room_full = matching_logic::check_unit_can_join(room, oversized, 110, 2);
  CASE_EXPECT_FALSE(room_full.can_join);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL, room_full.result);

  CASE_EXPECT_TRUE(room.add_unit(make_unit(2, 10002, 14, 2)));
  auto ready = matching_logic::check_room_ready(room, 110, 2);
  CASE_EXPECT_TRUE(ready.ready);
  CASE_EXPECT_EQ(1000, ready.result_template_id);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, reuses_valid_selected_template_before_scanning_rule_candidates) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 10, 1)));
  room.set_result_template_id(1001);

  const auto result = matching_logic::check_room_ready(room, 110, 1);
  CASE_EXPECT_EQ(0, result.result);
  CASE_EXPECT_FALSE(result.ready);
  CASE_EXPECT_EQ(1001, result.result_template_id);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate result_template;
  result_template.set_id(1000);
  auto* team = result_template.add_team_template();
  team->set_user_number(1);
  team->set_count(2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate changed_alternate_template;
  changed_alternate_template.set_id(1001);
  auto* changed_alternate_team = changed_alternate_template.add_team_template();
  changed_alternate_team->set_user_number(2);
  changed_alternate_team->set_count(1);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate convergence_template;
  convergence_template.set_id(2000);
  auto* convergence_team = convergence_template.add_team_template();
  convergence_team->set_user_number(1);
  convergence_team->set_count(3);
  runtime.resource().set_file(
      "matching_result_template.bytes",
      make_table_bytes({result_template, changed_alternate_template, convergence_template}));
  runtime.resource().set_version("matchsvr-unit-test-v2");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);

  const auto reloaded_result = matching_logic::check_room_ready(room, 110, 1);
  CASE_EXPECT_EQ(0, reloaded_result.result);
  CASE_EXPECT_FALSE(reloaded_result.ready);
  CASE_EXPECT_EQ(1000, reloaded_result.result_template_id);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rejects_banned_users_wrong_region_and_expired_rule_window) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  auto stored = make_unit(1, 10001, 10);
  stored.add_ban_users()->CopyFrom(make_unit(2, 10002, 10).users(0).user_key());
  CASE_EXPECT_TRUE(room.add_unit(stored));
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(2, 10002, 10), 110, 2).can_join);
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(3, 10003, 10), 161, 2).can_join);

  PROJECT_NAMESPACE_ID::DMatchingScope other_scope = room.get_scope();
  other_scope.set_region("us");
  matching_room other_room{"other-region", other_scope, 100, 300};
  CASE_EXPECT_TRUE(other_room.add_unit(make_unit(4, 10004, 10)));
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(other_room, make_unit(5, 10005, 10), 110, 2).can_join);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, fails_after_confirmation_when_orbitsvr_is_unavailable) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  manager->init();
  rpc::context ctx{rpc::context::create_without_task()};

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot first_response;
  auto first_request = make_create_request(1, 10001, 10, 1);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, first_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, first_response.snapshot().status());
  CASE_EXPECT_EQ(1, manager->get_room_count());
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot second_response;
  auto second_request = make_create_request(2, 10002, 14, 2);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, second_response));
  CASE_EXPECT_EQ(first_response.snapshot().matching_id(), second_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, second_response.snapshot().status());

  PROJECT_NAMESPACE_ID::SSMatchingConfirmReq confirm;
  confirm.set_matching_id(second_response.snapshot().matching_id());
  confirm.set_unit_id(1);
  confirm.mutable_operator_user()->CopyFrom(first_request.operator_user());
  confirm.set_confirmed(true);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot confirm_response;
  const int64_t last_event_id_before_confirm = second_response.snapshot().last_event_id();
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, confirm_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, confirm_response.snapshot().status());
  CASE_EXPECT_EQ(last_event_id_before_confirm, confirm_response.snapshot().last_event_id());

  confirm.set_unit_id(2);
  confirm.mutable_operator_user()->CopyFrom(second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, confirm_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, confirm_response.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, confirm_response.snapshot().result());

  manager->clear();
  CASE_EXPECT_EQ(0, manager->get_room_count());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rejects_conflicts_and_unauthorized_operations) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_create_request(10, 20001, 10);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot conflict_response;
  auto invalid_scope = make_create_request(12, 20002, 10);
  invalid_scope.mutable_scope()->clear_region();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                 manager->create_matching(ctx, invalid_scope, conflict_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT,
                 manager->create_matching(ctx, request, conflict_response));

  auto same_user = make_create_request(11, 20001, 10);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT,
                 manager->create_matching(ctx, same_user, conflict_response));

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(response.snapshot().matching_id());
  check.set_unit_id(10);
  check.mutable_operator_user()->set_user_id(99999);
  check.mutable_operator_user()->set_zone_id(1);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, manager->check_matching(ctx, check, response));

  PROJECT_NAMESPACE_ID::SSMatchingCancelReq cancel;
  cancel.set_matching_id(check.matching_id());
  cancel.set_unit_id(10);
  cancel.mutable_operator_user()->CopyFrom(check.operator_user());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, manager->cancel_matching(ctx, cancel, response));
  cancel.mutable_operator_user()->CopyFrom(request.operator_user());
  CASE_EXPECT_EQ(0, manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED, response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, prefers_oldest_compatible_room_after_rule_expands) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot oldest_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(21, 30001, 0, 0, 2), oldest_response));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot newer_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(22, 30002, 100, 0, 2), newer_response));
  CASE_EXPECT_EQ(2, manager->get_room_count());
  CASE_EXPECT_TRUE(oldest_response.snapshot().matching_id() != newer_response.snapshot().matching_id());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot candidate_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(23, 30003, 50, 0, 2), candidate_response));
  CASE_EXPECT_EQ(oldest_response.snapshot().matching_id(), candidate_response.snapshot().matching_id());
  CASE_EXPECT_EQ(2, candidate_response.snapshot().units_size());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, migrates_smaller_room_into_larger_room_on_check) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto isolated_request = make_create_request(31, 40001, 0, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot isolated_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, isolated_request, isolated_response));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto larger_request = make_create_request(32, 40002, 100, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot larger_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, larger_request, larger_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(33, 40003, 102, 0, 2), larger_response));
  const std::string target_matching_id = larger_response.snapshot().matching_id();
  CASE_EXPECT_EQ(2, larger_response.snapshot().units_size());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, larger_response.snapshot().status());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_unit_id(isolated_request.unit().unit_id());
  check.mutable_operator_user()->CopyFrom(isolated_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot migrated_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.snapshot().matching_id());
  CASE_EXPECT_EQ(3, migrated_response.snapshot().units_size());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, migrated_response.snapshot().status());
  CASE_EXPECT_EQ(2, manager->get_room_count());

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq stale_source_check = check;
  stale_source_check.set_matching_id(isolated_response.snapshot().matching_id());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, stale_source_check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.snapshot().matching_id());
  check.set_matching_id(target_matching_id);
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, migrated_response));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, isolates_rooms_by_every_scope_dimension) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto base = make_create_request(41, 50001, 10, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, base, response));

  auto different_level = make_create_request(42, 50002, 10, 0, 2);
  different_level.mutable_scope()->set_level_type(2);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, different_level, response));
  auto different_region = make_create_request(43, 50003, 10, 0, 2);
  different_region.mutable_scope()->set_region("us");
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, different_region, response));
  auto different_version = make_create_request(44, 50004, 10, 0, 2);
  different_version.mutable_scope()->set_battle_version("2.0");
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, different_version, response));
  auto different_pool = make_create_request(45, 50005, 10, 0, 1);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, different_pool, response));
  CASE_EXPECT_EQ(5, manager->get_room_count());
  CASE_EXPECT_EQ(5, manager->get_total_matching_user_count());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, times_out_and_recycles_terminal_rooms) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_create_request(51, 60001, 10, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{121});
  CASE_EXPECT_EQ(0, manager->tick());
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(response.snapshot().matching_id());
  check.set_unit_id(request.unit().unit_id());
  check.mutable_operator_user()->CopyFrom(request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_TIMEOUT, response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{181});
  CASE_EXPECT_EQ(1, manager->tick());
  CASE_EXPECT_EQ(0, manager->get_room_count());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, manager->check_matching(ctx, check, response));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, removes_unconfirmed_unit_and_resumes_after_confirm_timeout) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto accepted_request = make_create_request(61, 70001, 10, 1);
  auto timeout_request = make_create_request(62, 70002, 14, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, accepted_request, response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, timeout_request, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, response.snapshot().status());

  PROJECT_NAMESPACE_ID::SSMatchingConfirmReq confirm;
  confirm.set_matching_id(response.snapshot().matching_id());
  confirm.set_unit_id(accepted_request.unit().unit_id());
  confirm.mutable_operator_user()->CopyFrom(accepted_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  const int64_t last_event_id_before_timeout = response.snapshot().last_event_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{16});
  CASE_EXPECT_EQ(0, manager->tick());
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_unit_id(accepted_request.unit().unit_id());
  check.mutable_operator_user()->CopyFrom(accepted_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, response.snapshot().status());
  CASE_EXPECT_EQ(1, response.snapshot().units_size());
  CASE_EXPECT_EQ(last_event_id_before_timeout + 1, response.snapshot().last_event_id());
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, timeout_request, retry_response));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, releases_units_when_orbitsvr_is_unavailable) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto first_request = make_create_request(71, 80001, 10, 1);
  auto second_request = make_create_request(72, 80002, 14, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, response));

  PROJECT_NAMESPACE_ID::SSMatchingConfirmReq confirm;
  confirm.set_matching_id(response.snapshot().matching_id());
  confirm.set_unit_id(first_request.unit().unit_id());
  confirm.mutable_operator_user()->CopyFrom(first_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  confirm.set_unit_id(second_request.unit().unit_id());
  confirm.mutable_operator_user()->CopyFrom(second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, response.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, response.snapshot().result());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, retry_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, retry_response));

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}
