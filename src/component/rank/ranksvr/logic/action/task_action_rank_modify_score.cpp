#include "task_action_rank_modify_score.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include "logic/rank_manager.h"

RANK_SERVICE_API task_action_rank_modify_score::task_action_rank_modify_score(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_modify_score::~task_action_rank_modify_score() {}

RANK_SERVICE_API const char* task_action_rank_modify_score::name() const { return "task_action_rank_modify_score"; }

RANK_SERVICE_API task_action_rank_modify_score::result_type task_action_rank_modify_score::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(rank_manager::me()->modify_score(
      get_shared_context(), req_body.rank_key(), req_body.data().user_key(), req_body.data().score(), req_body.data().custom_data()));
  rsp_body.set_result(ret);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_modify_score::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_modify_score::on_failed() { return get_result(); }
