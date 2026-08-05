// Copyright 2026 atframework

#include <chrono>
#include <cstdint>
#include <string>

#include <atframework/testing/mock_resource.h>
#include <atframework/testing/runtime.h>

#include "config/excel/config_manager.h"
#include "frame/test_macros.h"

#include <protocol/config/com.const.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>

namespace {
std::string make_empty_table_bytes() {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("rpc-unit-test");
  return blocks.SerializeAsString();
}

std::string make_const_table_bytes(int32_t fake_key, int32_t mail_max_count) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("rpc-unit-test");
  PROJECT_NAMESPACE_ID::config::ExcelConstConfig item;
  item.set_fake_key(fake_key);
  item.set_mail_max_count_per_major_type(mail_max_count);
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

// All 7 tables are mandatory: a missing file fails load_all() of its table and therefore reload_all().
void seed_all_tables(atframework::testing::mock_resource &resource, int32_t fake_key, int32_t mail_max_count) {
  resource.set_file("const.bytes", make_const_table_bytes(fake_key, mail_max_count));
  resource.set_file("dtmq_channel_type.bytes", make_empty_table_bytes());
  resource.set_file("item_type.bytes", make_empty_table_bytes());
  resource.set_file("rank_define.bytes", make_empty_table_bytes());
  resource.set_file("rank_period_reward_pool.bytes", make_empty_table_bytes());
  resource.set_file("rank_rule.bytes", make_empty_table_bytes());
  resource.set_file("UeSource_Inventory.bytes", make_empty_table_bytes());
}

int32_t get_mail_max_count_by_fake_key(int32_t fake_key) {
  const excel::config_manager::config_group_ptr_t &group = excel::config_manager::me()->get_current_config_group();
  if (!group) {
    return -1;
  }
  auto item = group->ExcelConstConfig.get_by_fake_key(fake_key);
  if (!item) {
    return -1;
  }
  return item->mail_max_count_per_major_type();
}
}  // namespace

// server_frame component: the scoped resource provider feeds in-memory bytes into the real generated
// excel config manager; parse, index build and group swap all go through the production reload path.
// Replacing version + bytes and driving excel_config_wrapper_reload_all(false) must surface the new data.
CASE_TEST(server_frame_unit_test, resource_provider_parse_index_and_reload) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime &rt) {
    seed_all_tables(rt.resource(), 123, 11);
    rt.resource().set_version("0.0.0.1");
    return 0;
  };
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }

  // Startup init already parsed the provider bytes and built the index.
  CASE_EXPECT_EQ(11, static_cast<int>(get_mail_max_count_by_fake_key(123)));
  CASE_EXPECT_TRUE(!test.resource().access_history().empty());

  // Replace bytes + version and reload through the real wrapper.
  test.resource().set_file("const.bytes", make_const_table_bytes(123, 22));
  test.resource().set_version("0.0.0.2");
  CASE_EXPECT_TRUE(test.resource().reload() >= 0);
  CASE_EXPECT_EQ(22, static_cast<int>(get_mail_max_count_by_fake_key(123)));

  // Same version is a no-op reload: bytes changed but version kept, group stays.
  test.resource().set_file("const.bytes", make_const_table_bytes(123, 33));
  CASE_EXPECT_TRUE(test.resource().reload() >= 0);
  CASE_EXPECT_EQ(22, static_cast<int>(get_mail_max_count_by_fake_key(123)));

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: a missing mandatory resource file fails the reload through the real manager
// instead of being silently skipped.
CASE_TEST(server_frame_unit_test, resource_provider_missing_file_fails_reload) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::resource};
  options.setup_callback = [](atframework::testing::runtime &rt) {
    seed_all_tables(rt.resource(), 456, 44);
    rt.resource().set_version("0.0.0.3");
    return 0;
  };
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return;
  }
  CASE_EXPECT_EQ(44, static_cast<int>(get_mail_max_count_by_fake_key(456)));

  test.resource().remove_file("const.bytes");
  test.resource().set_version("0.0.0.5");
  CASE_EXPECT_TRUE(test.resource().reload() < 0);

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: teardown clears the active provider; the next fixture's provider serves its own
// bytes and sees none of the previous fixture's files. The excel manager singleton is process-lifetime, so
// each fixture must use a distinct version (see IMPLEMENTATION_PLAN.md 8.7).
CASE_TEST(server_frame_unit_test, resource_provider_isolated_between_fixtures) {
  {
    atframework::testing::runtime first;
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::resource};
    options.setup_callback = [](atframework::testing::runtime &rt) {
      seed_all_tables(rt.resource(), 789, 77);
      rt.resource().set_version("0.0.0.6");
      return 0;
    };
    if (0 != first.start(options) || !first.is_running()) {
      CASE_MSG_INFO() << "first runtime start failed: " << first.get_diagnostic() << '\n';
      return;
    }
    CASE_EXPECT_EQ(77, static_cast<int>(get_mail_max_count_by_fake_key(789)));
    CASE_EXPECT_EQ(0, first.stop());
    CASE_EXPECT_TRUE(!first.resource().is_bound());
    CASE_EXPECT_TRUE(first.resource().access_history().empty());
  }

  {
    atframework::testing::runtime second;
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::resource};
    options.setup_callback = [](atframework::testing::runtime &rt) {
      seed_all_tables(rt.resource(), 789, 88);
      rt.resource().set_version("0.0.1.0");
      return 0;
    };
    if (0 != second.start(options) || !second.is_running()) {
      CASE_MSG_INFO() << "second runtime start failed: " << second.get_diagnostic() << '\n';
      return;
    }
    CASE_EXPECT_EQ(88, static_cast<int>(get_mail_max_count_by_fake_key(789)));
    CASE_EXPECT_EQ(0, second.stop());
  }
}
