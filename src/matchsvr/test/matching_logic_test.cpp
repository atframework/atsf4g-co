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
#include <logic/matching/matching_unit.h>

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

void seed_matching_tables(atframework::testing::mock_resource& resource, bool relaxed_balanced_windows = false) {
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool pool;
  pool.set_id(1);
  pool.set_max_user_cout_limit(4);
  pool.set_max_faction_cout_limit(2);
  pool.set_unit_max_size(2);
  pool.set_faction_user_max_size(2);
  pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  pool.add_rule_group_ids(10);
  pool.set_search_timeout_seconds(120);
  pool.set_confirm_timeout_seconds(15);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool convergence_pool;
  convergence_pool.set_id(2);
  convergence_pool.set_max_user_cout_limit(3);
  convergence_pool.set_max_faction_cout_limit(3);
  convergence_pool.set_unit_max_size(1);
  convergence_pool.set_faction_user_max_size(1);
  convergence_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  convergence_pool.add_rule_group_ids(20);
  convergence_pool.set_search_timeout_seconds(120);
  convergence_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool faction_pool;
  faction_pool.set_id(3);
  faction_pool.set_max_user_cout_limit(6);
  faction_pool.set_max_faction_cout_limit(3);
  faction_pool.set_unit_max_size(3);
  faction_pool.set_faction_user_max_size(3);
  faction_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  faction_pool.add_rule_group_ids(30);
  faction_pool.set_search_timeout_seconds(120);
  faction_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool incompatible_unit_pool;
  incompatible_unit_pool.set_id(4);
  incompatible_unit_pool.set_max_user_cout_limit(4);
  incompatible_unit_pool.set_max_faction_cout_limit(2);
  incompatible_unit_pool.set_unit_max_size(3);
  incompatible_unit_pool.set_faction_user_max_size(3);
  incompatible_unit_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  incompatible_unit_pool.add_rule_group_ids(40);
  incompatible_unit_pool.set_search_timeout_seconds(120);
  incompatible_unit_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool dynamic_ready_pool;
  dynamic_ready_pool.set_id(5);
  dynamic_ready_pool.set_max_user_cout_limit(3);
  dynamic_ready_pool.set_max_faction_cout_limit(3);
  dynamic_ready_pool.set_unit_max_size(1);
  dynamic_ready_pool.set_faction_user_max_size(1);
  dynamic_ready_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  dynamic_ready_pool.add_rule_group_ids(50);
  dynamic_ready_pool.set_search_timeout_seconds(120);
  dynamic_ready_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool partial_template_pool;
  partial_template_pool.set_id(6);
  partial_template_pool.set_max_user_cout_limit(3);
  partial_template_pool.set_max_faction_cout_limit(3);
  partial_template_pool.set_unit_max_size(1);
  partial_template_pool.set_faction_user_max_size(1);
  partial_template_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  partial_template_pool.add_rule_group_ids(60);
  partial_template_pool.set_search_timeout_seconds(120);
  partial_template_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool balanced_pool;
  balanced_pool.set_id(7);
  balanced_pool.set_max_user_cout_limit(9);
  balanced_pool.set_max_faction_cout_limit(3);
  balanced_pool.set_unit_max_size(3);
  balanced_pool.set_faction_user_max_size(3);
  balanced_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_BALANCED);
  balanced_pool.add_rule_group_ids(70);
  balanced_pool.set_search_timeout_seconds(120);
  balanced_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool two_faction_pool;
  two_faction_pool.set_id(8);
  two_faction_pool.set_max_user_cout_limit(6);
  two_faction_pool.set_max_faction_cout_limit(2);
  two_faction_pool.set_unit_max_size(3);
  two_faction_pool.set_faction_user_max_size(3);
  two_faction_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  two_faction_pool.add_rule_group_ids(30);
  two_faction_pool.set_search_timeout_seconds(120);
  two_faction_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool minimum_faction_pool;
  minimum_faction_pool.set_id(9);
  minimum_faction_pool.set_max_user_cout_limit(4);
  minimum_faction_pool.set_max_faction_cout_limit(2);
  minimum_faction_pool.set_unit_max_size(2);
  minimum_faction_pool.set_faction_user_max_size(2);
  minimum_faction_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  minimum_faction_pool.add_rule_group_ids(90);
  minimum_faction_pool.set_search_timeout_seconds(120);
  minimum_faction_pool.set_confirm_timeout_seconds(15);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingPool mixed_template_pool;
  mixed_template_pool.set_id(10);
  mixed_template_pool.set_max_user_cout_limit(5);
  mixed_template_pool.set_max_faction_cout_limit(2);
  mixed_template_pool.set_unit_max_size(3);
  mixed_template_pool.set_faction_user_max_size(3);
  mixed_template_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  mixed_template_pool.add_rule_group_ids(100);
  mixed_template_pool.set_search_timeout_seconds(120);
  mixed_template_pool.set_confirm_timeout_seconds(15);
  auto small_balanced_pool = incompatible_unit_pool;
  small_balanced_pool.set_id(11);
  small_balanced_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_BALANCED);
  auto invalid_balanced_pool = balanced_pool;
  invalid_balanced_pool.set_id(12);
  invalid_balanced_pool.clear_rule_group_ids();
  invalid_balanced_pool.add_rule_group_ids(30);
  auto small_unit_priority_pool = two_faction_pool;
  small_unit_priority_pool.set_id(13);
  small_unit_priority_pool.set_unit_max_size(2);
  auto small_unit_balanced_pool = balanced_pool;
  small_unit_balanced_pool.set_id(14);
  small_unit_balanced_pool.set_unit_max_size(2);
  small_unit_balanced_pool.set_max_faction_cout_limit(2);
  small_unit_balanced_pool.set_max_user_cout_limit(6);
  auto small_faction_pool = incompatible_unit_pool;
  small_faction_pool.set_id(15);
  small_faction_pool.set_faction_user_max_size(2);
  small_faction_pool.set_max_user_cout_limit(6);
  auto undersized_faction_pool = small_faction_pool;
  undersized_faction_pool.set_id(16);
  undersized_faction_pool.set_faction_user_max_size(1);
  auto missing_faction_capacity_pool = small_faction_pool;
  missing_faction_capacity_pool.set_id(17);
  missing_faction_capacity_pool.clear_faction_user_max_size();
  auto unreachable_exclusive_pool = mixed_template_pool;
  unreachable_exclusive_pool.set_id(18);
  unreachable_exclusive_pool.set_unit_max_size(1);
  auto undersized_balanced_pool = small_balanced_pool;
  undersized_balanced_pool.set_id(19);
  undersized_balanced_pool.set_faction_user_max_size(1);
  auto downgrade_pool = small_unit_balanced_pool;
  downgrade_pool.set_id(20);
  downgrade_pool.set_unit_max_size(3);
  downgrade_pool.clear_rule_group_ids();
  downgrade_pool.add_rule_group_ids(200);
  auto priority_downgrade_pool = downgrade_pool;
  priority_downgrade_pool.set_id(21);
  priority_downgrade_pool.set_faction_fill_policy(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_PRIORITY);
  resource.set_file("matching_pool.bytes", make_table_bytes({pool,
                                                             convergence_pool,
                                                             faction_pool,
                                                             incompatible_unit_pool,
                                                             dynamic_ready_pool,
                                                             partial_template_pool,
                                                             balanced_pool,
                                                             two_faction_pool,
                                                             minimum_faction_pool,
                                                             mixed_template_pool,
                                                             small_balanced_pool,
                                                             invalid_balanced_pool,
                                                             small_unit_priority_pool,
                                                             small_unit_balanced_pool,
                                                             small_faction_pool,
                                                             undersized_faction_pool,
                                                             missing_faction_capacity_pool,
                                                             unreachable_exclusive_pool,
                                                             undersized_balanced_pool,
                                                             downgrade_pool,
                                                             priority_downgrade_pool}));

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
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup partial_template_group;
  partial_template_group.set_group_id(60);
  partial_template_group.set_global_user_lower(0);
  partial_template_group.set_global_user_upper(100);
  partial_template_group.add_pool_rules(600);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup minimum_faction_group;
  minimum_faction_group.set_group_id(90);
  minimum_faction_group.set_global_user_lower(0);
  minimum_faction_group.set_global_user_upper(100);
  minimum_faction_group.add_pool_rules(900);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup mixed_template_group;
  mixed_template_group.set_group_id(100);
  mixed_template_group.set_global_user_lower(0);
  mixed_template_group.set_global_user_upper(100);
  mixed_template_group.add_pool_rules(1000);
  auto balanced_group = faction_group;
  balanced_group.set_group_id(70);
  balanced_group.clear_pool_rules();
  balanced_group.add_pool_rules(700);
  balanced_group.add_pool_rules(701);
  auto downgrade_group = balanced_group;
  downgrade_group.set_group_id(200);
  downgrade_group.clear_pool_rules();
  downgrade_group.add_pool_rules(2000);
  downgrade_group.add_pool_rules(2001);
  downgrade_group.add_pool_rules(2002);
  resource.set_file("matching_rule_group.bytes",
                    make_table_bytes({group, convergence_group, faction_group, incompatible_unit_group,
                                      dynamic_ready_group, partial_template_group, minimum_faction_group,
                                      mixed_template_group, balanced_group, downgrade_group}));

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule rule;
  rule.set_id(100);
  rule.mutable_time_limit()->set_min(0);
  rule.mutable_time_limit()->set_max(60);
  rule.set_min_user_cout_limit(2);
  rule.set_min_faction_count_limit(1);
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
  strict_rule.set_min_user_cout_limit(3);
  strict_rule.set_min_faction_count_limit(3);
  auto* strict_rank_rule = strict_rule.add_rules();
  strict_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  strict_rank_rule->add_values(5);

  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule relaxed_rule;
  relaxed_rule.set_id(201);
  relaxed_rule.mutable_time_limit()->set_min(61);
  relaxed_rule.set_min_user_cout_limit(3);
  relaxed_rule.set_min_faction_count_limit(3);
  relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule faction_strict_rule;
  faction_strict_rule.set_id(300);
  faction_strict_rule.mutable_time_limit()->set_min(0);
  faction_strict_rule.mutable_time_limit()->set_max(60);
  faction_strict_rule.set_min_user_cout_limit(6);
  faction_strict_rule.set_min_faction_count_limit(2);
  auto* faction_rank_rule = faction_strict_rule.add_rules();
  faction_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  faction_rank_rule->add_values(5);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule faction_relaxed_rule;
  faction_relaxed_rule.set_id(301);
  faction_relaxed_rule.mutable_time_limit()->set_min(61);
  faction_relaxed_rule.set_min_user_cout_limit(6);
  faction_relaxed_rule.set_min_faction_count_limit(2);
  faction_relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule incompatible_unit_rule;
  incompatible_unit_rule.set_id(400);
  incompatible_unit_rule.mutable_time_limit()->set_min(0);
  incompatible_unit_rule.set_min_user_cout_limit(4);
  incompatible_unit_rule.set_min_faction_count_limit(2);
  incompatible_unit_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule dynamic_ready_strict_rule;
  dynamic_ready_strict_rule.set_id(500);
  dynamic_ready_strict_rule.mutable_time_limit()->set_min(0);
  dynamic_ready_strict_rule.mutable_time_limit()->set_max(60);
  dynamic_ready_strict_rule.set_min_user_cout_limit(3);
  dynamic_ready_strict_rule.set_min_faction_count_limit(2);
  auto* dynamic_ready_rank_rule = dynamic_ready_strict_rule.add_rules();
  dynamic_ready_rank_rule->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF);
  dynamic_ready_rank_rule->add_values(5);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule dynamic_ready_relaxed_rule;
  dynamic_ready_relaxed_rule.set_id(501);
  dynamic_ready_relaxed_rule.mutable_time_limit()->set_min(61);
  dynamic_ready_relaxed_rule.set_min_user_cout_limit(2);
  dynamic_ready_relaxed_rule.set_min_faction_count_limit(2);
  dynamic_ready_relaxed_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule partial_template_rule;
  partial_template_rule.set_id(600);
  partial_template_rule.mutable_time_limit()->set_min(0);
  partial_template_rule.set_min_user_cout_limit(3);
  partial_template_rule.set_min_faction_count_limit(3);
  partial_template_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule minimum_faction_rule;
  minimum_faction_rule.set_id(900);
  minimum_faction_rule.mutable_time_limit()->set_min(0);
  minimum_faction_rule.set_min_user_cout_limit(2);
  minimum_faction_rule.set_min_faction_count_limit(2);
  minimum_faction_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRule mixed_template_rule;
  mixed_template_rule.set_id(1000);
  mixed_template_rule.mutable_time_limit()->set_min(0);
  mixed_template_rule.set_min_user_cout_limit(5);
  mixed_template_rule.set_min_faction_count_limit(2);
  mixed_template_rule.add_rules()->set_type(PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_NONE);
  auto balanced_strict_rule = faction_strict_rule;
  balanced_strict_rule.set_id(700);
  auto balanced_relaxed_rule = faction_relaxed_rule;
  balanced_relaxed_rule.set_id(701);
  if (relaxed_balanced_windows) {
    balanced_relaxed_rule.set_min_user_cout_limit(4);
  }
  auto downgrade_strict_rule = balanced_strict_rule;
  downgrade_strict_rule.set_id(2000);
  downgrade_strict_rule.mutable_time_limit()->set_max(19);
  auto downgrade_middle_rule = balanced_relaxed_rule;
  downgrade_middle_rule.set_id(2001);
  downgrade_middle_rule.mutable_time_limit()->set_min(20);
  downgrade_middle_rule.mutable_time_limit()->set_max(59);
  downgrade_middle_rule.set_min_user_cout_limit(4);
  auto downgrade_last_rule = downgrade_middle_rule;
  downgrade_last_rule.set_id(2002);
  downgrade_last_rule.mutable_time_limit()->set_min(60);
  downgrade_last_rule.mutable_time_limit()->set_max(0);
  downgrade_last_rule.set_min_user_cout_limit(2);
  resource.set_file(
      "matching_rule.bytes",
      make_table_bytes({rule, strict_rule, relaxed_rule, faction_strict_rule, faction_relaxed_rule,
                        incompatible_unit_rule, dynamic_ready_strict_rule, dynamic_ready_relaxed_rule,
                        partial_template_rule, minimum_faction_rule, mixed_template_rule, balanced_strict_rule,
                        balanced_relaxed_rule, downgrade_strict_rule, downgrade_middle_rule, downgrade_last_rule}));

  resource.set_file("const.bytes", make_empty_table_bytes());
  resource.set_file("dtmq_channel_type.bytes", make_empty_table_bytes());
  resource.set_file("item_type.bytes", make_empty_table_bytes());
  resource.set_file(
      "level.bytes",
      make_table_bytes({make_level(101, 1, 1), make_level(201, 1, 2), make_level(202, 1, 2), make_level(203, 1, 2),
                        make_level(207, 1, 2), make_level(208, 2, 2), make_level(301, 1, 3), make_level(401, 1, 4),
                        make_level(501, 1, 5), make_level(601, 1, 6), make_level(701, 1, 7), make_level(1001, 1, 10),
                        make_level(2001, 1, 20)}));
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
  return result;
}

