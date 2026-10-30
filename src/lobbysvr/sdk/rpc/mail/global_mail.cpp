// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#include "rpc/mail/global_mail.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/extern_service_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.struct.mail.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.struct.mail.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>
#include <config/excel_config_mail_index.h>
#include <config/logic_config.h>

#include <logic/hpa/logic_hpa_easy_api.h>
#include <logic/logic_server_setup.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/db/uuid.h>
#include <rpc/rpc_context.h>

#include <chrono>
#include <memory>

#include "logic/mail/mail_utils.h"
#include "rpc/lobby/lobbysvrservice.atfw.gen.h"

namespace rpc {
namespace mail {
namespace {
static void compact_mails(rpc::context &ctx, atfw::mail::table_global_mail_blob_data &db_data) {
  int32_t max_mail_count = excel::get_const_config().global_mail_max_count_per_major_type();
  if (max_mail_count <= 0) {
    return;
  }

  auto now = ctx.logical_now();
  int32_t future_reserve_count = excel::get_const_config().global_future_reserve_mail_max_count_per_major_type();
  int32_t future_mail_count = 0;
  for (const auto &record : db_data.mail_records()) {
    if (protobuf_to_system_clock(record.start_time()) > now) {
      ++future_mail_count;
    }
  }
  if (future_reserve_count > future_mail_count) {
    future_reserve_count = future_mail_count;
  }
  if (future_reserve_count < 0) {
    future_reserve_count = 0;
  }
  max_mail_count += future_reserve_count;

  // 按类型只保留一定数量的邮件
  if (db_data.mail_records_size() <= max_mail_count) {
    return;
  }

  // 用于检查未来生效的邮件，防止起始生效时间异常过高而无法删除
  auto delivery_time_max_offset =
      protobuf_to_system_clock(logic_config::me()->get_logic_cfg().mail().compact_delivery_time_max_offset());
  if (delivery_time_max_offset <= std::chrono::seconds(1)) {
    delivery_time_max_offset = std::chrono::seconds(30 * atfw::util::time::time_utility::DAY_SECONDS);
  }

  int32_t rmcnt = db_data.mail_records_size() - max_mail_count;
  while (rmcnt-- > 0) {
    if (db_data.mail_records_size() <= max_mail_count) {
      return;
    }

    int selected_idx = -1;
    std::chrono::system_clock::time_point select_mail_compact_time = std::chrono::system_clock::time_point::min();
    for (int i = 0; i < db_data.mail_records_size(); ++i) {
      const PROJECT_NAMESPACE_ID::DMailRecord &record = db_data.mail_records(i);
      if (atfw::mail::mail_is_history_removable(ctx, record)) {
        PROJECT_NAMESPACE_ID::DMailRecord *dirty_record = db_data.add_pending_to_remove();
        if (nullptr == dirty_record) {
          FCTXLOGERROR(ctx, "{}", "malloc failed");
          break;
        }
        protobuf_copy_message(*dirty_record, record);
        // NOLINTNEXTLINE(bugprone-signed-bitwise)
        dirty_record->set_status(dirty_record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
        // 重设失效时间为当前时间+容忍误差时间,之后可以删除该记录
        auto remove_time = now;
        remove_time += std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT);
        if (protobuf_to_system_clock(dirty_record->expired_time()) > remove_time) {
          *dirty_record->mutable_expired_time() = protobuf_from_system_clock(remove_time);
        }
        if (protobuf_to_system_clock(dirty_record->remove_time()) > remove_time) {
          *dirty_record->mutable_remove_time() = protobuf_from_system_clock(remove_time);
        }
        protobuf_remove_repeated_at(*db_data.mutable_mail_records(), i);
        --i;
        continue;
      }

      auto delivery_time = protobuf_to_system_clock(record.delivery_time());
      auto compact_time = protobuf_to_system_clock(record.start_time());
      if (delivery_time > compact_time) {
        compact_time = delivery_time;
      } else if (compact_time > delivery_time + delivery_time_max_offset) {
        compact_time = delivery_time + delivery_time_max_offset;
      }

      if (selected_idx == -1 || delivery_time < select_mail_compact_time) {
        selected_idx = i;
        select_mail_compact_time = delivery_time;
      }
    }

    // 移除完过期邮件，可能已经不满了
    if (db_data.mail_records_size() <= max_mail_count || selected_idx < 0) {
      return;
    }

    PROJECT_NAMESPACE_ID::DMailRecord *dirty_record = db_data.add_pending_to_remove();
    if (nullptr == dirty_record) {
      FCTXLOGERROR(ctx, "{}", "malloc failed");
      break;
    }
    protobuf_copy_message(*dirty_record, db_data.mail_records(selected_idx));
    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    dirty_record->set_status(dirty_record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
    // 重设失效时间为当前时间+容忍误差时间,之后可以删除该记录
    auto remove_time = now;
    remove_time += std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT);
    if (protobuf_to_system_clock(dirty_record->expired_time()) > remove_time) {
      *dirty_record->mutable_expired_time() = protobuf_from_system_clock(remove_time);
    }
    if (protobuf_to_system_clock(dirty_record->remove_time()) > remove_time) {
      *dirty_record->mutable_remove_time() = protobuf_from_system_clock(remove_time);
    }
    protobuf_remove_repeated_at(*db_data.mutable_mail_records(), selected_idx);
  }
}

static result_void_type notify_lobbysvr(rpc::context &ctx, uint32_t zone_id) {
  // broadcast to all gamesvr to update global record index
  do {
    logic_server_common_module *mod = logic_server_last_common_module();
    if (mod == nullptr) {
      break;
    }

    auto discovery_index =
        mod->get_discovery_index_by_type(static_cast<uint64_t>(::atfw::component::logic_service_type::kLobbySvr));
    if (!discovery_index) {
      break;
    }

    const auto &all_nodes = discovery_index->get_sorted_nodes(
        logic_hpa_discovery_select(static_cast<uint64_t>(::atfw::component::logic_service_type::kLobbySvr)));

    // 没有监听gamesvr的话就不用通知了
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMailUpdateGlobalMailsSync> req_body{ctx};
    for (const auto &svr_info : all_nodes) {
      if (!svr_info) {
        continue;
      }

      uint64_t svr_zone_id = svr_info->get_discovery_info().area().zone_id();
      if (0 != zone_id && 0 != svr_zone_id && svr_zone_id != zone_id) {
        continue;
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::lobby::unicast::mail_update_global_mails(ctx, *svr_info, *req_body));
      if (res < 0) {
        FCTXLOGWARNING(ctx, "send mail update global mails sync failed(node_id={:#x}), res: {}({})",
                       svr_info->get_discovery_info().id(), res, protobuf_mini_dumper_get_error_msg(res));
      }
    }
  } while (false);

  RPC_RETURN_VOID;
}
}  // namespace

LOBBY_RPC_API result_code_type add_global_mail(rpc::context &ctx, PROJECT_NAMESPACE_ID::DMailContent &mail,
                                               uint32_t zone_id, int32_t channel, int64_t channel_param) {
  // check mail type
  if (!excel::mail::is_valid_global_mail(mail.major_type())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_INVALID_MAJOR_TYPE);
  }

