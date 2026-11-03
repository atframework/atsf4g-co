// Copyright 2026 atframework

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.const.config.pb.h>
#include <protocol/config/com.struct.item.config.pb.h>
#include <protocol/config/pb_header_v3.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.mail.pb.h>
#include <protocol/pbdesc/com.struct.mail.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp.h>
#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/runtime.h>
#include <config/excel/config_manager.h>
#include <router/router_manager_set.h>
#include <rpc/rpc_context.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"
#include "data/user.h"
#include "frame/test_macros.h"
#include "logic/item/user_item_container_manager.h"
#include "logic/item/user_item_manager.h"
#include "logic/mail/user_mail_manager.h"
#include "rpc/async_jobs/async_jobs.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/mail/global_mail.h"
#include "rpc/mail/user_mail.h"

namespace {

struct mail_test_data {
  PROJECT_NAMESPACE_ID::DMailRecord record;
  PROJECT_NAMESPACE_ID::DMailContent content;
};

std::string make_item_type_table() {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("lobbysvr-mail-test");
  PROJECT_NAMESPACE_ID::config::ExcelItemType item;
  item.set_type_id(1001);
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

std::string make_global_compaction_config() {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("lobbysvr-mail-compaction-test");
  PROJECT_NAMESPACE_ID::config::ExcelConstConfig config;
  config.set_fake_key(1);
  config.set_global_mail_max_count_per_major_type(1);
  config.set_global_future_reserve_mail_max_count_per_major_type(0);
  config.mutable_mail_default_expire()->set_seconds(86400);
  blocks.add_data_block(config.SerializeAsString());
  return blocks.SerializeAsString();
}

mail_test_data make_mail(rpc::context& ctx, int64_t mail_id, int32_t attachment_type_id, int64_t attachment_count) {
  mail_test_data result;
  result.content.set_mail_id(mail_id);
  result.content.set_major_type(PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_BUSINESS);
  result.content.set_minor_type(7);
  result.content.set_title("mail test");
  result.content.set_content("mail test body");

  auto now = ctx.logical_now();
  *result.content.mutable_delivery_time() = protobuf_from_system_clock(now - std::chrono::seconds{1});
  *result.content.mutable_show_time() = protobuf_from_system_clock(now - std::chrono::seconds{1});
  *result.content.mutable_start_time() = protobuf_from_system_clock(now - std::chrono::seconds{1});
  *result.content.mutable_expired_time() = protobuf_from_system_clock(now + std::chrono::hours{1});

  if (attachment_type_id != 0) {
    auto* attachment = result.content.add_attachments();
    attachment->set_type_id(attachment_type_id);
    attachment->set_count(attachment_count);
  }

  result.record.set_mail_id(mail_id);
  result.record.set_major_type(result.content.major_type());
  result.record.set_minor_type(result.content.minor_type());
  *result.record.mutable_delivery_time() = result.content.delivery_time();
  *result.record.mutable_show_time() = result.content.show_time();
  *result.record.mutable_start_time() = result.content.start_time();
  *result.record.mutable_expired_time() = result.content.expired_time();
  result.record.set_is_global_mail(false);
  return result;
}

void run_mail_case(const char* name, const std::function<rpc::result_code_type(rpc::context&, user&)>& body,
                   const std::function<int(atfw::testing::runtime&)>& setup = {}) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs, atfw::testing::feature::db,
                      atfw::testing::feature::router, atfw::testing::feature::resource};
  options.setup_callback = [name, setup](atfw::testing::runtime& runtime) -> int {
    runtime.resource().set_file("item_type.bytes", make_item_type_table());
    runtime.resource().set_version(name);
    if (setup) {
      int result = setup(runtime);
      if (result < 0) {
        return result;
      }
    }
    return handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice();
  };
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }
  test.get_app()->mutable_area().set_zone_id(1);
  CASE_EXPECT_GE(router_manager_set::me()->tick(), 0);
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_mail_content>();
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_mail_global_index>();
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_user_async_jobs>();

  auto player = user::create(91001, 1, "mail-manager-test");
  CASE_EXPECT_TRUE(!!player);
  if (!player) {
    CASE_EXPECT_EQ(0, test.stop());
    return;
  }

  auto task = test.run_task(name, std::chrono::seconds{2}, [player, body](rpc::context& ctx) -> rpc::result_code_type {
    RPC_AWAIT_IGNORE_VOID(player->get_user_item_container_manager().create_init(ctx));
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(body(ctx, *player)));
  });
  CASE_EXPECT_FALSE(task.empty());
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_FALSE(result.hard_timed_out);
    CASE_EXPECT_EQ(0, result.result_code);
  }
  CASE_EXPECT_EQ(0, test.stop());
}

}  // namespace