bool add_unit(matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& data) {
  return room.add_unit(std::make_shared<matching_unit>(data));
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
  protobuf_copy_message(*result.mutable_operator_user(), result.unit().users(0).user_key());
  auto* route = result.add_subscriber_routes();
  protobuf_copy_message(*route->mutable_user_key(), result.operator_user());
  route->set_server_id(0x160001);
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
  protobuf_copy_message(*result.mutable_operator_user(), result.unit().users(0).user_key());
  uint64_t server_id = 0x160001;
  for (const auto& user : result.unit().users()) {
    auto* route = result.add_subscriber_routes();
    protobuf_copy_message(*route->mutable_user_key(), user.user_key());
    route->set_server_id(server_id++);
  }
  return result;
}

void set_request_levels(PROJECT_NAMESPACE_ID::SSMatchingCreateReq& request,
                        std::initializer_list<int32_t> acceptable_level_ids) {
  request.mutable_unit()->clear_acceptable_level_ids();
  for (int32_t level_id : acceptable_level_ids) {
    request.mutable_unit()->add_acceptable_level_ids(level_id);
  }
}

PROJECT_NAMESPACE_ID::SSMatchingCheckReq make_heartbeat_request(uint64_t unit_id,
                                                                const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                                                int64_t acknowledge_event_id = 0,
                                                                uint64_t subscriber_server_id = 0x160001) {
  PROJECT_NAMESPACE_ID::SSMatchingCheckReq result;
  result.set_unit_id(unit_id);
  result.set_subscriber_server_id(subscriber_server_id);
  auto* heartbeat_user = result.mutable_heartbeat_data();
  protobuf_copy_message(*heartbeat_user->mutable_user_key(), user_key);
  heartbeat_user->set_acknowledge_event_id(acknowledge_event_id);
  return result;
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

  auto unit = make_unit(1, 10001, 10);
  matching_unit runtime_unit{unit};
  rpc::context ctx{rpc::context::create_without_task()};
  PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute route;
  protobuf_copy_message(*route.mutable_user_key(), unit.users(0).user_key());
  route.set_server_id(kLobbyServerId);
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute> routes;
  protobuf_copy_message(*routes.Add(), route);
  const bool subscribed = runtime_unit.initialize_subscribers(ctx, routes);
  CASE_EXPECT_TRUE(subscribed);
  if (!subscribed) {
    runtime.stop();
    return;
  }
  runtime.transport().clear_history();

  atframework::testing::transport_send_behavior failed_send;
  failed_send.immediate_error = -12345;
  auto failed_send_rule = runtime.transport().add_rule(kLobbyServerId, -1, failed_send);

  runtime_unit.publish(ctx, PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CREATED);

  auto subscriber_route = runtime_unit.get_subscriber_route(unit.users(0).user_key());
  CASE_EXPECT_TRUE(subscriber_route.has_value());
  if (subscriber_route.has_value()) {
    CASE_EXPECT_EQ(kLobbyServerId, subscriber_route->server_id);
    CASE_EXPECT_EQ(0, subscriber_route->acknowledge_event_id);
  }

  failed_send_rule.reset();
  atfw::util::time::time_utility::set_global_now_offset(atfw::util::time::time_utility::get_global_now_offset() +
                                                        std::chrono::seconds{2});
  CASE_EXPECT_EQ(1, runtime.transport().outbound_count_to(kLobbyServerId));
  PROJECT_NAMESPACE_ID::DMatchingUserHeartbeat heartbeat;
  protobuf_copy_message(*heartbeat.mutable_user_key(), unit.users(0).user_key());
  heartbeat.set_acknowledge_event_id(0);
  CASE_EXPECT_TRUE(runtime_unit.heartbeat(ctx, kLobbyServerId, heartbeat));
  CASE_EXPECT_EQ(2, runtime.transport().outbound_count_to(kLobbyServerId));
  heartbeat.set_acknowledge_event_id(1);
  CASE_EXPECT_TRUE(runtime_unit.heartbeat(ctx, kLobbyServerId, heartbeat));
  subscriber_route = runtime_unit.get_subscriber_route(unit.users(0).user_key());
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

  atfw::util::time::time_utility::reset_global_now_offset();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_wal, routes_unit_members_to_their_own_lobbysvr) {
  atframework::testing::runtime runtime;
  if (!start_wal_runtime(runtime)) {
    return;
  }

  constexpr uint64_t kFirstLobby = 0x160011;
  constexpr uint64_t kSecondLobby = 0x160012;
  for (uint64_t server_id : {kFirstLobby, kSecondLobby}) {
    atfw::testing::mock_node node;
    node.set_id(server_id)
        .set_name("matching-unit-route-test-" + std::to_string(server_id))
        .set_type_id(4097)
        .set_zone_id(1);
    CASE_EXPECT_TRUE(!!runtime.discovery().add_node(node));
  }

  auto unit_data = make_party_unit(10, 20001, 2, 10, true);
  matching_unit unit{unit_data};
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingSubscriberRoute> routes;
  auto* route = routes.Add();
  protobuf_copy_message(*route->mutable_user_key(), unit_data.users(0).user_key());
  route->set_server_id(kFirstLobby);
  rpc::context ctx{rpc::context::create_without_task()};
  CASE_EXPECT_TRUE(unit.initialize_subscribers(ctx, routes));
  runtime.transport().clear_history();
  unit.publish(ctx, PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_EVENT_TYPE_CREATED);
  CASE_EXPECT_EQ(1, runtime.transport().outbound_count_to(kFirstLobby));
  CASE_EXPECT_EQ(0, runtime.transport().outbound_count_to(kSecondLobby));

  PROJECT_NAMESPACE_ID::DMatchingUserHeartbeat second_heartbeat;
  protobuf_copy_message(*second_heartbeat.mutable_user_key(), unit_data.users(1).user_key());
  second_heartbeat.set_acknowledge_event_id(0);
  CASE_EXPECT_TRUE(unit.heartbeat(ctx, kSecondLobby, second_heartbeat));
  auto second_route = unit.get_subscriber_route(unit_data.users(1).user_key());
  CASE_EXPECT_TRUE(second_route.has_value());
  if (second_route.has_value()) {
    CASE_EXPECT_EQ(kSecondLobby, second_route->server_id);
    CASE_EXPECT_EQ(0, second_route->acknowledge_event_id);
  }
  CASE_EXPECT_EQ(1, runtime.transport().outbound_count_to(kSecondLobby));

  PROJECT_NAMESPACE_ID::DMatchingUserHeartbeat outsider_heartbeat;
  outsider_heartbeat.mutable_user_key()->set_user_id(30001);
  outsider_heartbeat.mutable_user_key()->set_zone_id(1);
  outsider_heartbeat.set_acknowledge_event_id(0);
  CASE_EXPECT_FALSE(unit.heartbeat(ctx, kSecondLobby, outsider_heartbeat));
  CASE_EXPECT_FALSE(unit.get_subscriber_route(outsider_heartbeat.user_key()).has_value());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, same_capacity_rule_supports_exclusive_one_vs_one_and_three_vs_three) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto rule = *excel::get_ExcelMatchingRule_by_id(300);
  rule.set_faction_add_rule(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM);
  rule.set_min_user_cout_limit(2);
  runtime.resource().set_file("matching_rule.bytes", make_table_bytes(rule));
  runtime.resource().set_version("same-capacity-exclusive");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(8);
  for (int32_t size : {1, 3}) {
    matching_room room{"same-capacity", scope, 301, 100, 300};
    auto first = make_party_unit(1, 10001, size, 10, false);
    auto created = matching_logic::check_unit_can_create_room(scope, first, 100, size);
    CASE_EXPECT_TRUE(created.evaluation.can_join());
    if (!created.evaluation.can_join()) {
      continue;
    }
    CASE_EXPECT_TRUE(add_unit(room, first));
    CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
    CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 100, size).ready());
    auto different = make_party_unit(2, 10011, size == 1 ? 3 : 1, 10, false);
    auto rejected = matching_logic::check_unit_can_join(room, different, 100, 4);
    CASE_EXPECT_FALSE(rejected.evaluation.can_join());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND, rejected.evaluation.result());
    auto second = make_party_unit(3, 10021, size, 10, false);
    auto joined = matching_logic::check_unit_can_join(room, second, 100, size * 2);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (joined.evaluation.can_join()) {
      CASE_EXPECT_TRUE(add_unit(room, second));
      CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
      CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
      CASE_EXPECT_TRUE(matching_logic::check_room_ready(room, 100, size * 2).ready());
      room.begin_confirmation(115);
      CASE_EXPECT_TRUE(room.finalize_faction_ids());
      CASE_EXPECT_NE(0, room.get_unit_faction_id(1));
      CASE_EXPECT_NE(room.get_unit_faction_id(1), room.get_unit_faction_id(3));
    }
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, same_capacity_compares_fill_target_not_incoming_unit_size) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto rule = *excel::get_ExcelMatchingRule_by_id(300);
  rule.set_faction_add_rule(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM);
  rule.set_min_user_cout_limit(2);
  runtime.resource().set_file("matching_rule.bytes", make_table_bytes(rule));
  runtime.resource().set_version("same-fill-target");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  for (int32_t pool_id : {8, 14}) {
    // 两种池策略都必须补满，不能因为已满足最小总人数就提前开局。
    auto scope = make_room().get_scope();
    scope.set_matching_pool_id(pool_id);
    if (pool_id == 14) {
      auto balanced_rule = rule;
      balanced_rule.set_id(700);
      runtime.resource().set_file("matching_rule.bytes", make_table_bytes({rule, balanced_rule}));
      runtime.resource().set_version("same-fill-balanced");
      CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
    }
    matching_room room{"fill-target", scope, 301, 100, 300};
    for (uint64_t unit_id = 1; unit_id <= 6; ++unit_id) {
      auto unit = make_party_unit(unit_id, 10100 + unit_id, 1, 10, true);
      auto joined = unit_id == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 6)
                                 : matching_logic::check_unit_can_join(room, unit, 100, 6);
      CASE_EXPECT_TRUE(joined.evaluation.can_join());
      if (!joined.evaluation.can_join()) {
        break;
      }
      CASE_EXPECT_TRUE(add_unit(room, unit));
      CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
      CASE_EXPECT_EQ(unit_id == 6, matching_logic::check_room_ready(room, 100, 6).ready());
      if (unit_id == 1) {
        const auto exclusive = make_party_unit(9, 10109, 1, 10, false);
        CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, exclusive, 100, 6).evaluation.can_join());
      }
      for (const auto& assignment : room.get_faction_assignments()) {
        CASE_EXPECT_EQ(3, assignment.user_capacity());
      }
    }
    CASE_EXPECT_EQ(6, room.get_user_count());
    CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, none_rule_allows_three_unequal_full_factions) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(7);
  matching_room room{"unequal-factions", scope, 301, 100, 300};
  for (int32_t size : {1, 2, 3}) {
    auto unit = make_party_unit(static_cast<uint64_t>(size), static_cast<uint64_t>(10200 + size * 10), size, 10, false);
    auto joined = size == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 6)
                            : matching_logic::check_unit_can_join(room, unit, 100, 6);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (!joined.evaluation.can_join()) {
      break;
    }
    CASE_EXPECT_TRUE(add_unit(room, unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
    CASE_EXPECT_EQ(size == 3, matching_logic::check_room_ready(room, 100, 6).ready());
  }
  CASE_EXPECT_EQ(6, room.get_user_count());
  CASE_EXPECT_EQ(3, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, faction_capacity_and_numeric_limits_must_match_the_same_rule) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto same_rule = *excel::get_ExcelMatchingRule_by_id(100);
  same_rule.set_faction_add_rule(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM);
  auto unrestricted_rule = same_rule;
  unrestricted_rule.set_id(101);
  unrestricted_rule.set_faction_add_rule(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_NONE);
  unrestricted_rule.mutable_rules(0)->set_values(0, 2);
  PROJECT_NAMESPACE_ID::config::ExcelMatchingRuleGroup group;
  group.set_group_id(10);
  group.set_global_user_upper(100);
  group.add_pool_rules(100);
  group.add_pool_rules(101);
  runtime.resource().set_file("matching_rule_group.bytes", make_table_bytes(group));
  runtime.resource().set_file("matching_rule.bytes", make_table_bytes({same_rule, unrestricted_rule}));
  runtime.resource().set_version("capacity-and-numeric");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  auto room = make_room();
  auto stored = make_party_unit(1, 10301, 1, 10, false);
  auto created = matching_logic::check_unit_can_create_room(room.get_scope(), stored, 100, 1);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, stored));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
  // SAME 接受段位差 4 但拒绝容量 2；NONE 接受容量 2 但拒绝段位差 4，不能拼接两条规则。
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(2, 10302, 14), 100, 2).evaluation.can_join());
  auto incoming = make_unit(2, 10302, 11);
  CASE_EXPECT_TRUE(matching_logic::check_unit_can_join(room, incoming, 100, 2).evaluation.can_join());
  protobuf_copy_message(*incoming.add_ban_users(), stored.users(0).user_key());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, incoming, 100, 2).evaluation.can_join());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, complete_faction_migration_obeys_same_capacity_rule) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto rule = *excel::get_ExcelMatchingRule_by_id(300);
  rule.set_faction_add_rule(PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM);
  runtime.resource().set_file("matching_rule.bytes", make_table_bytes(rule));
  runtime.resource().set_version("same-capacity-migration");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(3);
  matching_room room{"same-capacity-target", scope, 301, 100, 300};
  auto first = make_party_unit(1, 10401, 2, 10, false);
  auto created = matching_logic::check_unit_can_create_room(scope, first, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, first));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
  auto different = make_party_unit(2, 10411, 3, 10, false);
  CASE_EXPECT_FALSE(matching_logic::check_faction_can_join(room, {&different}, 3, 100, 5).evaluation.can_join());
  auto same = make_party_unit(3, 10421, 2, 10, false);
  auto accepted = matching_logic::check_faction_can_join(room, {&same}, 2, 100, 4);
  CASE_EXPECT_TRUE(accepted.evaluation.can_join());
  CASE_EXPECT_EQ(2, accepted.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(2, room.get_user_count());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rejects_unknown_faction_add_rule_after_reload) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  auto unit = make_unit(1, 10501, 10);
  CASE_EXPECT_TRUE(matching_logic::check_unit_can_create_room(scope, unit, 100, 1).evaluation.can_join());
  auto rule = *excel::get_ExcelMatchingRule_by_id(100);
  rule.set_faction_add_rule(static_cast<PROJECT_NAMESPACE_ID::config::EnMatchingFactionAddRule>(99));
  runtime.resource().set_file("matching_rule.bytes", make_table_bytes(rule));
  runtime.resource().set_version("unknown-add-rule");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_create_room(scope, unit, 100, 1).evaluation.can_join());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, validates_units_against_pool_contract) {
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

CASE_TEST(matchsvr_matching_manager, validates_create_operator_as_unit_member) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_party_create_request(9, 19001, 2, 10);
  protobuf_copy_message(*request.mutable_operator_user(), request.unit().users(1).user_key());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(2, manager->get_total_matching_user_count());

  manager->clear();
  request = make_party_create_request(10, 19101, 2, 10);
  request.mutable_operator_user()->set_user_id(19999);
  request.mutable_operator_user()->set_zone_id(1);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                 manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, applies_capacity_rank_limits_without_force_count_filter) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto room = make_room();
  CASE_EXPECT_TRUE(add_unit(room, make_unit(1, 10001, 10, 1)));
  set_single_faction(room, 1, 1);

  auto accepted = matching_logic::check_unit_can_join(room, make_unit(2, 10002, 14, 2), 110, 2);
  CASE_EXPECT_TRUE(accepted.evaluation.can_join());
  CASE_EXPECT_EQ(0, accepted.evaluation.result());

  auto rank_rejected = matching_logic::check_unit_can_join(room, make_unit(3, 10003, 16, 2), 110, 2);
  CASE_EXPECT_FALSE(rank_rejected.evaluation.can_join());
  auto same_force = matching_logic::check_unit_can_join(room, make_unit(4, 10004, 12, 1), 110, 2);
  CASE_EXPECT_TRUE(same_force.evaluation.can_join());

  auto oversized = make_unit(5, 10005, 12, 2);
  for (uint64_t user_id = 10006; user_id <= 10008; ++user_id) {
    oversized.add_users()->mutable_user_key()->set_user_id(user_id);
  }
  auto room_full = matching_logic::check_unit_can_join(room, oversized, 110, 2);
  CASE_EXPECT_FALSE(room_full.evaluation.can_join());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT, room_full.evaluation.result());

  CASE_EXPECT_TRUE(add_unit(room, make_unit(2, 10002, 14, 2)));
  CASE_EXPECT_TRUE(room.set_faction_assignments(accepted.evaluation.faction_assignments()));
  auto ready = matching_logic::check_room_ready(room, 110, 2);
  // 新建的可补位 faction 容量为 2，只有 1 人时尚未补满。
  CASE_EXPECT_FALSE(ready.ready());
  auto filler = make_unit(6, 10009, 12, 2);
  auto filled = matching_logic::check_unit_can_join(room, filler, 110, 3);
  CASE_EXPECT_TRUE(filled.evaluation.can_join());
  if (!filled.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_TRUE(add_unit(room, filler));
  CASE_EXPECT_TRUE(room.set_faction_assignments(filled.evaluation.faction_assignments()));
  ready = matching_logic::check_room_ready(room, 110, 3);
  CASE_EXPECT_TRUE(ready.ready());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, uses_pool_capacity_without_template_reachability) {
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
  auto full_faction =
      matching_logic::check_unit_can_create_room(scope, make_party_unit(23, 26005, 3, 10, true), 100, 3);
  CASE_EXPECT_TRUE(full_faction.evaluation.can_join());
  CASE_EXPECT_EQ(3, full_faction.evaluation.faction_assignments(0).user_capacity());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, priority_factions_require_full_capacity) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(4);
  auto party = make_party_unit(24, 26010, 2, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, party, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  CASE_EXPECT_EQ(0, created.evaluation.result());
  if (created.evaluation.can_join()) {
    matching_room room{"priority-incomplete", scope, 401, 100, 300};
    CASE_EXPECT_EQ(3, created.evaluation.faction_assignments(0).user_capacity());
    CASE_EXPECT_TRUE(add_unit(room, party));
    CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
    auto second = make_party_unit(25, 26020, 2, 10, true);
    auto joined = matching_logic::check_unit_can_join(room, second, 100, 4);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (joined.evaluation.can_join()) {
      CASE_EXPECT_TRUE(add_unit(room, second));
      CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
      CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
      CASE_EXPECT_EQ(4, room.get_user_count());
      // 当前人数满足两项最小门槛，但两个容量 3 的 faction 都没有补满。
      const auto ready = matching_logic::check_room_ready(room, 100, 4);
      CASE_EXPECT_EQ(0, ready.result());
      CASE_EXPECT_FALSE(ready.ready());
    }
  }

  // 两个独占队伍的固定容量均为 2，同样四人时已满员，可以成局。
  party.set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  created = matching_logic::check_unit_can_create_room(scope, party, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (created.evaluation.can_join()) {
    CASE_EXPECT_EQ(2, created.evaluation.faction_assignments(0).user_capacity());
    matching_room room{"priority-exclusive", scope, 401, 100, 300};
    CASE_EXPECT_TRUE(add_unit(room, party));
    CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
    auto second = make_party_unit(25, 26020, 2, 10, false);
    auto joined = matching_logic::check_unit_can_join(room, second, 100, 4);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (joined.evaluation.can_join()) {
      CASE_EXPECT_TRUE(add_unit(room, second));
      CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
      CASE_EXPECT_TRUE(matching_logic::check_room_ready(room, 100, 4).ready());
    }
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, keeps_room_searching_until_user_threshold_is_reached) {
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
  CASE_EXPECT_TRUE(add_unit(room, first_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  auto ready = matching_logic::check_room_ready(room, 100, 1);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());

  auto second_unit = make_unit(25, 27002, 10);
  auto joined = matching_logic::check_unit_can_join(room, second_unit, 162, 2);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, second_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  ready = matching_logic::check_room_ready(room, 162, 2);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_TRUE(ready.ready());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, accepts_full_factions_after_readiness_limits) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(6);

  auto first_unit = make_unit(26, 28001, 10);
  auto created = matching_logic::check_unit_can_create_room(scope, first_unit, 100, 1);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (!created.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }

  matching_room room{"partial-template", scope, 601, 100, 500};
  CASE_EXPECT_TRUE(add_unit(room, first_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  auto ready = matching_logic::check_room_ready(room, 100, 1);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());

  auto second_unit = make_unit(27, 28002, 10);
  auto joined = matching_logic::check_unit_can_join(room, second_unit, 100, 2);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, second_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  ready = matching_logic::check_room_ready(room, 100, 2);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());

  auto third_unit = make_unit(28, 28003, 10);
  joined = matching_logic::check_unit_can_join(room, third_unit, 100, 3);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, third_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  ready = matching_logic::check_room_ready(room, 100, 3);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_TRUE(ready.ready());

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
  CASE_EXPECT_TRUE(add_unit(room, stored));
  set_single_faction(room, 1, 1);
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(2, 10002, 10), 110, 2).evaluation.can_join());
  CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, make_unit(3, 10003, 10), 161, 2).evaluation.can_join());

  PROJECT_NAMESPACE_ID::DMatchingScope other_scope = room.get_scope();
  other_scope.set_region("us");
  matching_room other_room{"other-region", other_scope, 1, 100, 300};
  CASE_EXPECT_TRUE(add_unit(other_room, make_unit(4, 10004, 10)));
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
  CASE_EXPECT_TRUE(add_unit(room, stored));
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