  // check title/content
  if (mail.title().empty() || mail.content().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // alloc mail_id first
  if (mail.mail_id() == 0) {
    int64_t guid = RPC_AWAIT_TYPE_RESULT(
        rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_MAIL));
    if (guid < 0) {
      RPC_RETURN_CODE(static_cast<int32_t>(guid));
    }

    mail.set_mail_id(guid);
  }

  // setup delivery_time,show_time,start_time,expired_time
  if (mail.delivery_time().seconds() <= 0) {
    *mail.mutable_delivery_time() = protobuf_from_system_clock(ctx.logical_now());
  }
  if (mail.show_time().seconds() <= 0) {
    protobuf_copy_message(*mail.mutable_show_time(), mail.delivery_time());
  }
  if (mail.start_time().seconds() <= 0) {
    protobuf_copy_message(*mail.mutable_start_time(), mail.delivery_time());
  }
  if (mail.expired_time().seconds() <= 0) {
    *mail.mutable_expired_time() =
        protobuf_from_system_clock(protobuf_to_system_clock(mail.start_time()) +
                                   protobuf_to_system_clock(::excel::get_const_config().mail_default_expire()));
  }
  if (mail.remove_time().seconds() <= 0) {
    if (mail.resolve_expired_time().seconds() > 0) {
      *mail.mutable_remove_time() = protobuf_from_system_clock(atfw::mail::mail_padding_day_time(
          protobuf_to_system_clock(mail.expired_time()) + protobuf_to_system_clock(mail.resolve_expired_time())));
    } else {
      protobuf_copy_message(*mail.mutable_remove_time(), mail.expired_time());
    }
  }

  uint64_t ttl_second = PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT;
  time_t timeout = static_cast<time_t>(
      std::chrono::duration_cast<std::chrono::seconds>(protobuf_to_system_clock(mail.remove_time()) - ctx.logical_now())
          .count());
  if (timeout > 0) {
    ttl_second += static_cast<uint64_t>(timeout);
  }

