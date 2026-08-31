// Copyright 2026 atframework

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.level.config.pb.h>
#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

#include <logic/matching/matching_logic.h>
#include <logic/matching/matching_manager.h>
#include <logic/matching/matching_room.h>

#include <rpc/rpc_context.h>

#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "frame/test_macros.h"

namespace {
matching_logic::unit_view make_unit_view(const std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit>& units) {
  matching_logic::unit_view result;
  result.reserve(units.size());
  for (const auto& unit : units) {
    result.emplace_back(&unit);
  }
  return result;
}

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

PROJECT_NAMESPACE_ID::config::ExcelLevel make_level(int32_t level_id, int32_t level_type, int32_t matching_pool_id) {
  PROJECT_NAMESPACE_ID::config::ExcelLevel result;
  result.set_level_id(level_id);
  result.set_level_type(level_type);
  result.set_matching_pool_id(matching_pool_id);
  result.set_client_template_id(level_id);
  return result;
}

void seed_matching_tables(atframework::testing::mock_resource& resource) {
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool pool;
  pool.set_id(1);
  pool.set_user_upper(4);
  pool.set_unit_max_size(2);
  pool.add_rule_group_ids(10);
  pool.set_search_timeout_seconds(120);
  pool.set_confirm_timeout_seconds(15);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool convergence_pool;
  convergence_pool.set_id(2);
  convergence_pool.set_user_upper(3);
  convergence_pool.set_unit_max_size(1);
  convergence_pool.add_rule_group_ids(20);
  convergence_pool.set_search_timeout_seconds(120);
  convergence_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool faction_pool;
  faction_pool.set_id(3);
  faction_pool.set_user_upper(6);
  faction_pool.set_unit_max_size(3);
  faction_pool.add_rule_group_ids(30);
  faction_pool.set_search_timeout_seconds(120);
  faction_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool incompatible_unit_pool;
  incompatible_unit_pool.set_id(4);
  incompatible_unit_pool.set_user_upper(4);
  incompatible_unit_pool.set_unit_max_size(3);
  incompatible_unit_pool.add_rule_group_ids(40);
  incompatible_unit_pool.set_search_timeout_seconds(120);
  incompatible_unit_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool dynamic_ready_pool;
  dynamic_ready_pool.set_id(5);
  dynamic_ready_pool.set_user_upper(3);
  dynamic_ready_pool.set_unit_max_size(1);
  dynamic_ready_pool.add_rule_group_ids(50);
  dynamic_ready_pool.set_search_timeout_seconds(120);
  dynamic_ready_pool.set_confirm_timeout_seconds(15);
  resource.set_file("matching_pool.bytes", make_table_bytes({pool, convergence_pool, faction_pool,
                                                             incompatible_unit_pool, dynamic_ready_pool}));

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
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup faction_group;
  faction_group.set_group_id(30);
  faction_group.set_global_user_lower(0);
  faction_group.set_global_user_upper(100);
  faction_group.add_pool_rules(300);
  faction_group.add_pool_rules(301);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup incompatible_unit_group;
  incompatible_unit_group.set_group_id(40);
  incompatible_unit_group.set_global_user_lower(0);
  incompatible_unit_group.set_global_user_upper(100);
  incompatible_unit_group.add_pool_rules(400);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup dynamic_ready_group;
  dynamic_ready_group.set_group_id(50);
  dynamic_ready_group.set_global_user_lower(0);
  dynamic_ready_group.set_global_user_upper(100);
  dynamic_ready_group.add_pool_rules(500);
  dynamic_ready_group.add_pool_rules(501);
  resource.set_file("matching_rule_group.bytes", make_table_bytes({group, convergence_group, faction_group,
                                                                   incompatible_unit_group, dynamic_ready_group}));

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
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule faction_strict_rule;
  faction_strict_rule.set_id(300);
  faction_strict_rule.mutable_time_limit()->set_min(0);
  faction_strict_rule.mutable_time_limit()->set_max(60);
  faction_strict_rule.add_result_template_ids(3000);
  faction_strict_rule.add_result_template_ids(3001);
  faction_strict_rule.add_result_template_ids(3002);
  faction_strict_rule.set_min_total_user(6);
  faction_strict_rule.set_start_battle_min_user(6);
  auto* faction_rank_rule = faction_strict_rule.add_rules();
  faction_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  faction_rank_rule->add_values(5);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule faction_relaxed_rule;
  faction_relaxed_rule.set_id(301);
  faction_relaxed_rule.mutable_time_limit()->set_min(61);
  faction_relaxed_rule.add_result_template_ids(3000);
  faction_relaxed_rule.add_result_template_ids(3001);
  faction_relaxed_rule.add_result_template_ids(3002);
  faction_relaxed_rule.set_min_total_user(6);
  faction_relaxed_rule.set_start_battle_min_user(6);
  faction_relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule incompatible_unit_rule;
  incompatible_unit_rule.set_id(400);
  incompatible_unit_rule.mutable_time_limit()->set_min(0);
  incompatible_unit_rule.add_result_template_ids(4000);
  incompatible_unit_rule.set_min_total_user(4);
  incompatible_unit_rule.set_start_battle_min_user(4);
  incompatible_unit_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule dynamic_ready_strict_rule;
  dynamic_ready_strict_rule.set_id(500);
  dynamic_ready_strict_rule.mutable_time_limit()->set_min(0);
  dynamic_ready_strict_rule.mutable_time_limit()->set_max(60);
  dynamic_ready_strict_rule.add_result_template_ids(5001);
  dynamic_ready_strict_rule.set_min_total_user(3);
  dynamic_ready_strict_rule.set_start_battle_min_user(3);
  auto* dynamic_ready_rank_rule = dynamic_ready_strict_rule.add_rules();
  dynamic_ready_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  dynamic_ready_rank_rule->add_values(5);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule dynamic_ready_relaxed_rule;
  dynamic_ready_relaxed_rule.set_id(501);
  dynamic_ready_relaxed_rule.mutable_time_limit()->set_min(61);
  dynamic_ready_relaxed_rule.add_result_template_ids(5001);
  dynamic_ready_relaxed_rule.add_result_template_ids(5000);
  dynamic_ready_relaxed_rule.set_min_total_user(2);
  dynamic_ready_relaxed_rule.set_start_battle_min_user(2);
  dynamic_ready_relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  resource.set_file("matching_rule.bytes",
                    make_table_bytes({rule, strict_rule, relaxed_rule, faction_strict_rule, faction_relaxed_rule,
                                      incompatible_unit_rule, dynamic_ready_strict_rule, dynamic_ready_relaxed_rule}));

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate result_template;
  result_template.set_id(1000);
  auto* faction = result_template.add_faction_template();
  faction->set_user_number(1);
  faction->set_count(2);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate alternate_result_template;
  alternate_result_template.set_id(1001);
  auto* alternate_faction = alternate_result_template.add_faction_template();
  alternate_faction->set_user_number(1);
  alternate_faction->set_count(2);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate convergence_template;
  convergence_template.set_id(2000);
  auto* convergence_faction = convergence_template.add_faction_template();
  convergence_faction->set_user_number(1);
  convergence_faction->set_count(3);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate faction_template;
  faction_template.set_id(3000);
  auto* three_user_faction = faction_template.add_faction_template();
  three_user_faction->set_user_number(3);
  three_user_faction->set_count(2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate mixed_faction_template;
  mixed_faction_template.set_id(3001);
  auto* two_user_mixed_faction = mixed_faction_template.add_faction_template();
  two_user_mixed_faction->set_user_number(2);
  two_user_mixed_faction->set_count(1);
  auto* three_user_mixed_faction = mixed_faction_template.add_faction_template();
  three_user_mixed_faction->set_user_number(3);
  three_user_mixed_faction->set_count(2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate two_user_template;
  two_user_template.set_id(3002);
  auto* two_user_template_faction = two_user_template.add_faction_template();
  two_user_template_faction->set_user_number(2);
  two_user_template_faction->set_count(3);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate incompatible_unit_template;
  incompatible_unit_template.set_id(4000);
  auto* two_user_faction = incompatible_unit_template.add_faction_template();
  two_user_faction->set_user_number(2);
  two_user_faction->set_count(2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate dynamic_ready_three_factions;
  dynamic_ready_three_factions.set_id(5000);
  auto* dynamic_ready_three_faction = dynamic_ready_three_factions.add_faction_template();
  dynamic_ready_three_faction->set_user_number(1);
  dynamic_ready_three_faction->set_count(3);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate dynamic_ready_two_factions;
  dynamic_ready_two_factions.set_id(5001);
  auto* dynamic_ready_two_faction = dynamic_ready_two_factions.add_faction_template();
  dynamic_ready_two_faction->set_user_number(1);
  dynamic_ready_two_faction->set_count(2);
  resource.set_file(
      "matching_result_template.bytes",
      make_table_bytes({result_template, alternate_result_template, convergence_template, faction_template,
                        mixed_faction_template, two_user_template, incompatible_unit_template,
                        dynamic_ready_three_factions, dynamic_ready_two_factions}));

  resource.set_file("const.bytes", make_empty_table_bytes());
  resource.set_file("dtmq_channel_type.bytes", make_empty_table_bytes());
  resource.set_file("item_type.bytes", make_empty_table_bytes());
  resource.set_file("level.bytes",
                    make_table_bytes({make_level(101, 1, 1), make_level(201, 1, 2), make_level(202, 1, 2),
                                      make_level(203, 1, 2), make_level(207, 1, 2), make_level(208, 2, 2),
                                      make_level(301, 1, 3), make_level(401, 1, 4), make_level(501, 1, 5)}));
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
  result.add_acceptable_level_ids(1);
  result.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE);
  auto* user = result.add_users();
  user->mutable_user_key()->set_user_id(user_id);
  user->mutable_user_key()->set_zone_id(1);
  protobuf_copy_message(*result.mutable_captain_user_key(), user->user_key());
  return result;
}

PROJECT_NAMESPACE_ID::DMatchingUnit make_party_unit(uint64_t unit_id, uint64_t first_user_id, int32_t user_count,
                                                    int32_t rank_level, bool allow_faction_fill) {
  PROJECT_NAMESPACE_ID::DMatchingUnit result;
  result.set_unit_id(unit_id);
  result.mutable_parameter()->set_rank_level(rank_level);
  result.add_acceptable_level_ids(1);
  result.set_faction_fill_policy(allow_faction_fill ? PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE
                                                    : PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  for (int32_t index = 0; index < user_count; ++index) {
    auto* user = result.add_users();
    user->mutable_user_key()->set_user_id(first_user_id + static_cast<uint64_t>(index));
    user->mutable_user_key()->set_zone_id(1);
  }
  protobuf_copy_message(*result.mutable_captain_user_key(), result.users(0).user_key());
  return result;
}

matching_room make_room() {
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(1);
  return matching_room{"logic-test", scope, 1, 100, 300};
}

void set_single_faction(matching_room& room, uint64_t unit_id, uint32_t capacity) {
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* faction = assignments.Add();
  faction->set_user_capacity(capacity);
  faction->add_unit_ids(unit_id);
  CASE_EXPECT_TRUE(room.set_faction_assignments(assignments));
}

bool start_runtime(atframework::testing::runtime& runtime) {
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::resource};
  options.working_directory = MATCHSVR_TEST_WORKING_DIRECTORY;
  options.setup_callback = [](atframework::testing::runtime& rt) {
    seed_matching_tables(rt.resource());
    return 0;
  };
  const int32_t start_result = runtime.start(options);
  CASE_EXPECT_EQ(0, start_result);
  CASE_EXPECT_TRUE(runtime.is_running());
  if (start_result == 0 && runtime.is_running()) {
    return true;
  }
  CASE_MSG_INFO() << "runtime start failed: " << runtime.get_diagnostic() << '\n';
  return false;
}

bool start_wal_runtime(atframework::testing::runtime& runtime) {
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};
  const int32_t start_result = runtime.start(options);
  CASE_EXPECT_EQ(0, start_result);
  CASE_EXPECT_TRUE(runtime.is_running());
  if (start_result == 0 && runtime.is_running()) {
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
  protobuf_copy_message(*result.mutable_unit(), make_unit(unit_id, user_id, rank_level, force_type));
  const int32_t default_level_id = matching_pool_id * 100 + 1;
  result.mutable_unit()->set_acceptable_level_ids(0, default_level_id);
  protobuf_copy_message(*result.mutable_operator_user(), result.unit().captain_user_key());
  return result;
}

PROJECT_NAMESPACE_ID::SSMatchingCreateReq make_party_create_request(uint64_t unit_id, uint64_t first_user_id,
                                                                    int32_t user_count, int32_t rank_level,
                                                                    bool allow_faction_fill = true) {
  PROJECT_NAMESPACE_ID::SSMatchingCreateReq result;
  result.mutable_scope()->set_level_type(1);
  result.mutable_scope()->set_region("cn");
  result.mutable_scope()->set_battle_version("1.0");
  result.mutable_scope()->set_matching_pool_id(3);
  protobuf_copy_message(*result.mutable_unit(),
                        make_party_unit(unit_id, first_user_id, user_count, rank_level, allow_faction_fill));
  result.mutable_unit()->set_acceptable_level_ids(0, 301);
  protobuf_copy_message(*result.mutable_operator_user(), result.unit().captain_user_key());
  return result;
}

void set_request_levels(PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                        std::initializer_list<int32_t> acceptable_level_ids) {
  request.mutable_unit()->clear_acceptable_level_ids();
  for (int32_t level_id : acceptable_level_ids) {
    request.mutable_unit()->add_acceptable_level_ids(level_id);
  }
}
}  // namespace

CASE_TEST(matchsvr_matching_wal, advances_subscriber_cursor_only_after_successful_delivery) {
  atframework::testing::runtime runtime;
  if (!start_wal_runtime(runtime)) {
    return;
  }

  constexpr uint64_t kLobbyServerId = 0x160001;
  atfw::testing::mock_node lobby_node;
  lobby_node.set_id(kLobbyServerId)
      .set_name("matching-wal-test-lobby")
      .set_type_id(4097)
      .set_type_name("matching-wal-test-lobby")
      .set_zone_id(1);
  auto remote = runtime.discovery().add_node(lobby_node);
  CASE_EXPECT_TRUE(!!remote);
  if (!remote) {
    runtime.stop();
    return;
  }

  auto room = make_room();
  auto unit = make_unit(1, 10001, 10);
  const bool unit_added = room.add_unit(unit);
  CASE_EXPECT_TRUE(unit_added);
  if (!unit_added) {
    runtime.stop();
    return;
  }
  rpc::context ctx{rpc::context::create_without_task()};
  const bool subscribed = room.subscribe(ctx, unit.users(0).user_key(), kLobbyServerId, 0);
  CASE_EXPECT_TRUE(subscribed);
  if (!subscribed) {
    runtime.stop();
    return;
  }
  runtime.transport().clear_history();

  atframework::testing::transport_send_behavior failed_send;
  failed_send.immediate_error = -12345;
  auto failed_send_rule = runtime.transport().add_rule(kLobbyServerId, -1, failed_send);

  PROJECT_NAMESPACE_ID::DMatchingEventLog event_log;
  protobuf_copy_message(*event_log.mutable_add_unit(), unit);
  room.publish(ctx, std::move(event_log));

  auto subscriber_route = room.get_subscriber_route(unit.users(0).user_key());
  CASE_EXPECT_TRUE(subscriber_route.has_value());
  if (subscriber_route.has_value()) {
    CASE_EXPECT_EQ(kLobbyServerId, subscriber_route->server_id);
    CASE_EXPECT_EQ(0, subscriber_route->acknowledge_event_id);
  }

  failed_send_rule.reset();
  const bool replayed = room.subscribe(ctx, unit.users(0).user_key(), kLobbyServerId, 0);
  CASE_EXPECT_TRUE(replayed);
  if (!replayed) {
    runtime.stop();
    return;
  }
  subscriber_route = room.get_subscriber_route(unit.users(0).user_key());
  CASE_EXPECT_TRUE(subscriber_route.has_value());
  if (subscriber_route.has_value()) {
    CASE_EXPECT_EQ(1, subscriber_route->acknowledge_event_id);
  }
  CASE_EXPECT_EQ(2, runtime.transport().outbound_count_to(kLobbyServerId));
  const auto* failed_record = runtime.transport().outbound_at(0);
  const auto* replay_record = runtime.transport().outbound_at(1);
  CASE_EXPECT_TRUE(nullptr != failed_record);
  CASE_EXPECT_TRUE(nullptr != replay_record);
  if (nullptr != failed_record && nullptr != replay_record) {
    CASE_EXPECT_EQ(-12345, failed_record->immediate_error);
    CASE_EXPECT_EQ(0, replay_record->immediate_error);
  }

  CASE_EXPECT_EQ(0, runtime.stop());
}

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
  protobuf_copy_message(*invalid.add_users(), invalid.users(0));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));
  invalid = valid;
  invalid.add_users()->mutable_user_key()->set_user_id(10002);
  invalid.mutable_users(1)->mutable_user_key()->set_zone_id(1);
  invalid.add_users()->mutable_user_key()->set_user_id(10003);
  invalid.mutable_users(2)->mutable_user_key()->set_zone_id(1);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, matching_logic::validate_unit(1, invalid));
  invalid = valid;
  invalid.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_UNSPECIFIED);
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
  set_single_faction(room, 1, 1);

  auto accepted = matching_logic::check_unit_can_join(room, make_unit(2, 10002, 14, 2), 110, 2);
  CASE_EXPECT_TRUE(accepted.evaluation.can_join());
  CASE_EXPECT_EQ(0, accepted.evaluation.result());

  auto rank_rejected = matching_logic::check_unit_can_join(room, make_unit(3, 10003, 16, 2), 110, 2);
  CASE_EXPECT_FALSE(rank_rejected.evaluation.can_join());
  auto force_rejected = matching_logic::check_unit_can_join(room, make_unit(4, 10004, 12, 1), 110, 2);
  CASE_EXPECT_FALSE(force_rejected.evaluation.can_join());

  auto oversized = make_unit(5, 10005, 12, 2);
  for (uint64_t user_id = 10006; user_id <= 10008; ++user_id) {
    oversized.add_users()->mutable_user_key()->set_user_id(user_id);
  }
  auto room_full = matching_logic::check_unit_can_join(room, oversized, 110, 2);
  CASE_EXPECT_FALSE(room_full.evaluation.can_join());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL, room_full.evaluation.result());

  CASE_EXPECT_TRUE(room.add_unit(make_unit(2, 10002, 14, 2)));
  CASE_EXPECT_TRUE(room.set_faction_assignments(accepted.evaluation.faction_assignments()));
  auto ready = matching_logic::check_room_ready(room, 110, 2);
  CASE_EXPECT_TRUE(ready.ready());
  CASE_EXPECT_EQ(1000, ready.result_template_id());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, selects_initial_faction_from_template_capacity_index) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  auto fillable = matching_logic::check_unit_can_create_room(scope, make_party_unit(21, 26001, 2, 10, true), 100, 2);
  CASE_EXPECT_TRUE(fillable.evaluation.can_join());
  CASE_EXPECT_EQ(1, fillable.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(3, fillable.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_EQ(2, fillable.evaluation.faction_assignments(0).assigned_user_count());

  auto exclusive = matching_logic::check_unit_can_create_room(scope, make_party_unit(22, 26003, 2, 10, false), 100, 2);
  CASE_EXPECT_TRUE(exclusive.evaluation.can_join());
  CASE_EXPECT_EQ(1, exclusive.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(2, exclusive.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_EQ(2, exclusive.evaluation.faction_assignments(0).assigned_user_count());

  scope.set_matching_pool_id(4);
  auto missing_slot =
      matching_logic::check_unit_can_create_room(scope, make_party_unit(23, 26005, 3, 10, true), 100, 3);
  CASE_EXPECT_FALSE(missing_slot.evaluation.can_join());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND, missing_slot.evaluation.result());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, keeps_template_eligible_until_room_start_threshold_is_reached) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(5);

  auto first_unit = make_unit(24, 27001, 10);
  auto created = matching_logic::check_unit_can_create_room(scope, first_unit, 100, 1);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (!created.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }

  matching_room room{"start-threshold", scope, 501, 100, 500};
  CASE_EXPECT_TRUE(room.add_unit(std::move(first_unit)));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  auto ready = matching_logic::check_room_ready(room, 100, 1);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());

  auto second_unit = make_unit(25, 27002, 10);
  auto joined = matching_logic::check_unit_can_join(room, second_unit, 162, 2);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_TRUE(room.add_unit(std::move(second_unit)));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  ready = matching_logic::check_room_ready(room, 162, 2);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_TRUE(ready.ready());
  CASE_EXPECT_EQ(5001, ready.result_template_id());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, keeps_template_dynamic_until_all_fixed_factions_are_full) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(make_unit(1, 10001, 10, 1)));
  room.set_result_template_id(1001);
  set_single_faction(room, 1, 1);

  const auto result = matching_logic::check_room_ready(room, 110, 1);
  CASE_EXPECT_EQ(0, result.result());
  CASE_EXPECT_FALSE(result.ready());
  CASE_EXPECT_EQ(0, result.result_template_id());

  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate result_template;
  result_template.set_id(1000);
  auto* faction = result_template.add_faction_template();
  faction->set_user_number(1);
  faction->set_count(2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate changed_alternate_template;
  changed_alternate_template.set_id(1001);
  auto* changed_alternate_faction = changed_alternate_template.add_faction_template();
  changed_alternate_faction->set_user_number(2);
  changed_alternate_faction->set_count(1);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate convergence_template;
  convergence_template.set_id(2000);
  auto* convergence_faction = convergence_template.add_faction_template();
  convergence_faction->set_user_number(1);
  convergence_faction->set_count(3);
  runtime.resource().set_file("matching_result_template.bytes",
                              make_table_bytes({result_template, changed_alternate_template, convergence_template}));
  runtime.resource().set_version("matchsvr-unit-test-v2");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);

  const auto reloaded_result = matching_logic::check_room_ready(room, 110, 1);
  CASE_EXPECT_EQ(0, reloaded_result.result());
  CASE_EXPECT_FALSE(reloaded_result.ready());
  CASE_EXPECT_EQ(0, reloaded_result.result_template_id());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rejects_banned_users_wrong_region_and_expired_rule_window) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  auto stored = make_unit(1, 10001, 10);
  protobuf_copy_message(*stored.add_ban_users(), make_unit(2, 10002, 10).users(0).user_key());
  CASE_EXPECT_TRUE(room.add_unit(stored));
  set_single_faction(room, 1, 1);
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(2, 10002, 10), 110, 2).evaluation.can_join());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(3, 10003, 10), 161, 2).evaluation.can_join());

  PROJECT_NAMESPACE_ID::DMatchingScope other_scope = room.get_scope();
  other_scope.set_region("us");
  matching_room other_room{"other-region", other_scope, 1, 100, 300};
  CASE_EXPECT_TRUE(other_room.add_unit(make_unit(4, 10004, 10)));
  CASE_EXPECT_FALSE(
      matching_logic::check_unit_can_join(other_room, make_unit(5, 10005, 10), 110, 2).evaluation.can_join());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rejects_bidirectional_unit_and_user_history_bans) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto stored = make_unit(1, 10001, 10);
  protobuf_copy_message(*stored.add_ban_users(), make_unit(2, 10002, 10).users(0).user_key());
  protobuf_copy_message(*stored.mutable_users(0)->add_lasting_ban_users(), make_unit(4, 10004, 10).users(0).user_key());
  protobuf_copy_message(*stored.mutable_users(0)->add_last_battle_users(), make_unit(6, 10006, 10).users(0).user_key());
  auto room = make_room();
  CASE_EXPECT_TRUE(room.add_unit(stored));
  set_single_faction(room, 1, 1);

  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(2, 10002, 10), 110, 2).evaluation.can_join());

  auto unit_bans_existing = make_unit(3, 10003, 10);
  protobuf_copy_message(*unit_bans_existing.add_ban_users(), stored.users(0).user_key());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, unit_bans_existing, 110, 2).evaluation.can_join());

  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(4, 10004, 10), 110, 2).evaluation.can_join());

  auto lasting_bans_existing = make_unit(5, 10005, 10);
  protobuf_copy_message(*lasting_bans_existing.mutable_users(0)->add_lasting_ban_users(), stored.users(0).user_key());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, lasting_bans_existing, 110, 2).evaluation.can_join());

  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(6, 10006, 10), 110, 2).evaluation.can_join());

  auto last_battle_with_existing = make_unit(7, 10007, 10);
  protobuf_copy_message(*last_battle_with_existing.mutable_users(0)->add_last_battle_users(),
                        stored.users(0).user_key());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, last_battle_with_existing, 110, 2).evaluation.can_join());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, selects_exact_template_for_preserved_factions) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"preserved-factions", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 10001, 2, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(2, 10003, 1, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(3, 10004, 3, 10, false)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* first_faction = assignments.Add();
  first_faction->set_user_capacity(3);
  first_faction->add_unit_ids(1);
  first_faction->add_unit_ids(2);
  auto* second_faction = assignments.Add();
  second_faction->set_user_capacity(3);
  second_faction->add_unit_ids(3);
  CASE_EXPECT_TRUE(room.set_faction_assignments(assignments));
  auto result = matching_logic::check_room_ready(room, 110, 6);
  CASE_EXPECT_TRUE(result.ready());
  CASE_EXPECT_EQ(3000, result.result_template_id());

  matching_room solo_room{"preserved-solo-factions", scope, 301, 100, 300};
  assignments.Clear();
  for (uint64_t unit_id = 1; unit_id <= 6; ++unit_id) {
    CASE_EXPECT_TRUE(solo_room.add_unit(make_party_unit(unit_id, 20000 + unit_id, 1, 10, true)));
    auto* faction = unit_id <= 3 ? (assignments.empty() ? assignments.Add() : assignments.Mutable(0))
                                 : (assignments.size() == 1 ? assignments.Add() : assignments.Mutable(1));
    faction->set_user_capacity(3);
    faction->add_unit_ids(unit_id);
  }
  CASE_EXPECT_TRUE(solo_room.set_faction_assignments(assignments));
  result = matching_logic::check_room_ready(solo_room, 110, 6);
  CASE_EXPECT_TRUE(result.ready());
  CASE_EXPECT_EQ(3000, result.result_template_id());
  for (const auto& faction : solo_room.get_faction_assignments()) {
    CASE_EXPECT_EQ(3, faction.unit_ids_size());
  }

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, accepts_multiple_incomplete_fixed_factions) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room single_pending_room{"single-pending", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(single_pending_room.add_unit(make_party_unit(1, 21001, 1, 10, true)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> assignments;
  auto* faction = assignments.Add();
  faction->set_user_capacity(3);
  faction->add_unit_ids(1);
  CASE_EXPECT_TRUE(single_pending_room.set_faction_assignments(assignments));
  auto result = matching_logic::check_room_ready(single_pending_room, 110, 1);
  CASE_EXPECT_EQ(0, result.result());
  CASE_EXPECT_FALSE(result.ready());
  CASE_EXPECT_EQ(3, single_pending_room.get_faction_assignments().Get(0).user_capacity());

  matching_room exclusive_room{"exclusive-pending", scope, 301, 100, 300};
  assignments.Clear();
  CASE_EXPECT_TRUE(exclusive_room.add_unit(make_party_unit(2, 22001, 2, 10, false)));
  faction = assignments.Add();
  faction->set_user_capacity(2);
  faction->add_unit_ids(2);
  CASE_EXPECT_TRUE(exclusive_room.set_faction_assignments(assignments));
  result = matching_logic::check_room_ready(exclusive_room, 110, 2);
  CASE_EXPECT_EQ(0, result.result());
  CASE_EXPECT_FALSE(result.ready());

  matching_room multiple_pending_room{"multiple-pending", scope, 301, 100, 300};
  assignments.Clear();
  CASE_EXPECT_TRUE(multiple_pending_room.add_unit(make_party_unit(3, 22003, 2, 10, true)));
  CASE_EXPECT_TRUE(multiple_pending_room.add_unit(make_party_unit(4, 22005, 2, 10, true)));
  CASE_EXPECT_TRUE(multiple_pending_room.add_unit(make_party_unit(5, 22007, 1, 10, true)));
  auto* first_faction = assignments.Add();
  first_faction->set_user_capacity(3);
  first_faction->add_unit_ids(3);
  auto* second_faction = assignments.Add();
  second_faction->set_user_capacity(3);
  second_faction->add_unit_ids(4);
  second_faction->add_unit_ids(5);
  CASE_EXPECT_TRUE(multiple_pending_room.set_faction_assignments(assignments));
  result = matching_logic::check_room_ready(multiple_pending_room, 110, 5);
  CASE_EXPECT_EQ(0, result.result());
  CASE_EXPECT_FALSE(result.ready());
  CASE_EXPECT_EQ(2, multiple_pending_room.get_faction_assignments().size());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, creates_a_second_incomplete_faction_when_existing_gap_does_not_fit) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"single-pending-faction", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 23001, 2, 10, true)));
  room.set_result_template_id(3001);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction = initial_assignments.Add();
  faction->set_user_capacity(3);
  faction->add_unit_ids(1);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  const auto accepted = matching_logic::check_unit_can_join(room, make_party_unit(2, 23003, 2, 10, true), 110, 4);
  CASE_EXPECT_TRUE(accepted.evaluation.can_join());
  CASE_EXPECT_EQ(2, accepted.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(3, accepted.evaluation.faction_assignments(1).user_capacity());
  CASE_EXPECT_EQ(2, accepted.evaluation.faction_assignments(1).assigned_user_count());

  const auto completed = matching_logic::check_unit_can_join(room, make_party_unit(3, 23005, 1, 10, true), 110, 3);
  CASE_EXPECT_TRUE(completed.evaluation.can_join());
  CASE_EXPECT_EQ(1, completed.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(2, completed.evaluation.faction_assignments(0).unit_ids_size());
  CASE_EXPECT_EQ(3, completed.evaluation.faction_assignments(0).assigned_user_count());
  CASE_EXPECT_TRUE(completed.progress.has_faction);
  CASE_EXPECT_TRUE(completed.progress.joins_existing);
  CASE_EXPECT_TRUE(completed.progress.completes_faction);
  CASE_EXPECT_EQ(0, completed.progress.remaining_user_count);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rebalance_unit_only_fills_an_existing_faction) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"rebalance-existing-faction-only", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 23101, 2, 10, true)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction = initial_assignments.Add();
  faction->set_user_capacity(3);
  faction->add_unit_ids(1);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  const auto would_create_faction =
      matching_logic::check_unit_can_join_for_rebalance(room, make_party_unit(2, 23103, 2, 10, true), 110, 4);
  CASE_EXPECT_FALSE(would_create_faction.evaluation.can_join());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND, would_create_faction.evaluation.result());
  CASE_EXPECT_EQ(0, would_create_faction.evaluation.faction_assignments_size());
  CASE_EXPECT_FALSE(would_create_faction.progress.has_faction);

  const auto fills_existing =
      matching_logic::check_unit_can_join_for_rebalance(room, make_party_unit(3, 23105, 1, 10, true), 110, 3);
  CASE_EXPECT_TRUE(fills_existing.evaluation.can_join());
  CASE_EXPECT_EQ(1, fills_existing.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(2, fills_existing.evaluation.faction_assignments(0).unit_ids_size());
  CASE_EXPECT_EQ(3, fills_existing.evaluation.faction_assignments(0).unit_ids(1));
  CASE_EXPECT_TRUE(fills_existing.progress.has_faction);
  CASE_EXPECT_TRUE(fills_existing.progress.joins_existing);
  CASE_EXPECT_TRUE(fills_existing.progress.completes_faction);
  CASE_EXPECT_EQ(0, fills_existing.progress.remaining_user_count);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, fills_the_pending_faction_before_opening_another) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"incremental-faction", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 30001, 1, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(2, 30002, 1, 10, true)));
  room.set_result_template_id(3000);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction_zero = initial_assignments.Add();
  faction_zero->set_user_capacity(3);
  faction_zero->add_unit_ids(1);
  faction_zero->add_unit_ids(2);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  auto joined = matching_logic::check_unit_can_join(room, make_party_unit(3, 30003, 1, 10, true), 110, 3);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_EQ(1, joined.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(0).unit_ids_size());
  CASE_EXPECT_EQ(1, joined.evaluation.faction_assignments(0).unit_ids(0));
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(0).unit_ids(1));
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(0).unit_ids(2));
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(0).assigned_user_count());

  matching_room create_faction_room{"create-faction", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(create_faction_room.add_unit(make_party_unit(11, 31001, 2, 10, true)));
  CASE_EXPECT_TRUE(create_faction_room.add_unit(make_party_unit(13, 31005, 1, 10, true)));
  create_faction_room.set_result_template_id(3000);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> create_faction_assignments;
  auto* existing_faction = create_faction_assignments.Add();
  existing_faction->set_user_capacity(3);
  existing_faction->add_unit_ids(11);
  existing_faction->add_unit_ids(13);
  CASE_EXPECT_TRUE(create_faction_room.set_faction_assignments(create_faction_assignments));

  joined = matching_logic::check_unit_can_join(create_faction_room, make_party_unit(12, 31003, 2, 10, true), 110, 4);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(11, joined.evaluation.faction_assignments(0).unit_ids(0));
  CASE_EXPECT_EQ(12, joined.evaluation.faction_assignments(1).unit_ids(0));

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, does_not_rebuild_factions_after_incremental_assignment_fails) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"fallback-faction", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 32001, 2, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(2, 32003, 1, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(3, 32004, 1, 10, true)));
  room.set_result_template_id(3000);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction_zero = initial_assignments.Add();
  faction_zero->set_user_capacity(3);
  faction_zero->add_unit_ids(1);
  auto* faction_one = initial_assignments.Add();
  faction_one->set_user_capacity(3);
  faction_one->add_unit_ids(2);
  faction_one->add_unit_ids(3);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  auto joined = matching_logic::check_unit_can_join(room, make_party_unit(4, 32005, 2, 10, true), 110, 6);
  CASE_EXPECT_FALSE(joined.evaluation.can_join());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, keeps_created_faction_capacity_when_larger_templates_exist) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"fixed-capacity", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 33001, 1, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(2, 33002, 1, 10, true)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction = initial_assignments.Add();
  faction->set_user_capacity(2);
  faction->add_unit_ids(1);
  faction->add_unit_ids(2);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  const auto joined = matching_logic::check_unit_can_join(room, make_party_unit(3, 33003, 1, 10, true), 110, 3);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(1).user_capacity());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, keeps_multiple_incomplete_factions_after_cancellation) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room room{"cancel-gaps", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(1, 34001, 2, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(2, 34003, 1, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(3, 34004, 2, 10, true)));
  CASE_EXPECT_TRUE(room.add_unit(make_party_unit(4, 34006, 1, 10, true)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* first_faction = initial_assignments.Add();
  first_faction->set_user_capacity(3);
  first_faction->add_unit_ids(1);
  first_faction->add_unit_ids(2);
  auto* second_faction = initial_assignments.Add();
  second_faction->set_user_capacity(3);
  second_faction->add_unit_ids(3);
  second_faction->add_unit_ids(4);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  CASE_EXPECT_TRUE(room.remove_unit(2));
  CASE_EXPECT_TRUE(room.remove_unit(4));
  const auto ready = matching_logic::check_room_ready(room, 110, 4);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());
  CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(3, room.get_faction_assignments().Get(0).user_capacity());
  CASE_EXPECT_EQ(3, room.get_faction_assignments().Get(1).user_capacity());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, moves_a_complete_faction_without_merging_or_splitting_it) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(3);

  matching_room target{"faction-target", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(target.add_unit(make_party_unit(10, 35001, 2, 10, true)));
  set_single_faction(target, 10, 3);

  std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit> source_units;
  source_units.emplace_back(make_party_unit(11, 35101, 2, 10, true));
  source_units.emplace_back(make_party_unit(12, 35103, 1, 10, true));
  const auto source_view = make_unit_view(source_units);
  const auto joined = matching_logic::check_faction_can_join(target, source_view, 3, 110, 5);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(1, joined.evaluation.faction_assignments(0).unit_ids_size());
  CASE_EXPECT_EQ(10, joined.evaluation.faction_assignments(0).unit_ids(0));
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(1).unit_ids_size());
  CASE_EXPECT_EQ(11, joined.evaluation.faction_assignments(1).unit_ids(0));
  CASE_EXPECT_EQ(12, joined.evaluation.faction_assignments(1).unit_ids(1));
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(1).user_capacity());
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(1).assigned_user_count());
  CASE_EXPECT_TRUE(joined.progress.has_faction);
  CASE_EXPECT_FALSE(joined.progress.joins_existing);
  CASE_EXPECT_TRUE(joined.progress.completes_faction);
  CASE_EXPECT_EQ(0, joined.progress.remaining_user_count);

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, intersects_level_candidates_inside_one_coarse_bucket) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};

  auto first = make_create_request(801, 98001, 10, 0, 2);
  set_request_levels(first, {202, 201, 202});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot first_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first, first_response));
  CASE_EXPECT_EQ(201, first_response.snapshot().selected_level_id());

  auto overlapping = make_create_request(802, 98002, 11, 0, 2);
  set_request_levels(overlapping, {203, 202});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot overlapping_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, overlapping, overlapping_response));
  CASE_EXPECT_EQ(first_response.snapshot().matching_id(), overlapping_response.snapshot().matching_id());
  CASE_EXPECT_EQ(202, overlapping_response.snapshot().selected_level_id());

  auto disjoint = make_create_request(803, 98003, 12, 0, 2);
  set_request_levels(disjoint, {203});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot disjoint_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, disjoint, disjoint_response));
  CASE_EXPECT_TRUE(first_response.snapshot().matching_id() != disjoint_response.snapshot().matching_id());
  CASE_EXPECT_EQ(203, disjoint_response.snapshot().selected_level_id());
  CASE_EXPECT_EQ(2, manager->get_room_count());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rejects_create_without_level_candidates) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_create_request(804, 98004, 10, 0, 2);
  request.mutable_unit()->clear_acceptable_level_ids();
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                 manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(0, manager->get_room_count());

  manager->clear();
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
  protobuf_copy_message(*confirm.mutable_operator_user(), first_request.operator_user());
  confirm.set_confirmed(true);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot confirm_response;
  const int64_t last_event_id_before_confirm = second_response.snapshot().last_event_id();
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, confirm_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, confirm_response.snapshot().status());
  CASE_EXPECT_EQ(last_event_id_before_confirm, confirm_response.snapshot().last_event_id());

  confirm.set_unit_id(2);
  protobuf_copy_message(*confirm.mutable_operator_user(), second_request.operator_user());
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
  auto cross_pool_level = make_create_request(13, 20003, 10);
  set_request_levels(cross_pool_level, {201});
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                 manager->create_matching(ctx, cross_pool_level, conflict_response));
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
  protobuf_copy_message(*cancel.mutable_operator_user(), check.operator_user());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_FOUND, manager->cancel_matching(ctx, cancel, response));
  protobuf_copy_message(*cancel.mutable_operator_user(), request.operator_user());
  CASE_EXPECT_EQ(0, manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CANCELLED, response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rejects_first_unit_that_cannot_fit_any_faction_template) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_party_create_request(20, 25001, 3, 10);
  request.mutable_scope()->set_matching_pool_id(4);
  set_request_levels(request, {401});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND,
                 manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(0, manager->get_room_count());
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
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(candidate_response.snapshot().matching_id()));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, prefers_a_newer_room_when_it_has_a_pending_faction) {
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
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(24, 31001, 2, 0), oldest_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(25, 31003, 1, 2), oldest_response));
  const std::string oldest_matching_id = oldest_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot pending_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(26, 31101, 2, 100), pending_response));
  const std::string pending_matching_id = pending_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(oldest_matching_id != pending_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot joined_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(27, 31201, 1, 50), joined_response));
  CASE_EXPECT_EQ(pending_matching_id, joined_response.snapshot().matching_id());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(pending_matching_id));
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(oldest_matching_id));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, confirms_a_source_that_becomes_ready_before_rebalance) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(28, 31301, 100, 0, 5), target_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(29, 31302, 102, 0, 5), target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source_first = make_create_request(30, 31401, 0, 0, 5);
  auto source_second = make_create_request(31, 31402, 2, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_first, source_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_second, source_response));
  const std::string source_matching_id = source_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(source_matching_id != target_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(source_matching_id);
  check.set_unit_id(source_first.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), source_first.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, checked_response.snapshot().status());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(source_matching_id));
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, skips_a_target_that_is_already_ready_during_rebalance) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto source_request = make_create_request(32, 31501, 0, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_request, source_response));
  const std::string source_matching_id = source_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(33, 31601, 100, 0, 5), target_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(34, 31602, 102, 0, 5), target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(source_matching_id != target_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(source_matching_id);
  check.set_unit_id(source_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), source_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, checked_response.snapshot().status());
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(source_matching_id));
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, fills_an_older_target_with_multiple_atoms_in_one_rebalance) {
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
  const std::string target_matching_id = isolated_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto larger_request = make_create_request(32, 40002, 100, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot larger_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, larger_request, larger_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(33, 40003, 102, 0, 2), larger_response));
  const std::string donor_matching_id = larger_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(target_matching_id != donor_matching_id);
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(donor_matching_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, larger_response.snapshot().status());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(3, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(0, manager->get_room_unit_count(donor_matching_id));

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(target_matching_id);
  check.set_unit_id(isolated_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), isolated_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot migrated_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, migrated_response.snapshot().status());
  CASE_EXPECT_EQ(2, manager->get_room_count());

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq stale_source_check;
  stale_source_check.set_matching_id(donor_matching_id);
  stale_source_check.set_unit_id(larger_request.unit().unit_id());
  protobuf_copy_message(*stale_source_check.mutable_operator_user(), larger_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, stale_source_check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.snapshot().matching_id());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, migrated_response));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rebalances_from_oldest_compatible_donor_first) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto target_request = make_create_request(34, 40101, 0, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_request, target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto older_donor_request = make_create_request(35, 40102, 100, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot older_donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, older_donor_request, older_donor_response));
  const std::string older_donor_matching_id = older_donor_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{2});
  auto newer_donor_request = make_create_request(36, 40103, 200, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot newer_donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, newer_donor_request, newer_donor_response));
  const std::string newer_donor_matching_id = newer_donor_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(target_matching_id != older_donor_matching_id);
  CASE_EXPECT_TRUE(target_matching_id != newer_donor_matching_id);
  CASE_EXPECT_TRUE(older_donor_matching_id != newer_donor_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{63});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq target_check;
  target_check.set_matching_id(target_matching_id);
  target_check.set_unit_id(target_request.unit().unit_id());
  protobuf_copy_message(*target_check.mutable_operator_user(), target_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, target_check, target_checked_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, target_checked_response.snapshot().status());

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq older_donor_check;
  older_donor_check.set_matching_id(older_donor_matching_id);
  older_donor_check.set_unit_id(older_donor_request.unit().unit_id());
  protobuf_copy_message(*older_donor_check.mutable_operator_user(), older_donor_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot older_donor_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, older_donor_check, older_donor_checked_response));
  CASE_EXPECT_EQ(target_matching_id, older_donor_checked_response.snapshot().matching_id());

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq newer_donor_check;
  newer_donor_check.set_matching_id(newer_donor_matching_id);
  newer_donor_check.set_unit_id(newer_donor_request.unit().unit_id());
  protobuf_copy_message(*newer_donor_check.mutable_operator_user(), newer_donor_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot newer_donor_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, newer_donor_check, newer_donor_checked_response));
  CASE_EXPECT_EQ(newer_donor_matching_id, newer_donor_checked_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING,
                 newer_donor_checked_response.snapshot().status());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, target_rebalance_does_not_create_a_new_incomplete_faction) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto target_request = make_party_create_request(91, 89001, 2, 0);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_request, target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto donor_request = make_party_create_request(92, 89101, 2, 100);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, donor_request, donor_response));
  const std::string donor_matching_id = donor_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(target_matching_id != donor_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(target_matching_id);
  check.set_unit_id(target_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), target_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(target_matching_id, checked_response.snapshot().matching_id());
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(donor_matching_id));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rebalances_a_complete_faction_atomically) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto target_duo = make_party_create_request(101, 90001, 2, 0);
  auto target_solo_one = make_party_create_request(102, 90003, 1, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_duo, target_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_solo_one, target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, target_response.snapshot().status());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source_duo_one = make_party_create_request(104, 90101, 2, 100);
  auto source_solo_one = make_party_create_request(105, 90103, 1, 102);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_duo_one, source_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_solo_one, source_response));
  const std::string source_matching_id = source_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(target_matching_id != source_matching_id);
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(source_matching_id));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check_moved;
  check_moved.set_matching_id(target_matching_id);
  check_moved.set_unit_id(target_duo.unit().unit_id());
  protobuf_copy_message(*check_moved.mutable_operator_user(), target_duo.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot moved_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check_moved, moved_response));
  CASE_EXPECT_EQ(target_matching_id, moved_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, moved_response.snapshot().status());
  CASE_EXPECT_EQ(4, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(2, manager->get_room_faction_count(target_matching_id));

  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check_faction_member;
  check_faction_member.set_matching_id(source_matching_id);
  check_faction_member.set_unit_id(source_solo_one.unit().unit_id());
  protobuf_copy_message(*check_faction_member.mutable_operator_user(), source_solo_one.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot faction_member_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check_faction_member, faction_member_response));
  CASE_EXPECT_EQ(target_matching_id, faction_member_response.snapshot().matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, faction_member_response.snapshot().status());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, rejects_rebalance_when_level_candidate_intersection_is_empty) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};

  auto target_one = make_create_request(811, 98101, 100, 0, 2);
  auto target_two = make_create_request(812, 98102, 102, 0, 2);
  set_request_levels(target_one, {201});
  set_request_levels(target_two, {201});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_one, target_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, target_two, target_response));
  const std::string target_matching_id = target_response.snapshot().matching_id();
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source = make_create_request(813, 98103, 0, 0, 2);
  set_request_levels(source, {202});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source, source_response));
  const std::string source_matching_id = source_response.snapshot().matching_id();
  CASE_EXPECT_TRUE(target_matching_id != source_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(source_matching_id);
  check.set_unit_id(source.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), source.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.snapshot().matching_id());
  CASE_EXPECT_EQ(202, checked_response.snapshot().selected_level_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, checked_response.snapshot().status());
  CASE_EXPECT_EQ(2, manager->get_room_count());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, isolates_rooms_by_every_coarse_scope_dimension) {
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
  set_request_levels(different_level, {208});
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
  protobuf_copy_message(*check.mutable_operator_user(), request.operator_user());
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
  protobuf_copy_message(*confirm.mutable_operator_user(), accepted_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  const int64_t last_event_id_before_timeout = response.snapshot().last_event_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{16});
  CASE_EXPECT_EQ(0, manager->tick());
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_unit_id(accepted_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), accepted_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_MATCHING, response.snapshot().status());
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(response.snapshot().matching_id()));
  CASE_EXPECT_EQ(last_event_id_before_timeout + 1, response.snapshot().last_event_id());
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, timeout_request, retry_response));

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, validates_and_idempotently_handles_orbit_start_failure) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto first_request = make_create_request(61, 79001, 10, 1);
  auto second_request = make_create_request(62, 79002, 14, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot snapshot;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, snapshot));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, snapshot));
  const std::string matching_id = snapshot.snapshot().matching_id();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CONFIRMING, snapshot.snapshot().status());
  CASE_EXPECT_EQ(0, snapshot.snapshot().faction_id());
  const int64_t now = atfw::util::time::time_utility::get_now();
  constexpr uint64_t kOrbitServerId = 0x180001;
  CASE_EXPECT_TRUE(manager->prepare_battle_creation_for_test(matching_id, kOrbitServerId, now + 120));

  auto invoke_ready = [&](bool start_success, uint64_t source_server_id,
                          PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp& response) {
    PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq request;
    request.set_matching_id(matching_id);
    request.set_start_success(start_success);
    auto task = runtime.run_task(
        "matching.orbit_room_ready", std::chrono::seconds{2},
        [manager, &request, &response, source_server_id](rpc::context& task_ctx) -> rpc::result_code_type {
          const int32_t result =
              RPC_AWAIT_CODE_RESULT(manager->orbit_room_ready(task_ctx, request, response, source_server_id));
          RPC_RETURN_CODE(result);
        });
    CASE_EXPECT_FALSE(task.empty());
    if (task.empty()) {
      return false;
    }
    auto result = runtime.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_FALSE(result.hard_timed_out);
    CASE_EXPECT_EQ(0, result.result_code);
    return result.task_exited && !result.hard_timed_out && result.result_code == 0;
  };

  PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp orbit_response;
  CASE_EXPECT_TRUE(invoke_ready(false, kOrbitServerId + 1, orbit_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_CONFLICT, orbit_response.result());
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(matching_id);
  check.set_unit_id(first_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), first_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_CREATING_BATTLE, snapshot.snapshot().status());
  CASE_EXPECT_EQ(1001, snapshot.snapshot().faction_id());
  check.set_unit_id(second_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(1002, snapshot.snapshot().faction_id());
  check.set_unit_id(first_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), first_request.operator_user());

  orbit_response.Clear();
  CASE_EXPECT_TRUE(invoke_ready(false, kOrbitServerId, orbit_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED, orbit_response.result());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, snapshot.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED, snapshot.snapshot().result());

  orbit_response.Clear();
  CASE_EXPECT_TRUE(invoke_ready(false, kOrbitServerId, orbit_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED, orbit_response.result());
  orbit_response.Clear();
  CASE_EXPECT_TRUE(invoke_ready(true, kOrbitServerId, orbit_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED, orbit_response.result());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, times_out_battle_creation_and_rejects_late_ready) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto first_request = make_create_request(63, 79101, 10, 1);
  auto second_request = make_create_request(64, 79102, 14, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot snapshot;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, snapshot));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, snapshot));
  const std::string matching_id = snapshot.snapshot().matching_id();
  const int64_t now = atfw::util::time::time_utility::get_now();
  constexpr uint64_t kOrbitServerId = 0x180002;
  CASE_EXPECT_TRUE(manager->prepare_battle_creation_for_test(matching_id, kOrbitServerId, now + 5));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{6});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq check;
  check.set_matching_id(matching_id);
  check.set_unit_id(first_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_operator_user(), first_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, snapshot.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, snapshot.snapshot().result());

  PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq ready_request;
  ready_request.set_matching_id(matching_id);
  ready_request.set_start_success(true);
  PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp ready_response;
  auto task = runtime.run_task(
      "matching.late_orbit_room_ready", std::chrono::seconds{2},
      [manager, &ready_request, &ready_response](rpc::context& task_ctx) -> rpc::result_code_type {
        const int32_t result =
            RPC_AWAIT_CODE_RESULT(manager->orbit_room_ready(task_ctx, ready_request, ready_response, kOrbitServerId));
        RPC_RETURN_CODE(result);
      });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = runtime.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_FALSE(result.hard_timed_out);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, ready_response.result());

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
  protobuf_copy_message(*confirm.mutable_operator_user(), first_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  confirm.set_unit_id(second_request.unit().unit_id());
  protobuf_copy_message(*confirm.mutable_operator_user(), second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_ROOM_STATUS_FAILED, response.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, response.snapshot().result());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, retry_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, retry_response));

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}
