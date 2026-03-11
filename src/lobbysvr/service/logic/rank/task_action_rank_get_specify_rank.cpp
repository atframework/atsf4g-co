#include "task_action_rank_get_specify_rank.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>
#include <utility/rank_util.h>

#include <rank_logic/logic_rank_algorithm.h>

#include <logic/rank/user_rank_manager.h>

#include <data/player.h>

task_action_rank_get_specify_rank::task_action_rank_get_specify_rank(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_rank_get_specify_rank::~task_action_rank_get_specify_rank() {}

const char* task_action_rank_get_specify_rank::name() const { return "task_action_rank_get_specify_rank"; }

task_action_rank_get_specify_rank::result_type task_action_rank_get_specify_rank::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  player_ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  protobuf_copy_message(*rsp_body.mutable_rank_key(), req_body.rank_key());

  if (req_body.rank_key().rank_type() == PROJECT_NAMESPACE_ID::EN_RANK_LOGIC_TYPE_INVALID || req_body.start_no() <= 0 ||
      req_body.count() > rank_util::RANK_GET_TOP_MAX_COUNT) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto rank_rule_cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(req_body.rank_key().rank_type(),
                                                                              req_body.rank_key().rank_instance_id());
  if (!rank_rule_cfg) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INVALID_TYPE);
  }
  
  logic_rank_handle_key rank_key{*rank_rule_cfg};
  if (req_body.rank_key().sub_rank_instance_id() != 0) {
    rank_key.set_sub_instance_id(req_body.rank_key().sub_rank_instance_id());
  }

  uint32_t total_count = 0;
  int32_t res = RPC_AWAIT_CODE_RESULT(user->get_user_rank_manager().get_top_rank(
      get_shared_context(), rank_key, *rank_rule_cfg, *rsp_body.mutable_rank_records(), total_count,
      req_body.start_no(), req_body.count(), true));
  set_response_code(res);
  rsp_body.set_total_count(total_count);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_rank_get_specify_rank::on_success() { return get_result(); }

int task_action_rank_get_specify_rank::on_failed() { return get_result(); }