CASE_TEST(lobbysvr_user_mail, receive_attachment_commits_items_once) {
  run_mail_case("mail.receive.success", [](rpc::context& ctx, user& player) -> rpc::result_code_type {
    auto config_group = excel::config_manager::me()->get_current_config_group();
    CASE_EXPECT_TRUE(!!config_group);
    if (config_group) {
      CASE_EXPECT_TRUE(!!config_group->ExcelItemType.get_by_type_id(1001));
    }
    auto mail = make_mail(ctx, 1001, 1001, 7);
    auto& mail_manager = player.get_user_mail_manager();
    int32_t result = mail_manager.add_mail(ctx, mail.record, &mail.content);
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    auto& item_manager = player.get_user_item_manager();
    const int64_t initial_count = item_manager.get_count(ctx, 1001);
    PROJECT_NAMESPACE_ID::DMailOperationResult operation_result;
    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 1001, &operation_result, false));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, operation_result.result());
    CASE_EXPECT_EQ(initial_count + 7, item_manager.get_count(ctx, 1001));

    auto stored_mail = mail_manager.get_mail_raw(1001);
    CASE_EXPECT_TRUE(!!stored_mail);
    if (stored_mail && stored_mail->record) {
      CASE_EXPECT_NE(0, stored_mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT);
    }

    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 1001, &operation_result, false));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_ALREADY_RECEIVED, result);
    CASE_EXPECT_EQ(initial_count + 7, item_manager.get_count(ctx, 1001));

    auto empty_mail = make_mail(ctx, 1002, 1001, 0);
    result = mail_manager.add_mail(ctx, empty_mail.record, &empty_mail.content);
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 1002, &operation_result, false));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_user_mail, receive_attachment_failure_preserves_mail) {
  run_mail_case("mail.receive.failure", [](rpc::context& ctx, user& player) -> rpc::result_code_type {
    auto& mail_manager = player.get_user_mail_manager();
    auto first_mail = make_mail(ctx, 2001, -1, 3);
    int32_t result = mail_manager.add_mail(ctx, first_mail.record, &first_mail.content);
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }
    auto second_mail = make_mail(ctx, 2002, -1, 5);
    result = mail_manager.add_mail(ctx, second_mail.record, &second_mail.content);
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 2001, nullptr, true));
    CASE_EXPECT_LT(result, 0);
    auto first_stored_mail = mail_manager.get_mail_raw(2001);

    result = RPC_AWAIT_CODE_RESULT(
        mail_manager.receive_all(ctx, PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_BUSINESS, 0, nullptr, true));
    CASE_EXPECT_LT(result, 0);
    auto second_stored_mail = mail_manager.get_mail_raw(2002);

    CASE_EXPECT_TRUE(!!first_stored_mail);
    CASE_EXPECT_TRUE(!!second_stored_mail);
    if (first_stored_mail && first_stored_mail->record) {
      CASE_EXPECT_EQ(0, first_stored_mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT);
      CASE_EXPECT_EQ(0, first_stored_mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
    }
    if (second_stored_mail && second_stored_mail->record) {
      CASE_EXPECT_EQ(0, second_stored_mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_TOKEN_ATTACHMENT);
      CASE_EXPECT_EQ(0, second_stored_mail->record->status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
    }
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_user_mail, read_missing_mail_returns_not_found_with_remove) {
  run_mail_case("mail.read.missing", [](rpc::context& ctx, user& player) -> rpc::result_code_type {
    PROJECT_NAMESPACE_ID::DMailOperationResult operation_result;
    int32_t result = player.get_user_mail_manager().read_mail(ctx, 999999, &operation_result, true);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND, result);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND, operation_result.result());
    CASE_EXPECT_EQ(999999, operation_result.record().mail_id());
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_user_mail, keep_after_take_attachments_overrides_remove_request) {
  run_mail_case("mail.receive.keep", [](rpc::context& ctx, user& player) -> rpc::result_code_type {
    auto mail = make_mail(ctx, 4001, 1001, 3);
    mail.content.set_flags(PROJECT_NAMESPACE_ID::EN_MAIL_FLAG_KEEP_AFTER_TAKE_ATTACHMENTS);
    auto& mail_manager = player.get_user_mail_manager();
    int32_t result = mail_manager.add_mail(ctx, mail.record, &mail.content);
    if (result < 0) {
      RPC_RETURN_CODE(result);
    }

    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 4001, nullptr, true));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    auto stored_mail = mail_manager.get_mail_raw(4001);
    CASE_EXPECT_TRUE(!!stored_mail);

    result = RPC_AWAIT_CODE_RESULT(mail_manager.receive_mail_attachments(ctx, 4001, nullptr, true));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_ALREADY_RECEIVED, result);
    stored_mail = mail_manager.get_mail_raw(4001);
    CASE_EXPECT_TRUE(!!stored_mail);
    CASE_EXPECT_EQ(3, player.get_user_item_manager().get_count(ctx, 1001));
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_user_mail, global_mail_uses_player_record_time) {
  run_mail_case("mail.global.player_time", [](rpc::context& ctx, user& player) -> rpc::result_code_type {
    auto& mail_manager = player.get_user_mail_manager();
    auto early_content = make_mail(ctx, 5001, 0, 0);
    early_content.record.set_is_global_mail(true);
    *early_content.content.mutable_expired_time() =
        protobuf_from_system_clock(ctx.logical_now() - std::chrono::hours{1});
    int32_t result = mail_manager.add_mail(ctx, early_content.record, &early_content.content);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    CASE_EXPECT_TRUE(mail_manager.is_mail_shown(ctx, mail_manager.get_mail_raw(5001)));
    result = mail_manager.read_mail(ctx, 5001, nullptr, false);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

    auto early_record = make_mail(ctx, 5002, 0, 0);
    early_record.record.set_is_global_mail(true);
    result = mail_manager.add_mail(ctx, early_record.record, &early_record.content);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    auto stored_mail = mail_manager.get_mail_raw(5002);
    CASE_EXPECT_TRUE(!!stored_mail);
    if (stored_mail && stored_mail->record) {
      *stored_mail->record->mutable_expired_time() =
          protobuf_from_system_clock(ctx.logical_now() - std::chrono::hours{1});
    }
    CASE_EXPECT_FALSE(mail_manager.is_mail_shown(ctx, stored_mail));
    result = mail_manager.read_mail(ctx, 5002, nullptr, false);
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_EXPIRED, result);
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_user_mail_rpc, add_user_mail_uses_real_content_keys) {
  run_mail_case("mail.rpc.content_keys", [](rpc::context& ctx, user&) -> rpc::result_code_type {
    const int64_t mail_ids[] = {3001, 3002};
    for (int64_t mail_id : mail_ids) {
      auto mail = make_mail(ctx, mail_id, 1001, 1);
      int32_t result = RPC_AWAIT_CODE_RESULT(
          rpc::mail::add_user_mail(ctx, 92000 + static_cast<uint64_t>(mail_id), 1, std::move(mail.content)));
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    }

    auto retry_mail = make_mail(ctx, 3001, 1001, 1);
    int32_t result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_user_mail(ctx, 95001, 1, std::move(retry_mail.content)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

    auto other_receiver = make_mail(ctx, 3001, 1001, 1);
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_user_mail(ctx, 95002, 1, std::move(other_receiver.content)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, result);

    rpc::shared_message<PROJECT_NAMESPACE_ID::table_mail_content> existing_content{ctx};
    existing_content->set_mail_id(3003);
    existing_content->mutable_blob_data()->mutable_receiver()->set_user_id(95003);
    existing_content->mutable_blob_data()->mutable_receiver()->set_zone_id(1);
    auto old_content = make_mail(ctx, 3003, 1001, 1);
    *old_content.content.mutable_delivery_time() =
        protobuf_from_system_clock(ctx.logical_now() - std::chrono::hours{1});
    *old_content.content.mutable_show_time() = old_content.content.delivery_time();
    *old_content.content.mutable_start_time() = old_content.content.delivery_time();
    *old_content.content.mutable_expired_time() = protobuf_from_system_clock(ctx.logical_now() + std::chrono::hours{1});
    protobuf_copy_message(*existing_content->mutable_blob_data()->mutable_mail_content(), old_content.content);
    uint64_t existing_version = 0;
    result = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::replace(ctx, existing_content, existing_version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

    auto default_time_retry = make_mail(ctx, 3003, 1001, 1);
    default_time_retry.content.clear_delivery_time();
    default_time_retry.content.clear_show_time();
    default_time_retry.content.clear_start_time();
    default_time_retry.content.clear_expired_time();
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_user_mail(ctx, 95003, 1, std::move(default_time_retry.content)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

    std::vector<rpc::db::async_jobs::table_user_async_jobs_list_message> retry_jobs;
    result = RPC_AWAIT_CODE_RESULT(
        rpc::async_jobs::get_jobs(ctx, PROJECT_NAMESPACE_ID::EN_PAJT_MAIL_SYSTEM, 95003, 1, retry_jobs));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    CASE_EXPECT_EQ(1, static_cast<int>(retry_jobs.size()));
    if (retry_jobs.size() == 1) {
      const auto& record = (*retry_jobs[0].message)->job_data().add_mail().mail_record();
      CASE_EXPECT_EQ(old_content.content.delivery_time().SerializeAsString(),
                     record.delivery_time().SerializeAsString());
      CASE_EXPECT_EQ(old_content.content.show_time().SerializeAsString(), record.show_time().SerializeAsString());
      CASE_EXPECT_EQ(old_content.content.start_time().SerializeAsString(), record.start_time().SerializeAsString());
      CASE_EXPECT_EQ(old_content.content.expired_time().SerializeAsString(), record.expired_time().SerializeAsString());
    }

    auto conflicting_mail = make_mail(ctx, 3001, 1001, 1);
    conflicting_mail.content.set_title("different content");
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_user_mail(ctx, 95001, 1, std::move(conflicting_mail.content)));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, result);

    for (int64_t mail_id : mail_ids) {
      PROJECT_NAMESPACE_ID::table_mail_content stored;
      uint64_t version = 0;
      result = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::get_all(ctx, mail_id, stored, version));
      CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
      CASE_EXPECT_EQ(mail_id, stored.mail_id());
      CASE_EXPECT_EQ(mail_id, stored.blob_data().mail_content().mail_id());
      CASE_EXPECT_EQ(92000 + static_cast<uint64_t>(mail_id), stored.blob_data().receiver().user_id());
      CASE_EXPECT_EQ(1u, stored.blob_data().receiver().zone_id());
    }
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_global_mail_rpc, client_rendered_template_allows_empty_body) {
  run_mail_case("mail.global.client_template", [](rpc::context& ctx, user&) -> rpc::result_code_type {
    auto mail = make_mail(ctx, 6001, 0, 0);
    mail.content.set_major_type(PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT);
    mail.content.clear_title();
    mail.content.clear_content();
    mail.content.set_mail_template_id(77);
    int32_t result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, mail.content, 1));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, mail.content, 1));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

    auto conflicting_mail = mail;
    conflicting_mail.content.set_title("different content");
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, conflicting_mail.content, 1));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, result);

    PROJECT_NAMESPACE_ID::table_mail_global_index stored_index;
    uint64_t version = 0;
    result = RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::get_all(
        ctx, 1, PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT, stored_index, version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
    CASE_EXPECT_EQ(1, stored_index.blob_data().mail_records_size());
    if (stored_index.blob_data().mail_records_size() == 1) {
      CASE_EXPECT_EQ(6001, stored_index.blob_data().mail_records(0).mail_id());
    }

    auto invalid_mail = make_mail(ctx, 6002, 0, 0);
    invalid_mail.content.set_major_type(PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT);
    invalid_mail.content.clear_title();
    invalid_mail.content.clear_content();
    result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, invalid_mail.content, 1));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM, result);
    RPC_RETURN_CODE(0);
  });
}