CASE_TEST(matchsvr_matching_logic, accepts_preserved_full_factions_without_reassignment) {
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
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 10001, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(2, 10003, 1, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(3, 10004, 3, 10, false)));
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

  matching_room solo_room{"preserved-solo-factions", scope, 301, 100, 300};
  assignments.Clear();
  for (uint64_t unit_id = 1; unit_id <= 6; ++unit_id) {
    CASE_EXPECT_TRUE(add_unit(solo_room, make_party_unit(unit_id, 20000 + unit_id, 1, 10, true)));
    auto* faction = unit_id <= 3 ? (assignments.empty() ? assignments.Add() : assignments.Mutable(0))
                                 : (assignments.size() == 1 ? assignments.Add() : assignments.Mutable(1));
    faction->set_user_capacity(3);
    faction->add_unit_ids(unit_id);
  }
  CASE_EXPECT_TRUE(solo_room.set_faction_assignments(assignments));
  result = matching_logic::check_room_ready(solo_room, 110, 6);
  CASE_EXPECT_TRUE(result.ready());
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
  CASE_EXPECT_TRUE(add_unit(single_pending_room, make_party_unit(1, 21001, 1, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(exclusive_room, make_party_unit(2, 22001, 2, 10, false)));
  faction = assignments.Add();
  faction->set_user_capacity(2);
  faction->add_unit_ids(2);
  CASE_EXPECT_TRUE(exclusive_room.set_faction_assignments(assignments));
  result = matching_logic::check_room_ready(exclusive_room, 110, 2);
  CASE_EXPECT_EQ(0, result.result());
  CASE_EXPECT_FALSE(result.ready());

  matching_room multiple_pending_room{"multiple-pending", scope, 301, 100, 300};
  assignments.Clear();
  CASE_EXPECT_TRUE(add_unit(multiple_pending_room, make_party_unit(3, 22003, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(multiple_pending_room, make_party_unit(4, 22005, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(multiple_pending_room, make_party_unit(5, 22007, 1, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 23001, 2, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 23101, 2, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 30001, 1, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(2, 30002, 1, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(create_faction_room, make_party_unit(11, 31001, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(create_faction_room, make_party_unit(13, 31005, 1, 10, true)));
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

CASE_TEST(matchsvr_matching_logic, fills_factions_larger_than_the_unit_limit) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  for (int32_t pool_id : {13, 14}) {
    PROJECT_NAMESPACE_ID::DMatchingScope scope;
    scope.set_level_type(1);
    scope.set_region("cn");
    scope.set_battle_version("1.0");
    scope.set_matching_pool_id(pool_id);
    CASE_EXPECT_EQ(3, excel::get_matching_pool_faction_capacity(pool_id));
    const auto oversized_unit = make_party_unit(99, 39099, 3, 10, true);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                   matching_logic::validate_unit(scope.matching_pool_id(), oversized_unit));
    CASE_EXPECT_FALSE(matching_logic::check_unit_can_create_room(scope, oversized_unit, 100, 3).evaluation.can_join());

    matching_room room{"separate-unit-and-faction-limits", scope, 301, 100, 300};
    // 两个 2 人 Unit 分别占位，两个单人补满两个容量为 3 的 faction。
    for (uint64_t unit_id = 1; unit_id <= 4; ++unit_id) {
      auto unit = make_party_unit(unit_id, 39000 + unit_id * 10, unit_id <= 2 ? 2 : 1, 10, true);
      CASE_EXPECT_EQ(0, matching_logic::validate_unit(scope.matching_pool_id(), unit));
      auto joined = unit_id == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 6)
                                 : matching_logic::check_unit_can_join(room, unit, 100, 6);
      CASE_EXPECT_TRUE(joined.evaluation.can_join());
      if (!joined.evaluation.can_join()) {
        CASE_EXPECT_EQ(0, runtime.stop());
        return;
      }
      CASE_EXPECT_TRUE(add_unit(room, unit));
      CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
      if (unit_id < 4) {
        CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 100, 6).ready());
      }
    }
    CASE_EXPECT_FALSE(matching_logic::check_unit_can_join(room, oversized_unit, 100, 9).evaluation.can_join());
    CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
    for (const auto& assignment : room.get_faction_assignments()) {
      CASE_EXPECT_EQ(3, assignment.user_capacity());
      CASE_EXPECT_EQ(3, assignment.assigned_user_count());
    }
    const auto ready = matching_logic::check_room_ready(room, 100, 6);
    CASE_EXPECT_TRUE(ready.ready());
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, migrates_complete_faction_larger_than_the_unit_limit) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(13);
  matching_room target{"separate-faction-migration-limit", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(target, make_party_unit(10, 39101, 2, 10, true)));
  set_single_faction(target, 10, 3);
  std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit> source_units;
  source_units.emplace_back(make_party_unit(11, 39201, 2, 10, true));
  source_units.emplace_back(make_party_unit(12, 39203, 1, 10, true));
  const auto joined = matching_logic::check_faction_can_join(target, make_unit_view(source_units), 3, 110, 5);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
    const auto& migrated = joined.evaluation.faction_assignments(1);
    CASE_EXPECT_EQ(3, migrated.user_capacity());
    CASE_EXPECT_EQ(3, migrated.assigned_user_count());
    CASE_EXPECT_EQ(2, migrated.unit_ids_size());
    CASE_EXPECT_EQ(11, migrated.unit_ids(0));
    CASE_EXPECT_EQ(12, migrated.unit_ids(1));
    for (const auto& unit : source_units) {
      CASE_EXPECT_TRUE(add_unit(target, unit));
    }
    CASE_EXPECT_TRUE(target.set_faction_assignments(joined.evaluation.faction_assignments()));
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, enforces_faction_limit_independently_of_unit_limit) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(15);
  const auto valid_unit = make_party_unit(1, 39301, 2, 10, true);
  const auto created = matching_logic::check_unit_can_create_room(scope, valid_unit, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (created.evaluation.can_join()) {
    CASE_EXPECT_EQ(2, created.evaluation.faction_assignments(0).user_capacity());
  }
  for (bool allow_fill : {false, true}) {
    const auto oversized_unit = make_party_unit(2, 39311, 3, 10, allow_fill);
    CASE_EXPECT_EQ(0, matching_logic::validate_unit(scope.matching_pool_id(), oversized_unit));
    CASE_EXPECT_FALSE(matching_logic::check_unit_can_create_room(scope, oversized_unit, 100, 3).evaluation.can_join());
  }
  matching_room target{"reject-oversized-faction", scope, 401, 100, 300};
  CASE_EXPECT_TRUE(add_unit(target, valid_unit));
  set_single_faction(target, 1, 2);
  std::vector<PROJECT_NAMESPACE_ID::DMatchingUnit> source_units;
  source_units.emplace_back(make_party_unit(2, 39311, 3, 10, false));
  const auto rejected = matching_logic::check_faction_can_join(target, make_unit_view(source_units), 3, 100, 5);
  CASE_EXPECT_FALSE(rejected.evaluation.can_join());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL, rejected.evaluation.result());
  CASE_EXPECT_EQ(2, target.get_user_count());
  CASE_EXPECT_EQ(1, target.get_faction_assignments().size());

  // 不再通过模板校验池容量；正容量直接生效，漏配仍须拒绝。
  for (int32_t pool_id : {16, 19}) {
    scope.set_matching_pool_id(pool_id);
    const auto solo = make_party_unit(3, 39321, 1, 10, true);
    auto single = matching_logic::check_unit_can_create_room(scope, solo, 100, 1);
    CASE_EXPECT_TRUE(single.evaluation.can_join());
    CASE_EXPECT_EQ(1, excel::get_matching_pool_faction_capacity(pool_id));
  }
  scope.set_matching_pool_id(17);
  CASE_EXPECT_EQ(0, excel::get_matching_pool_faction_capacity(17));
  CASE_EXPECT_FALSE(
      matching_logic::check_unit_can_create_room(scope, make_unit(3, 39321, 10), 100, 1).evaluation.can_join());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, balances_factions_after_opening_the_pool_maximum) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(7);

  auto first_unit = make_party_unit(1, 31501, 1, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, first_unit, 100, 1);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  matching_room room{"balanced-factions", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, first_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  for (uint64_t unit_id = 2; unit_id <= 5; ++unit_id) {
    auto unit = make_party_unit(unit_id, 31500 + unit_id, 1, 10, true);
    auto joined = matching_logic::check_unit_can_join(room, unit, 100, static_cast<int32_t>(unit_id));
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (!joined.evaluation.can_join()) {
      CASE_EXPECT_EQ(0, runtime.stop());
      return;
    }
    CASE_EXPECT_TRUE(add_unit(room, unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

    const int expected_faction_count = unit_id < 3 ? static_cast<int>(unit_id) : 3;
    CASE_EXPECT_EQ(expected_faction_count, room.get_faction_assignments().size());
  }

  CASE_EXPECT_EQ(2, room.get_faction_assignments().Get(0).assigned_user_count());
  CASE_EXPECT_EQ(2, room.get_faction_assignments().Get(1).assigned_user_count());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().Get(2).assigned_user_count());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, balanced_factions_cannot_start_before_full_capacity) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(11);

  matching_room room{"balanced-full-capacity", scope, 401, 100, 300};
  auto first_unit = make_party_unit(1, 31511, 2, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, first_unit, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (!created.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_EQ(3, created.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_TRUE(add_unit(room, first_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
  auto second_unit = make_party_unit(2, 31513, 1, 10, true);
  auto second_joined = matching_logic::check_unit_can_join(room, second_unit, 100, 3);
  CASE_EXPECT_TRUE(second_joined.evaluation.can_join());
  if (!second_joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_EQ(3, second_joined.evaluation.faction_assignments(1).user_capacity());
  CASE_EXPECT_TRUE(add_unit(room, second_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(second_joined.evaluation.faction_assignments()));
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 100, 3).ready());

  auto incoming_unit = make_party_unit(3, 31514, 1, 10, true);
  auto joined = matching_logic::check_unit_can_join(room, incoming_unit, 100, 4);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
    CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(0).assigned_user_count());
    CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(1).assigned_user_count());
    CASE_EXPECT_TRUE(add_unit(room, incoming_unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
    const auto ready = matching_logic::check_room_ready(room, 100, 4);
    CASE_EXPECT_FALSE(ready.ready());
  }

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, lower_user_threshold_does_not_bypass_full_factions) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(20);
  matching_room room{"balanced-downgrade-fill", scope, 301, 100, 300};
  for (uint64_t unit_id = 1; unit_id <= 2; ++unit_id) {
    auto unit = make_party_unit(unit_id, 39500 + unit_id * 10, unit_id == 1 ? 2 : 1, 10, true);
    auto joined = unit_id == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 3)
                               : matching_logic::check_unit_can_join(room, unit, 100, 3);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (!joined.evaluation.can_join()) {
      CASE_EXPECT_EQ(0, runtime.stop());
      return;
    }
    CASE_EXPECT_TRUE(add_unit(room, unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
  }
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 119, 3).ready());
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 120, 3).ready());
  const auto incoming = make_party_unit(3, 39530, 1, 10, true);
  const auto joined = matching_logic::check_unit_can_join(room, incoming, 120, 4);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (joined.evaluation.can_join()) {
    CASE_EXPECT_TRUE(add_unit(room, incoming));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
    CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
    for (const auto& assignment : room.get_faction_assignments()) {
      CASE_EXPECT_EQ(3, assignment.user_capacity());
      CASE_EXPECT_EQ(2, assignment.assigned_user_count());
    }
    CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 119, 4).ready());
    const auto ready = matching_logic::check_room_ready(room, 120, 4);
    CASE_EXPECT_FALSE(ready.ready());
    // 仅修改开局人数条件，不拆分最初的双人 Unit，也不改变其阵营归属。
    CASE_EXPECT_EQ(1, room.get_faction_assignments().Get(0).unit_ids_size());
    CASE_EXPECT_EQ(1, room.get_faction_assignments().Get(0).unit_ids(0));
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, balanced_downgrade_does_not_shrink_an_oversized_faction) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(20);
  matching_room room{"balanced-downgrade-preserve-members", scope, 301, 100, 300};
  for (uint64_t unit_id = 1; unit_id <= 2; ++unit_id) {
    auto unit = make_party_unit(unit_id, 39600 + unit_id * 10, unit_id == 1 ? 3 : 1, 10, true);
    auto joined = unit_id == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 4)
                               : matching_logic::check_unit_can_join(room, unit, 100, 4);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (!joined.evaluation.can_join()) {
      CASE_EXPECT_EQ(0, runtime.stop());
      return;
    }
    CASE_EXPECT_TRUE(add_unit(room, unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
  }
  // [3,1] 既不是 2v2，也不是 1v1；不能把 3 人 Unit 拆出一个玩家。
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 120, 4).ready());
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 160, 4).ready());
  const auto first_before = room.get_faction_assignments().Get(0).SerializeAsString();
  const auto second_before = room.get_faction_assignments().Get(1).SerializeAsString();
  CASE_EXPECT_FALSE(
      matching_logic::check_unit_can_join(room, make_party_unit(9, 39690, 3, 10, true), 160, 7).evaluation.can_join());
  CASE_EXPECT_EQ(2, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(first_before, room.get_faction_assignments().Get(0).SerializeAsString());
  CASE_EXPECT_EQ(second_before, room.get_faction_assignments().Get(1).SerializeAsString());
  // 降低开局人数门槛不会改变已有容量，原房间仍须继续补满。
  const auto incoming = make_party_unit(3, 39630, 2, 10, true);
  const auto joined = matching_logic::check_unit_can_join(room, incoming, 160, 6);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (joined.evaluation.can_join()) {
    CASE_EXPECT_TRUE(add_unit(room, incoming));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
    const auto ready = matching_logic::check_room_ready(room, 160, 6);
    CASE_EXPECT_TRUE(ready.ready());
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, exclusive_solos_start_after_user_threshold_downgrade) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(20);
  matching_room room{"balanced-downgrade-one-vs-one", scope, 301, 100, 300};
  for (uint64_t unit_id = 1; unit_id <= 2; ++unit_id) {
    auto unit = make_party_unit(unit_id, 39700 + unit_id, 1, 10, false);
    auto joined = unit_id == 1 ? matching_logic::check_unit_can_create_room(scope, unit, 100, 2)
                               : matching_logic::check_unit_can_join(room, unit, 100, 2);
    CASE_EXPECT_TRUE(joined.evaluation.can_join());
    if (!joined.evaluation.can_join()) {
      CASE_EXPECT_EQ(0, runtime.stop());
      return;
    }
    CASE_EXPECT_TRUE(add_unit(room, unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
  }
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 119, 2).ready());
  CASE_EXPECT_FALSE(matching_logic::check_room_ready(room, 159, 2).ready());
  const auto ready = matching_logic::check_room_ready(room, 160, 2);
  CASE_EXPECT_TRUE(ready.ready());
  room.begin_confirmation(200);
  CASE_EXPECT_TRUE(room.finalize_faction_ids());
  CASE_EXPECT_NE(0, room.get_unit_faction_id(1));
  CASE_EXPECT_NE(room.get_unit_faction_id(1), room.get_unit_faction_id(2));
  for (const auto& assignment : room.get_faction_assignments()) {
    CASE_EXPECT_EQ(1, assignment.user_capacity());
    CASE_EXPECT_EQ(1, assignment.assigned_user_count());
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, validates_balanced_capacity_across_windows_and_reload) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(7);
  auto unit = make_party_unit(1, 31530, 1, 10, true);
  CASE_EXPECT_TRUE(matching_logic::check_unit_can_create_room(scope, unit, 100, 1).evaluation.can_join());

  // 仅放宽后续窗口的最小人数，不改变补位容量。
  seed_matching_tables(runtime.resource(), true);
  runtime.resource().set_version("balanced-relaxed-windows");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  auto relaxed = matching_logic::check_unit_can_create_room(scope, unit, 100, 1);
  CASE_EXPECT_TRUE(relaxed.evaluation.can_join());
  if (relaxed.evaluation.can_join()) {
    CASE_EXPECT_EQ(3, relaxed.evaluation.faction_assignments(0).user_capacity());
  }

  seed_matching_tables(runtime.resource());
  runtime.resource().set_version("balanced-consistent-windows");
  CASE_EXPECT_TRUE(runtime.resource().reload() >= 0);
  auto restored = matching_logic::check_unit_can_create_room(scope, unit, 100, 1);
  CASE_EXPECT_TRUE(restored.evaluation.can_join());
  if (restored.evaluation.can_join()) {
    CASE_EXPECT_EQ(3, restored.evaluation.faction_assignments(0).user_capacity());
  }
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, admits_none_rule_without_predicting_future_composition) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto scope = make_room().get_scope();
  scope.set_matching_pool_id(3);
  auto first = make_party_unit(1, 31540, 3, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, first, 100, 3);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (!created.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  matching_room room{"completion-user-limit", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, first));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));
  const auto before = room.get_faction_assignments().Get(0).SerializeAsString();

  // NONE 接受容量不同的独占阵营，只检查当前加入是否超过硬上限。
  const auto exclusive = matching_logic::check_unit_can_join(room, make_party_unit(2, 31543, 1, 10, false), 100, 4);
  CASE_EXPECT_TRUE(exclusive.evaluation.can_join());
  CASE_EXPECT_EQ(0, exclusive.evaluation.result());
  CASE_EXPECT_EQ(2, exclusive.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(1, exclusive.evaluation.faction_assignments(1).user_capacity());
  CASE_EXPECT_EQ(3, room.get_user_count());
  CASE_EXPECT_EQ(1, room.get_faction_assignments().size());
  CASE_EXPECT_EQ(before, room.get_faction_assignments().Get(0).SerializeAsString());

  // 不独占的单人 Unit 建立容量 3 的 faction，只需补两人，恰好达到上限。
  const auto accepted = matching_logic::check_unit_can_join(room, make_party_unit(2, 31543, 1, 10, true), 100, 4);
  CASE_EXPECT_TRUE(accepted.evaluation.can_join());
  CASE_EXPECT_EQ(2, accepted.evaluation.faction_assignments_size());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, matches_fixed_capacities_for_mixed_faction_sizes) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(10);

  auto fillable_unit = make_party_unit(1, 31521, 2, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, fillable_unit, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  if (!created.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }

  matching_room room{"mixed-template", scope, 1001, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, fillable_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  auto exclusive_unit = make_party_unit(2, 31523, 2, 10, false);
  auto joined = matching_logic::check_unit_can_join(room, exclusive_unit, 100, 4);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (!joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(1).user_capacity());
  CASE_EXPECT_TRUE(add_unit(room, exclusive_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  auto final_unit = make_party_unit(3, 31525, 1, 10, true);
  joined = matching_logic::check_unit_can_join(room, final_unit, 100, 5);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  if (joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(0).assigned_user_count());
    CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(1).assigned_user_count());
    CASE_EXPECT_TRUE(add_unit(room, final_unit));
    CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));
    const auto ready = matching_logic::check_room_ready(room, 100, 5);
    CASE_EXPECT_TRUE(ready.ready());
  }

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, requires_rule_minimum_faction_count_before_ready) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(9);

  auto first_unit = make_party_unit(1, 31601, 2, 10, true);
  auto created = matching_logic::check_unit_can_create_room(scope, first_unit, 100, 2);
  CASE_EXPECT_TRUE(created.evaluation.can_join());
  matching_room room{"minimum-factions", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, first_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(created.evaluation.faction_assignments()));

  auto ready = matching_logic::check_room_ready(room, 100, 2);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_FALSE(ready.ready());

  auto second_unit = make_party_unit(2, 31603, 2, 10, true);
  auto joined = matching_logic::check_unit_can_join(room, second_unit, 100, 4);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_TRUE(add_unit(room, second_unit));
  CASE_EXPECT_TRUE(room.set_faction_assignments(joined.evaluation.faction_assignments()));

  ready = matching_logic::check_room_ready(room, 100, 4);
  CASE_EXPECT_EQ(0, ready.result());
  CASE_EXPECT_TRUE(ready.ready());

  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_logic, rejects_new_faction_after_pool_faction_limit_is_reached) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(8);

  matching_room room{"fallback-faction", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 32001, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(2, 32003, 1, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(3, 32004, 1, 10, true)));
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