  // DB: add mail content
  auto mail_table = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_content>(ctx);
  // DB: add mail content
  mail_table->set_mail_id(mail.mail_id());
  // 全服邮件发送频率低，先简单copy，后面有需要再优化成move。 move后不可再使用
  protobuf_copy_message(*mail_table->mutable_blob_data()->mutable_mail_content(), mail);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::insert(ctx, mail_table, nullptr));
  if (ret < 0) {
    FCTXLOGERROR(ctx, "task {} add_mail_content({}) for global failed, res: {}({})", ctx.get_task_context().task_id,
                 mail.mail_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  // 插入保底TTL，以防泄露
  ret = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::set_ttl(ctx, mail_table->mail_id(), ttl_second));
  if (ret < 0) {
    FCTXLOGERROR(ctx, "task {} add_mail_content({}) for global success but set_ttl failed, res: {}({})",
                 ctx.get_task_context().task_id, mail.mail_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
  }
  FCTXLOGINFO(ctx, "task {} add_mail_content({}) for global success", ctx.get_task_context().task_id, mail.mail_id());

  // DB: retry and add mail record
  int left_retry = 5;
  while (left_retry-- > 0) {
    auto db_table = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_global_index>(ctx);
    uint64_t db_version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::db::mail_global_index::get_all(ctx, zone_id, mail.major_type(), *db_table, db_version));
    if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      FCTXLOGERROR(ctx, "get_global_mail({}, {}) failed, res: {}({})", zone_id, mail.major_type(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
      continue;
    }

    // 可能时新增数据，强制赋值一下
    db_table->set_zone_id(zone_id);
    db_table->set_major_type(mail.major_type());
    PROJECT_NAMESPACE_ID::DMailRecord *mail_record = db_table->mutable_blob_data()->add_mail_records();
    if (nullptr == mail_record) {
      FCTXLOGERROR(ctx, "get_global_mail({}, {}) failed, malloc failed", zone_id, mail.major_type());
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      continue;
    }

    mail_record->set_mail_id(mail.mail_id());
    mail_record->set_major_type(mail.major_type());
    mail_record->set_minor_type(mail.minor_type());
    mail_record->set_status(PROJECT_NAMESPACE_ID::EN_MAIL_ST_NONE);
    protobuf_copy_message(*mail_record->mutable_delivery_time(), mail.delivery_time());
    protobuf_copy_message(*mail_record->mutable_start_time(), mail.start_time());
    protobuf_copy_message(*mail_record->mutable_show_time(), mail.show_time());
    protobuf_copy_message(*mail_record->mutable_expired_time(), mail.expired_time());
    protobuf_copy_message(*mail_record->mutable_remove_time(), mail.remove_time());
    protobuf_copy_message(*mail_record->mutable_resolve_expired_time(), mail.resolve_expired_time());
    mail_record->set_is_global_mail(true);

    // 如果邮件太多需要淘汰老的
    compact_mails(ctx, *db_table->mutable_blob_data());
    res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::replace(ctx, db_table, db_version));
    if (res < 0) {
      FCTXLOGERROR(ctx, "replace_global_mail({}, {}) failed, res: {}({})", zone_id, mail.major_type(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
      continue;
    }

    ret = 0;
    left_retry = 0;

    // broadcast to all gamesvr to update global record index
    RPC_AWAIT_IGNORE_VOID(notify_lobbysvr(ctx, zone_id));
  }

  // FIXME OSS Log
  // std::unique_ptr<MAILGLOBALSENDMAILFLOW> oss_tdr(new MAILGLOBALSENDMAILFLOW());
  // if (oss_tdr) {
  //   oss_tdr->tableBase.iZoneAreaID = zone_id;
  //   memset(oss_tdr->tableBase.vOpenID, 0, sizeof(oss_tdr->tableBase.vOpenID));
  //
  //   mail_dump_to_oss_tdr(*oss_tdr, mail);
  //   LogMgr::SendOssLog(E_OSS_LOG_MAIL_GLOBAL_SEND_MAIL_FLOW, *oss_tdr);
  // }

  RPC_RETURN_CODE(ret);
}

LOBBY_RPC_API result_code_type add_global_mail_with_template(rpc::context &ctx, int32_t mail_template_id,
                                                             const PROJECT_NAMESPACE_ID::DMailUserInfo *sender,
                                                             uint32_t zone_id, int32_t channel, int64_t channel_param,
                                                             bool render_by_client) {
  PROJECT_NAMESPACE_ID::DMailUserInfo receiver;
  // check mail_template_id
  if (mail_template_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // fake system nickname
  receiver.mutable_profile()->set_nick_name(excel::get_const_config().i18n_system_admin());

  // alloc mail_id first
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DMailContent> mail(ctx);
  // render mail title and content
  int32_t ret =
      atfw::mail::mail_render_template(ctx, mail_template_id, *mail, receiver, sender, nullptr, render_by_client);
  if (ret < 0) {
    RPC_RETURN_CODE(ret);
  }

  if (mail->mail_id() == 0) {
    int64_t guid = RPC_AWAIT_TYPE_RESULT(
        rpc::db::uuid::generate_global_unique_id(ctx, PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_MAIL));
    if (guid < 0) {
      RPC_RETURN_CODE(static_cast<int32_t>(guid));
    }

    mail->set_mail_id(guid);
  }

  // call add_user_mail()
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(add_global_mail(ctx, *mail, zone_id, channel, channel_param)));
}

LOBBY_RPC_API result_code_type remove_global_mail(rpc::context &ctx, uint32_t zone_id, int64_t mail_id) {
  if (mail_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  int32_t major_type = 0;
  // DB: retry and add mail record
  int left_retry = 5;
  int32_t ret = 0;

  while (left_retry-- > 0) {
    if (major_type == 0) {
      // DB: get mail content
      uint64_t db_version = 0;
      auto mail_content = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_content>(ctx);
      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::get_all(ctx, mail_id, *mail_content, db_version));
      if (res == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND);
      }
      if (res < 0) {
        FCTXLOGERROR(ctx, "batch_get_mail_content({}) failed, res: {}({})", mail_id, res,
                     protobuf_mini_dumper_get_error_msg(res));
        ret = res;
        continue;
      }

      if (mail_content->blob_data().mail_content().mail_id() != mail_id ||
          mail_content->blob_data().mail_content().major_type() == 0) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
        break;
      }

      major_type = mail_content->blob_data().mail_content().major_type();
    }

    auto db_table = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_global_index>(ctx);
    uint64_t db_version = 0;
    int32_t res =
        RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::get_all(ctx, zone_id, major_type, *db_table, db_version));
    if (res == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
      break;
    }

    if (res < 0) {
      FCTXLOGERROR(ctx, "get_global_mail({}, {}) failed, res: {}({})", zone_id, major_type, res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
      continue;
    }

    auto &db_data = *db_table->mutable_blob_data();
    int selected_idx = -1;
    for (int i = 0; i < db_data.mail_records_size(); ++i) {
      if (db_data.mail_records(i).mail_id() == mail_id) {
        selected_idx = i;
        break;
      }
    }

    if (selected_idx < 0) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND);
    }