CASE_TEST(lobbysvr_global_mail_rpc, compaction_uses_capped_start_time) {
  run_mail_case(
      "mail.global.compaction",
      [](rpc::context& ctx, user&) -> rpc::result_code_type {
        auto now = ctx.logical_now();
        auto future_mail = make_mail(ctx, 7001, 0, 0);
        future_mail.content.set_major_type(PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT);
        *future_mail.content.mutable_delivery_time() = protobuf_from_system_clock(now - std::chrono::hours{24 * 30});
        *future_mail.content.mutable_start_time() = protobuf_from_system_clock(now + std::chrono::hours{24 * 365});
        *future_mail.content.mutable_expired_time() = protobuf_from_system_clock(now + std::chrono::hours{24 * 400});
        *future_mail.content.mutable_remove_time() = future_mail.content.expired_time();

        auto older_mail = make_mail(ctx, 7002, 0, 0);
        older_mail.content.set_major_type(PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT);
        *older_mail.content.mutable_delivery_time() = protobuf_from_system_clock(now - std::chrono::hours{24});
        *older_mail.content.mutable_start_time() = older_mail.content.delivery_time();
        *older_mail.content.mutable_expired_time() = future_mail.content.expired_time();
        *older_mail.content.mutable_remove_time() = future_mail.content.remove_time();

        int32_t result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, future_mail.content, 1));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
        result = RPC_AWAIT_CODE_RESULT(rpc::mail::add_global_mail(ctx, older_mail.content, 1));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);

        PROJECT_NAMESPACE_ID::table_mail_global_index stored_index;
        uint64_t version = 0;
        result = RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::get_all(
            ctx, 1, PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT, stored_index, version));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_SUCCESS, result);
        CASE_EXPECT_EQ(1, stored_index.blob_data().mail_records_size());
        bool found_future = false;
        bool found_older = false;
        for (const auto& record : stored_index.blob_data().mail_records()) {
          found_future = found_future || record.mail_id() == 7001;
          found_older = found_older || record.mail_id() == 7002;
        }
        CASE_EXPECT_TRUE(found_future);
        CASE_EXPECT_FALSE(found_older);
        RPC_RETURN_CODE(0);
      },
      [](atfw::testing::runtime& runtime) -> int {
        runtime.resource().set_file("const.bytes", make_global_compaction_config());
        return 0;
      });
}