CASE_TEST(matchsvr_matching_logic, uses_pool_capacity_for_new_faction) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  PROJECT_NAMESPACE_ID::DMatchingScope scope;
  scope.set_level_type(1);
  scope.set_region("cn");
  scope.set_battle_version("1.0");
  scope.set_matching_pool_id(10);

  matching_room room{"fixed-capacity", scope, 301, 100, 300};
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 33001, 2, 10, false)));
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> initial_assignments;
  auto* faction = initial_assignments.Add();
  faction->set_user_capacity(2);
  faction->add_unit_ids(1);
  CASE_EXPECT_TRUE(room.set_faction_assignments(initial_assignments));

  const auto joined = matching_logic::check_unit_can_join(room, make_party_unit(3, 33003, 1, 10, true), 110, 3);
  CASE_EXPECT_TRUE(joined.evaluation.can_join());
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments_size());
  if (!joined.evaluation.can_join()) {
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_EQ(2, joined.evaluation.faction_assignments(0).user_capacity());
  CASE_EXPECT_EQ(3, joined.evaluation.faction_assignments(1).user_capacity());

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
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(1, 34001, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(2, 34003, 1, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(3, 34004, 2, 10, true)));
  CASE_EXPECT_TRUE(add_unit(room, make_party_unit(4, 34006, 1, 10, true)));
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
  CASE_EXPECT_TRUE(add_unit(target, make_party_unit(10, 35001, 2, 10, true)));
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
  CASE_EXPECT_EQ(first_response.matching_id(), overlapping_response.matching_id());
  CASE_EXPECT_EQ(202, overlapping_response.snapshot().selected_level_id());

  auto disjoint = make_create_request(803, 98003, 12, 0, 2);
  set_request_levels(disjoint, {203});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot disjoint_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, disjoint, disjoint_response));
  CASE_EXPECT_TRUE(first_response.matching_id() != disjoint_response.matching_id());
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, first_response.snapshot().status());
  CASE_EXPECT_EQ(1, manager->get_room_count());
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot second_response;
  auto second_request = make_create_request(2, 10002, 14, 2);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, second_response));
  CASE_EXPECT_EQ(first_response.matching_id(), second_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 second_response.snapshot().status());

  PROJECT_NAMESPACE_ID::SSMatchingConfirmReq confirm;
  confirm.set_unit_id(1);
  protobuf_copy_message(*confirm.mutable_operator_user(), first_request.operator_user());
  confirm.set_confirmed(true);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot confirm_response;
  const int64_t last_event_id_before_confirm = second_response.snapshot().last_event_id();
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, confirm_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 confirm_response.snapshot().status());
  CASE_EXPECT_EQ(last_event_id_before_confirm, confirm_response.snapshot().last_event_id());

  confirm.set_unit_id(2);
  protobuf_copy_message(*confirm.mutable_operator_user(), second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, confirm_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, confirm_response.snapshot().status());
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING,
                 manager->create_matching(ctx, request, conflict_response));

  auto same_user = make_create_request(11, 20001, 10);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING,
                 manager->create_matching(ctx, same_user, conflict_response));

  PROJECT_NAMESPACE_ID::DUserIDKey unauthorized_user;
  unauthorized_user.set_user_id(99999);
  unauthorized_user.set_zone_id(1);
  auto check = make_heartbeat_request(10, unauthorized_user);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
                 manager->check_matching(ctx, check, response));

  PROJECT_NAMESPACE_ID::SSMatchingCancelReq cancel;
  cancel.set_unit_id(10);
  protobuf_copy_message(*cancel.mutable_operator_user(), unauthorized_user);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND,
                 manager->cancel_matching(ctx, cancel, response));
  protobuf_copy_message(*cancel.mutable_operator_user(), request.operator_user());
  CASE_EXPECT_EQ(0, manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED, response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  auto cancelled_heartbeat = make_heartbeat_request(request.unit().unit_id(), request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, cancelled_heartbeat, response));
  CASE_EXPECT_TRUE(response.matching_id().empty());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED, response.snapshot().status());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, creates_first_unit_without_predicting_future_composition) {
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

  CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(1, manager->get_room_count());
  CASE_EXPECT_EQ(3, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, response.snapshot().status());

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, tracks_searching_users_after_join_rejection_cancel_and_clear) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};
  const auto first = make_create_request(921, 39981, 10, 0, 20);
  const auto second = make_create_request(922, 39982, 10, 0, 20);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first, response));
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());
  const auto matching_id = response.matching_id();
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second, response));
  CASE_EXPECT_EQ(matching_id, response.matching_id());
  CASE_EXPECT_EQ(2, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_USER_ALREADY_IN_MATCHING,
                 manager->create_matching(ctx, second, response));
  CASE_EXPECT_EQ(2, manager->get_total_matching_user_count());
  PROJECT_NAMESPACE_ID::SSMatchingCancelReq cancel;
  cancel.set_unit_id(first.unit().unit_id());
  protobuf_copy_message(*cancel.mutable_operator_user(), first.operator_user());
  CASE_EXPECT_EQ(0, manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());
  cancel.set_unit_id(second.unit().unit_id());
  protobuf_copy_message(*cancel.mutable_operator_user(), second.operator_user());
  CASE_EXPECT_EQ(0, manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND,
                 manager->cancel_matching(ctx, cancel, response));
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first, response));
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());
  manager->clear();
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, tick_confirms_full_exclusive_factions_after_threshold_downgrade) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto first_request = make_create_request(901, 39901, 10, 0, 20);
  auto second_request = make_create_request(902, 39902, 10, 0, 20);
  first_request.mutable_unit()->set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  second_request.mutable_unit()->set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot first_response;
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot second_response;
  const int32_t first_result = manager->create_matching(ctx, first_request, first_response);
  const int32_t second_result = manager->create_matching(ctx, second_request, second_response);
  CASE_EXPECT_EQ(0, first_result);
  CASE_EXPECT_EQ(0, second_result);
  if (first_result != 0 || second_result != 0) {
    manager->clear();
    atfw::util::time::time_utility::reset_global_now_offset();
    CASE_EXPECT_EQ(0, runtime.stop());
    return;
  }
  CASE_EXPECT_EQ(first_response.matching_id(), second_response.matching_id());
  const auto first_heartbeat = make_heartbeat_request(901, first_request.operator_user());
  const auto second_heartbeat = make_heartbeat_request(902, second_request.operator_user());
  for (int elapsed : {19, 20, 59, 60}) {
    atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{elapsed - 1});
    // 心跳本身也会评估开局，因此在边界前刷新，再跨过边界让 tick 单独触发状态变化。
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, first_heartbeat, first_response));
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, second_heartbeat, second_response));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                   first_response.snapshot().status());
    CASE_EXPECT_EQ(2, manager->get_total_matching_user_count());
    atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{elapsed});
    CASE_EXPECT_EQ(0, manager->tick());
    CASE_EXPECT_EQ(elapsed < 60 ? 2 : 0, manager->get_total_matching_user_count());
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, first_heartbeat, first_response));
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, second_heartbeat, second_response));
    const auto expected_status = elapsed < 60 ? PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING
                                              : PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING;
    CASE_EXPECT_EQ(expected_status, first_response.snapshot().status());
    CASE_EXPECT_EQ(expected_status, second_response.snapshot().status());
    CASE_EXPECT_EQ(1, manager->get_room_count());
    CASE_EXPECT_EQ(2, manager->get_room_unit_count(first_response.matching_id()));
    CASE_EXPECT_EQ(2, manager->get_room_faction_count(first_response.matching_id()));
  }
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, tick_confirms_full_unequal_factions_after_threshold_downgrade) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto party = make_party_create_request(911, 39911, 3, 10);
  party.mutable_scope()->set_matching_pool_id(20);
  set_request_levels(party, {2001});
  auto solo = make_create_request(912, 39914, 10, 0, 20);
  solo.mutable_unit()->set_faction_fill_policy(PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot party_response;
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot solo_response;
  const int32_t party_result = manager->create_matching(ctx, party, party_response);
  const int32_t solo_result = manager->create_matching(ctx, solo, solo_response);
  CASE_EXPECT_EQ(0, party_result);
  CASE_EXPECT_EQ(0, solo_result);
  if (party_result == 0 && solo_result == 0) {
    CASE_EXPECT_EQ(party_response.matching_id(), solo_response.matching_id());
    atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{19});
    // 刷新每个成员的心跳，避免把时钟推进误当作 Unit 离线。
    for (const auto& user : party.unit().users()) {
      const auto heartbeat = make_heartbeat_request(911, user.user_key());
      CASE_EXPECT_EQ(0, manager->check_matching(ctx, heartbeat, party_response));
    }
    const auto solo_heartbeat = make_heartbeat_request(912, solo.operator_user());
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, solo_heartbeat, solo_response));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                   solo_response.snapshot().status());
    CASE_EXPECT_EQ(4, manager->get_total_matching_user_count());
    atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{20});
    CASE_EXPECT_EQ(0, manager->tick());
    CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
    const auto party_heartbeat = make_heartbeat_request(911, party.operator_user());
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, party_heartbeat, party_response));
    CASE_EXPECT_EQ(0, manager->check_matching(ctx, solo_heartbeat, solo_response));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                   party_response.snapshot().status());
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                   solo_response.snapshot().status());
    CASE_EXPECT_EQ(1, manager->get_room_count());
    CASE_EXPECT_EQ(2, manager->get_room_unit_count(party_response.matching_id()));
    CASE_EXPECT_EQ(2, manager->get_room_faction_count(party_response.matching_id()));
  }
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, applies_balanced_policy_through_create_matching) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  rpc::context ctx{rpc::context::create_without_task()};

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  std::string matching_id;
  for (uint64_t unit_id = 1; unit_id <= 4; ++unit_id) {
    auto request = make_create_request(unit_id, 29000 + unit_id, 10, 0, 7);
    CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));
    if (unit_id == 1) {
      matching_id = response.matching_id();
    } else {
      CASE_EXPECT_EQ(matching_id, response.matching_id());
    }
    const size_t expected_faction_count = unit_id < 3 ? static_cast<size_t>(unit_id) : size_t{3};
    CASE_EXPECT_EQ(expected_faction_count, manager->get_room_faction_count(matching_id));
  }

  CASE_EXPECT_EQ(1, manager->get_room_count());
  CASE_EXPECT_EQ(4, manager->get_total_matching_user_count());

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
  CASE_EXPECT_TRUE(oldest_response.matching_id() != newer_response.matching_id());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot candidate_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(23, 30003, 50, 0, 2), candidate_response));
  CASE_EXPECT_EQ(oldest_response.matching_id(), candidate_response.matching_id());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(candidate_response.matching_id()));

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
  const std::string oldest_matching_id = oldest_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot pending_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(26, 31101, 2, 100), pending_response));
  const std::string pending_matching_id = pending_response.matching_id();
  CASE_EXPECT_TRUE(oldest_matching_id != pending_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot joined_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_party_create_request(27, 31201, 1, 50), joined_response));
  CASE_EXPECT_EQ(pending_matching_id, joined_response.matching_id());
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
  const std::string target_matching_id = target_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source_first = make_create_request(30, 31401, 0, 0, 5);
  auto source_second = make_create_request(31, 31402, 2, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_first, source_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_second, source_response));
  const std::string source_matching_id = source_response.matching_id();
  CASE_EXPECT_TRUE(source_matching_id != target_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  auto check = make_heartbeat_request(source_first.unit().unit_id(), source_first.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 checked_response.snapshot().status());
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
  const std::string source_matching_id = source_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(33, 31601, 100, 0, 5), target_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, make_create_request(34, 31602, 102, 0, 5), target_response));
  const std::string target_matching_id = target_response.matching_id();
  CASE_EXPECT_TRUE(source_matching_id != target_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  auto check = make_heartbeat_request(source_request.unit().unit_id(), source_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                 checked_response.snapshot().status());
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
  const std::string target_matching_id = isolated_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto larger_request = make_create_request(32, 40002, 100, 0, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot larger_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, larger_request, larger_response));
  auto second_donor_request = make_create_request(33, 40003, 102, 0, 2);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_donor_request, larger_response));
  const std::string donor_matching_id = larger_response.matching_id();
  CASE_EXPECT_TRUE(target_matching_id != donor_matching_id);
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(donor_matching_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                 larger_response.snapshot().status());

  // 规则在 60 秒后扩散；先在租约窗口内刷新三个 Unit，避免本用例把“在线 donor 重平衡”
  // 和失联 Unit 摘除混为一谈。
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{55});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot heartbeat_response;
  CASE_EXPECT_EQ(0,
                 manager->check_matching(
                     ctx, make_heartbeat_request(isolated_request.unit().unit_id(), isolated_request.operator_user()),
                     heartbeat_response));
  CASE_EXPECT_EQ(0, manager->check_matching(
                        ctx, make_heartbeat_request(larger_request.unit().unit_id(), larger_request.operator_user()),
                        heartbeat_response));
  CASE_EXPECT_EQ(
      0, manager->check_matching(
             ctx, make_heartbeat_request(second_donor_request.unit().unit_id(), second_donor_request.operator_user()),
             heartbeat_response));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(3, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(0, manager->get_room_unit_count(donor_matching_id));

  auto check = make_heartbeat_request(isolated_request.unit().unit_id(), isolated_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot migrated_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 migrated_response.snapshot().status());
  CASE_EXPECT_EQ(2, manager->get_room_count());

  auto stale_source_check = make_heartbeat_request(larger_request.unit().unit_id(), larger_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, stale_source_check, migrated_response));
  CASE_EXPECT_EQ(target_matching_id, migrated_response.matching_id());
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
  const std::string target_matching_id = target_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto older_donor_request = make_create_request(35, 40102, 100, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot older_donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, older_donor_request, older_donor_response));
  const std::string older_donor_matching_id = older_donor_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{2});
  auto newer_donor_request = make_create_request(36, 40103, 200, 0, 5);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot newer_donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, newer_donor_request, newer_donor_response));
  const std::string newer_donor_matching_id = newer_donor_response.matching_id();
  CASE_EXPECT_TRUE(target_matching_id != older_donor_matching_id);
  CASE_EXPECT_TRUE(target_matching_id != newer_donor_matching_id);
  CASE_EXPECT_TRUE(older_donor_matching_id != newer_donor_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{63});
  auto target_check = make_heartbeat_request(target_request.unit().unit_id(), target_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot target_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, target_check, target_checked_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 target_checked_response.snapshot().status());

  auto older_donor_check =
      make_heartbeat_request(older_donor_request.unit().unit_id(), older_donor_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot older_donor_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, older_donor_check, older_donor_checked_response));
  CASE_EXPECT_EQ(target_matching_id, older_donor_checked_response.matching_id());

  auto newer_donor_check =
      make_heartbeat_request(newer_donor_request.unit().unit_id(), newer_donor_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot newer_donor_checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, newer_donor_check, newer_donor_checked_response));
  CASE_EXPECT_EQ(newer_donor_matching_id, newer_donor_checked_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
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
  const std::string target_matching_id = target_response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto donor_request = make_party_create_request(92, 89101, 2, 100);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot donor_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, donor_request, donor_response));
  const std::string donor_matching_id = donor_response.matching_id();
  CASE_EXPECT_TRUE(target_matching_id != donor_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  auto check = make_heartbeat_request(target_request.unit().unit_id(), target_request.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(target_matching_id, checked_response.matching_id());
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
  const std::string target_matching_id = target_response.matching_id();
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                 target_response.snapshot().status());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source_duo_one = make_party_create_request(104, 90101, 2, 100);
  auto source_solo_one = make_party_create_request(105, 90103, 1, 102);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_duo_one, source_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source_solo_one, source_response));
  const std::string source_matching_id = source_response.matching_id();
  CASE_EXPECT_TRUE(target_matching_id != source_matching_id);
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(source_matching_id));
  CASE_EXPECT_EQ(6, manager->get_total_matching_user_count());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  auto check_moved = make_heartbeat_request(target_duo.unit().unit_id(), target_duo.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot moved_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check_moved, moved_response));
  CASE_EXPECT_EQ(target_matching_id, moved_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 moved_response.snapshot().status());
  CASE_EXPECT_EQ(4, manager->get_room_unit_count(target_matching_id));
  CASE_EXPECT_EQ(2, manager->get_room_faction_count(target_matching_id));
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  auto check_faction_member = make_heartbeat_request(source_solo_one.unit().unit_id(), source_solo_one.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot faction_member_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check_faction_member, faction_member_response));
  CASE_EXPECT_EQ(target_matching_id, faction_member_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 faction_member_response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

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
  const std::string target_matching_id = target_response.matching_id();
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(target_matching_id));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{1});
  auto source = make_create_request(813, 98103, 0, 0, 2);
  set_request_levels(source, {202});
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot source_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, source, source_response));
  const std::string source_matching_id = source_response.matching_id();
  CASE_EXPECT_TRUE(target_matching_id != source_matching_id);

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{62});
  auto check = make_heartbeat_request(source.unit().unit_id(), source.operator_user());
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot checked_response;
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, checked_response));
  CASE_EXPECT_EQ(source_matching_id, checked_response.matching_id());
  CASE_EXPECT_EQ(202, checked_response.snapshot().selected_level_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
                 checked_response.snapshot().status());
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