    auto *dirty_record = db_data.add_pending_to_remove();
    if (nullptr == dirty_record) {
      FCTXLOGERROR(ctx, "malloc failed");
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      break;
    }
    protobuf_copy_message(*dirty_record, db_data.mail_records(selected_idx));
    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    dirty_record->set_status(dirty_record->status() | PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED);
    // 重设失效时间为当前时间+容忍误差时间,之后可以删除该记录
    auto remove_time = ctx.logical_now();
    remove_time += std::chrono::seconds(PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT);
    if (protobuf_to_system_clock(dirty_record->expired_time()) > remove_time) {
      *dirty_record->mutable_expired_time() = protobuf_from_system_clock(remove_time);
    }
    if (protobuf_to_system_clock(dirty_record->remove_time()) > remove_time) {
      *dirty_record->mutable_remove_time() = protobuf_from_system_clock(remove_time);
    }
    protobuf_remove_repeated_at(*db_data.mutable_mail_records(), selected_idx);

    // 如果邮件太多需要淘汰老的
    compact_mails(ctx, db_data);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::mail_global_index::replace(ctx, db_table, db_version));
    if (res < 0) {
      FCTXLOGERROR(ctx, "replace_global_mail({}, {}) failed, res: {}({})", zone_id, major_type, res,
                   protobuf_mini_dumper_get_error_msg(res));
      ret = res;
      continue;
    }

    // 插入保底TTL，以防泄露
    uint64_t ttl_second = PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT;
    ret = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::set_ttl(ctx, mail_id, ttl_second));
    if (ret < 0) {
      FCTXLOGERROR(ctx, "task {} update ttl for global mail {} failed, res: {}({})", ctx.get_task_context().task_id,
                   mail_id, ret, protobuf_mini_dumper_get_error_msg(ret));
    }

    ret = 0;
    left_retry = 0;
    // broadcast to all gamesvr to update global record index
    RPC_AWAIT_IGNORE_VOID(notify_lobbysvr(ctx, zone_id));
  }

  RPC_RETURN_CODE(ret);
}
}  // namespace mail
}  // namespace rpc
