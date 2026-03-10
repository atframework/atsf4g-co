#include "task_action_rank_get_special_one_front_back.h"

#include <std/explicit_declare.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/rank_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <logic/rank_manager.h>

RANK_SERVICE_API task_action_rank_get_special_one_front_back::task_action_rank_get_special_one_front_back(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

RANK_SERVICE_API task_action_rank_get_special_one_front_back::~task_action_rank_get_special_one_front_back() {}

RANK_SERVICE_API const char *task_action_rank_get_special_one_front_back::name() const {
  return "task_action_rank_get_special_one_front_back";
}

RANK_SERVICE_API task_action_rank_get_special_one_front_back::result_type task_action_rank_get_special_one_front_back::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  auto rank = rank_manager::me()->get_rank(req_body.rank_key());
  if (!rank || !rank->is_readable()) {
    FWLOGERROR("current node is not readable node rank:({}:{}:{}:{}) node:{}", req_body.rank_key().rank_type(),
               req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(), logic_config::me()->get_local_server_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_IS_NOT_SLAVE);
  }

  auto ret = rank->query_rank_user_front_back(req_body.user_key(), req_body.count(), *rsp_body.mutable_data());
  rsp_body.set_result(ret);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_get_special_one_front_back::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_get_special_one_front_back::on_failed() { return get_result(); }