CASE_TEST(matchsvr_matching_manager, expires_whole_searching_unit_when_one_member_stops_heartbeat) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto request = make_party_create_request(50, 59001, 2, 10);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, request, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, response.snapshot().status());
  const std::string matching_id = response.matching_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{6});
  auto first_user_heartbeat = make_heartbeat_request(request.unit().unit_id(), request.unit().users(0).user_key(), 0,
                                                     request.subscriber_routes(0).server_id());
  CASE_EXPECT_EQ(
      PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT,
      manager->check_matching(ctx, first_user_heartbeat, response, first_user_heartbeat.subscriber_server_id() + 1));
  CASE_EXPECT_EQ(
      0, manager->check_matching(ctx, first_user_heartbeat, response, first_user_heartbeat.subscriber_server_id()));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{11});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(0, manager->get_room_unit_count(matching_id));
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, first_user_heartbeat, response));
  CASE_EXPECT_TRUE(response.matching_id().empty());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT, response.snapshot().status());

  atfw::util::time::time_utility::reset_global_now_offset();
  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, refreshes_searching_users_after_heartbeat_removes_part_of_room) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }
  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  const auto expiring = make_party_create_request(501, 59101, 2, 10);
  const auto surviving = make_party_create_request(502, 59103, 1, 10);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, expiring, response));
  const std::string matching_id = response.matching_id();
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, surviving, response));
  CASE_EXPECT_EQ(matching_id, response.matching_id());
  CASE_EXPECT_EQ(3, manager->get_total_matching_user_count());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{6});
  auto heartbeat = make_heartbeat_request(surviving.unit().unit_id(), surviving.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, heartbeat, response));
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{11});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(matching_id));
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, heartbeat, response));
  CASE_EXPECT_EQ(matching_id, response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, response.snapshot().status());

  // 剩余房间仍在搜索桶中；补入新 Unit 后再次刷新人数，不能漏计或重复累计。
  const auto replacement = make_party_create_request(503, 59104, 2, 10);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, replacement, response));
  CASE_EXPECT_EQ(matching_id, response.matching_id());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(matching_id));
  CASE_EXPECT_EQ(3, manager->get_total_matching_user_count());
  manager->clear();
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  atfw::util::time::time_utility::reset_global_now_offset();
  CASE_EXPECT_EQ(0, runtime.stop());
}

