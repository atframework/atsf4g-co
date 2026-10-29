// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-06 12:31:24

#include "logic/chat/task_action_chat_send_message.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/user.h>

#include <rpc/compliance/compliance_client_api.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

#include <algorithm>
#include <utility>

#include "logic/chat/user_chat_manager.h"

namespace {
static constexpr char kReplacementText = '*';
}

task_action_chat_send_message::task_action_chat_send_message(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_chat_send_message::~task_action_chat_send_message() {}

const char* task_action_chat_send_message::name() const { return "task_action_chat_send_message"; }

task_action_chat_send_message::result_type task_action_chat_send_message::operator()() {
  rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  atfw::dtmq::DChannelIdKey dtmq_channel_key;
  int32_t response_code = user_inst->get_user_chat_manager().build_dtmq_channel_key_from_chat_channel_key(
      req_body.channel_key(), dtmq_channel_key);

  if (response_code < 0) {
    set_response_code(response_code);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  switch (req_body.content().chat_type_case()) {
    case atfw::chat::DChatMessageContent::kText: {
      // 敏感词过滤
      auto filter_req = rpc::make_shared_message<atfw::compliance::SSFilterTextReq>(get_shared_context());
      auto filter_rsp = rpc::make_shared_message<atfw::compliance::SSFilterTextRsp>(get_shared_context());

      auto* item = filter_req->add_items();
      item->set_text(req_body.content().text());
      item->mutable_settings()->set_return_original_text(false);
      item->mutable_settings()->mutable_replacement_text()->push_back(kReplacementText);
      if (item->text().empty()) {
        set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
        TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
      }

      gsl::string_view text = req_body.content().text();
      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::compliance::filter_text(get_shared_context(), *filter_req, *filter_rsp));
      // 仅当有回包时才处理,服务故障时忽略过滤
      if (res != 0) {
        FCTXLOGWARNING(get_shared_context(), "{} filter text {} failed, res={}({})", *user_inst, text, res,
                       protobuf_mini_dumper_get_error_msg(res));
      }
      if (res == 0 && filter_rsp->results_size() > 0) {
        const auto& filtered = filter_rsp->results(0);
        if (filtered.status() != atfw::compliance::EN_TEXT_FILTER_STATUS_OK) {
          if (filtered.filtered_text().empty()) {
            set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEXT_HAS_DIRTY_WORD);
            TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
          }

          text = filtered.filtered_text();

          // 所有词都是敏感词
          auto iter_not_replacement =
              std::find_if(text.begin(), text.end(), [](char c) { return c != kReplacementText; });
          if (iter_not_replacement == text.end()) {
            set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEXT_HAS_DIRTY_WORD);
            TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
          }
        }
      }

      // 替换敏感词
      if (text.data() != req_body.content().text().data()) {
        req_body.mutable_content()->set_text(text.data(), text.size());
      }
      response_code = RPC_AWAIT_CODE_RESULT(user_inst->get_user_chat_manager().send_chat_message(
          get_shared_context(), dtmq_channel_key, std::move(*req_body.mutable_content())));
      break;
    }
    default:
      response_code = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
  }

  set_response_code(response_code);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_chat_send_message::on_success() { return get_result(); }

int task_action_chat_send_message::on_failed() { return get_result(); }
