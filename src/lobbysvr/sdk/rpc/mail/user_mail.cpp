// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#include "rpc/mail/user_mail.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.struct.mail.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>
#include <config/excel_config_mail_index.h>
#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include <chrono>

#include "rpc/rpc_context.h"

#include "rpc/async_jobs/async_jobs.h"
#include "rpc/db/local_db_interface.atfw.gen.h"
#include "rpc/db/uuid.h"

#include "logic/mail/mail_utils.h"

namespace rpc {
namespace mail {
LOBBY_RPC_API result_code_type add_user_mail(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                             PROJECT_NAMESPACE_ID::DMailContent &&mail) {
  // check user_id
  if (user_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // check mail type
  if (mail.major_type() <= 0 || !excel::mail::is_valid_user_mail(mail.major_type())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_MAIL_INVALID_MAJOR_TYPE);
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
    auto expired_time = protobuf_to_system_clock(mail.start_time()) +
                        protobuf_to_system_clock(::excel::get_const_config().mail_default_expire());
    *mail.mutable_expired_time() = protobuf_from_system_clock(expired_time);
  }
  mail.clear_resolve_expired_time();  // 个人邮件不允许重置过期时间

  // DB: send mail record to user async jobs,(check EN_PAJT_MAIL_IMPORTANT/EN_PAJT_MAIL_SYSTEM/EN_PAJT_MAIL_BUSINESS)
  auto async_job = rpc::make_shared_message<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data>(ctx);

  PROJECT_NAMESPACE_ID::user_async_job_add_mail *mail_job = async_job->mutable_add_mail();
  if (nullptr == mail_job || nullptr == mail_job->mutable_mail_record()) {
    FCTXLOGERROR(ctx, "{}", "malloc user_async_job_add_mail failed");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  PROJECT_NAMESPACE_ID::DMailRecord *mail_record = mail_job->mutable_mail_record();
  mail_record->set_mail_id(mail.mail_id());
  mail_record->set_major_type(mail.major_type());
  mail_record->set_minor_type(mail.minor_type());
  mail_record->set_status(PROJECT_NAMESPACE_ID::EN_MAIL_ST_NONE);
  protobuf_copy_message(*mail_record->mutable_delivery_time(), mail.delivery_time());
  protobuf_copy_message(*mail_record->mutable_start_time(), mail.start_time());
  protobuf_copy_message(*mail_record->mutable_show_time(), mail.show_time());
  protobuf_copy_message(*mail_record->mutable_expired_time(), mail.expired_time());
  protobuf_copy_message(*mail_record->mutable_resolve_expired_time(), mail.resolve_expired_time());
  mail_record->set_is_global_mail(false);

  // 根据邮件大类转换异步任务pipeline
  int32_t jobs_type = 0;
  switch (mail.major_type()) {
    case PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_PAY:
    case PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT:
      jobs_type = PROJECT_NAMESPACE_ID::EN_PAJT_MAIL_IMPORTANT;
      break;
    default:
      if (mail.major_type() < PROJECT_NAMESPACE_ID::EN_MAIL_MT_BUSINESS_BOUND) {
        jobs_type = PROJECT_NAMESPACE_ID::EN_PAJT_MAIL_SYSTEM;
      } else {
        jobs_type = PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL;
      }
      break;
  }

  // FIXME Prepare OSS Log before mail is moved
  // std::unique_ptr<MAILUSERSENDMAILFLOW> oss_tdr(new MAILUSERSENDMAILFLOW());
  // if (oss_tdr) {
  //   oss_tdr->tableBase.iZoneAreaID = zone_id;
  //   UTIL_STRFUNC_SNPRINTF(oss_tdr->tableBase.vOpenID, sizeof(oss_tdr->tableBase.vOpenID) - 1, "%s", openid.c_str());
  //
  //   mail_dump_to_oss_tdr(*oss_tdr, async_job->add_mail());
  //   oss_tdr->senderUserId = async_job->add_mail().sender().profile().user_id();
  //   LogMgr::SendOssLog(E_OSS_LOG_MAIL_USER_SEND_MAIL_FLOW, *oss_tdr);
  // }

  uint64_t ttl_second = PROJECT_NAMESPACE_ID::EN_CL_MAIL_GLOBAL_LEAK_CHECK_TIMEOUT;
  time_t timeout = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                           protobuf_to_system_clock(mail.expired_time()) - ctx.logical_now())
                                           .count());
  if (timeout > 0) {
    ttl_second += static_cast<uint64_t>(timeout);
  }

  auto mail_table = rpc::make_shared_message<PROJECT_NAMESPACE_ID::table_mail_content>(ctx);
  protobuf_move_message(*mail_table->mutable_blob_data()->mutable_mail_content(), std::move(mail));

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::insert(ctx, mail_table, nullptr));

  if (ret < 0) {
    FCTXLOGERROR(ctx, "task {} add_mail_content({}) for user {} failed, res: {}({})", ctx.get_task_context().task_id,
                 mail_record->mail_id(), user_id, ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  // 插入保底TTL，以防泄露
  ret = RPC_AWAIT_CODE_RESULT(rpc::db::mail_content::set_ttl(ctx, mail_table->mail_id(), ttl_second));
  if (ret < 0) {
    FCTXLOGERROR(ctx, "task {} add_mail_content({}) for user {} success but set_ttl failed, res: {}({})",
                 ctx.get_task_context().task_id, mail_record->mail_id(), user_id, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
  }

  FCTXLOGINFO(ctx, "task {} add_mail_content({}) for user {} success", ctx.get_task_context().task_id,
              mail_record->mail_id(), user_id);

  ret = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(ctx, jobs_type, user_id, zone_id, async_job));
  RPC_RETURN_CODE(ret);
}

LOBBY_RPC_API result_code_type add_user_mail_with_template(rpc::context &ctx, int32_t mail_template_id,
                                                           uint32_t zone_id,
                                                           const PROJECT_NAMESPACE_ID::DMailUserInfo &receiver,
                                                           const PROJECT_NAMESPACE_ID::DMailUserInfo *sender,
                                                           bool render_by_client) {
  uint64_t user_id = receiver.profile().user_id();
  // check user_id
  if (user_id == 0 || mail_template_id == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // alloc mail_id first
  PROJECT_NAMESPACE_ID::DMailContent *mail = ctx.create<PROJECT_NAMESPACE_ID::DMailContent>();
  PROJECT_NAMESPACE_ID::DMailContent local_mail;
  if (mail == nullptr) {
    mail = &local_mail;
  }

  // render mail title and content
  int ret = atfw::mail::mail_render_template(ctx, mail_template_id, *mail, receiver, sender, nullptr, render_by_client);
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
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(add_user_mail(ctx, user_id, zone_id, std::move(*mail))));
}

}  // namespace mail
}  // namespace rpc