CASE_TEST(matchsvr_matching_manager, does_not_evict_units_after_matching_state) {
  atframework::testing::runtime runtime;
  if (!start_runtime(runtime)) {
    return;
  }

  auto manager = matching_manager::me();
  manager->clear();
  atfw::util::time::time_utility::reset_global_now_offset();
  atfw::util::time::time_utility::update();
  rpc::context ctx{rpc::context::create_without_task()};
  auto first_request = make_create_request(53, 59501, 10, 1);
  auto second_request = make_create_request(54, 59502, 14, 2);
  PROJECT_NAMESPACE_ID::SSMatchingSnapshot response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, response));
  const std::string matching_id = response.matching_id();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, response.snapshot().status());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{11});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(matching_id));
  auto heartbeat = make_heartbeat_request(first_request.unit().unit_id(), first_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, heartbeat, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, response.snapshot().status());

  const int64_t now = atfw::util::time::time_utility::get_now();
  CASE_EXPECT_TRUE(manager->prepare_battle_creation_for_test(matching_id, 0x180010, now + 120));
  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{22});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(2, manager->get_room_unit_count(matching_id));
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, heartbeat, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE, response.snapshot().status());

  atfw::util::time::time_utility::reset_global_now_offset();
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
  auto check = make_heartbeat_request(request.unit().unit_id(), request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT, response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{181});
  CASE_EXPECT_EQ(1, manager->tick());
  CASE_EXPECT_EQ(0, manager->get_room_count());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_UNIT_NOT_FOUND,
                 manager->check_matching(ctx, check, response));

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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, response.snapshot().status());

  PROJECT_NAMESPACE_ID::SSMatchingConfirmReq confirm;
  confirm.set_unit_id(accepted_request.unit().unit_id());
  protobuf_copy_message(*confirm.mutable_operator_user(), accepted_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  const int64_t last_event_id_before_timeout = response.snapshot().last_event_id();

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{16});
  CASE_EXPECT_EQ(0, manager->tick());
  auto check = make_heartbeat_request(accepted_request.unit().unit_id(), accepted_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, response.snapshot().status());
  CASE_EXPECT_EQ(1, manager->get_room_unit_count(response.matching_id()));
  CASE_EXPECT_EQ(last_event_id_before_timeout + 1, response.snapshot().last_event_id());
  CASE_EXPECT_EQ(1, manager->get_total_matching_user_count());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  // 同一玩家重新发起匹配时，Lobby 会分配新的 Unit ID。
  auto retry_request = make_create_request(63, 70002, 14, 2);
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, retry_request, retry_response));
  CASE_EXPECT_EQ(response.matching_id(), retry_response.matching_id());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
                 retry_response.snapshot().status());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());

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
  const std::string matching_id = snapshot.matching_id();
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, snapshot.snapshot().status());
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
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_SOURCE_SERVER_ID_NOT_FOUND, orbit_response.result());
  auto check = make_heartbeat_request(first_request.unit().unit_id(), first_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE, snapshot.snapshot().status());
  CASE_EXPECT_EQ(1001, snapshot.snapshot().faction_id());
  check.set_unit_id(second_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_heartbeat_data()->mutable_user_key(), second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(1001, snapshot.snapshot().faction_id());
  check.set_unit_id(first_request.unit().unit_id());
  protobuf_copy_message(*check.mutable_heartbeat_data()->mutable_user_key(), first_request.operator_user());

  orbit_response.Clear();
  CASE_EXPECT_TRUE(invoke_ready(false, kOrbitServerId, orbit_response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_START_FAILED, orbit_response.result());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, snapshot.snapshot().status());
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
  const std::string matching_id = snapshot.matching_id();
  const int64_t now = atfw::util::time::time_utility::get_now();
  constexpr uint64_t kOrbitServerId = 0x180002;
  CASE_EXPECT_TRUE(manager->prepare_battle_creation_for_test(matching_id, kOrbitServerId, now + 5));

  atfw::util::time::time_utility::set_global_now_offset(std::chrono::seconds{6});
  CASE_EXPECT_EQ(0, manager->tick());
  CASE_EXPECT_EQ(0, manager->get_total_matching_user_count());
  auto check = make_heartbeat_request(first_request.unit().unit_id(), first_request.operator_user());
  CASE_EXPECT_EQ(0, manager->check_matching(ctx, check, snapshot));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, snapshot.snapshot().status());
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
  confirm.set_unit_id(first_request.unit().unit_id());
  protobuf_copy_message(*confirm.mutable_operator_user(), first_request.operator_user());
  confirm.set_confirmed(true);
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  confirm.set_unit_id(second_request.unit().unit_id());
  protobuf_copy_message(*confirm.mutable_operator_user(), second_request.operator_user());
  CASE_EXPECT_EQ(0, manager->confirm_matching(ctx, confirm, response));
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, response.snapshot().status());
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_BATTLE_START_FAILED, response.snapshot().result());

  PROJECT_NAMESPACE_ID::SSMatchingSnapshot retry_response;
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, first_request, retry_response));
  CASE_EXPECT_EQ(0, manager->create_matching(ctx, second_request, retry_response));

  manager->clear();
  CASE_EXPECT_EQ(0, runtime.stop());
}
