// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:33:44

#include "logic/player/task_action_player_get_info.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>

task_action_player_get_info::task_action_player_get_info(dispatcher_start_data_t&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_player_get_info::~task_action_player_get_info() {}

const char* task_action_player_get_info::name() const { return "task_action_player_get_info"; }

task_action_player_get_info::result_type task_action_player_get_info::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  // 苹果审核模式
  // bool is_review_mode = user->is_review_mode();

  // 资源
  if (req_body.need_player_info()) {
    // TODO(owent) update auto restore

    PROJECT_SERVER_FRAME_NAMESPACE_ID::DPlayerInfo* rsp_item = rsp_body.mutable_player_info();
    protobuf_copy_message(*rsp_item->mutable_player(), user->get_account_info().profile());
    // rsp_item->set_player_level(user->get_player_level());

    // uint32_t player_level_func_bound = user->get_player_level();
    // uint32_t player_vip_level_func_bound = user->get_player_vip_level();

    // TODO(owent) 审核版本功能全开
    // if (is_review_mode) {
    //    player_level_func_bound = static_cast<uint32_t>(config_const_parameter_index::me()->get(
    //        PROJECT_SERVER_FRAME_NAMESPACE_ID::config::EN_CPT_PLAYER_MAX_LEVEL));
    //    player_vip_level_func_bound = static_cast<uint32_t>(config_const_parameter_index::me()->get(
    //        PROJECT_SERVER_FRAME_NAMESPACE_ID::config::EN_CPT_PLAYER_MAX_VIP_LEVEL));
    //}

    //// 额外下发依靠等级解锁的功能
    // for (uint32_t i = 0; i <= player_level_func_bound; ++i) {
    //    const std::vector<uint32_t> *cfg = config_player_index::me()->get_player_unlock(i);
    //    for (size_t j = 0; nullptr != cfg && j < cfg->size(); ++j) {
    //        if ((*cfg)[j] > 0) {
    //            moyo_no1::DPlayerLimit *limit = rsp_item->add_limits();
    //            if (nullptr != limit) {
    //                limit->set_function_id((*cfg)[j]);
    //                limit->set_limit_number(1);
    //            }
    //        }
    //    }
    //}
  }

  // 自定义选项
  if (req_body.need_player_options()) {
    protobuf_copy_message(*rsp_body.mutable_player_options(), user->get_player_options().custom_options());
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_player_get_info::on_success() { return get_result(); }

int task_action_player_get_info::on_failed() { return get_result(); }
