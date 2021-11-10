// Copyright 2021 atframework
// Created by owent on 2016/10/6.
//

#include "logic/action/task_action_player_info_get.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <rpc/db/login.h>

#include <data/player.h>
#include <logic/player_manager.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

task_action_player_info_get::task_action_player_info_get(dispatcher_start_data_t &&param)
    : actor_action_cs_req_base(COPP_MACRO_STD_MOVE(param)), rsp_(nullptr) {}
task_action_player_info_get::~task_action_player_info_get() {}

int task_action_player_info_get::operator()() {
  rsp_ = nullptr;

  player::ptr_t user = get_player<player>();
  if (!user) {
    WLOGERROR("not logined.");
    set_response_code(hello::EN_ERR_LOGIN_NOT_LOGINED);
    return 0;
  }

  msg_cref_type req_msg = get_request();
  if (!req_msg.has_body() || !req_msg.body().has_mcs_player_getinfo_req()) {
    WLOGERROR("parameter error");
    set_response_code(hello::EN_ERR_INVALID_PARAM);
    return hello::err::EN_SYS_PARAM;
  }

  const hello::CSPlayerGetInfoReq &req_body = req_msg.body().mcs_player_getinfo_req();

  hello::CSMsg &rsp_msg = get_rsp();
  hello::SCPlayerGetInfoRsp *rsp_body = rsp_msg.mutable_body()->mutable_msc_player_getinfo_rsp();

  // 苹果审核模式
  // bool is_review_mode = user->is_review_mode();

  // 资源
  if (req_body.need_player_info()) {
    // TODO update auto restore

    hello::DPlayerInfo *rsp_item = rsp_body->mutable_player_info();
    protobuf_copy_message(*rsp_item->mutable_player(), user->get_account_info().profile());
    // rsp_item->set_player_level(user->get_player_level());

    // uint32_t player_level_func_bound = user->get_player_level();
    // uint32_t player_vip_level_func_bound = user->get_player_vip_level();

    // TODO 审核版本功能全开
    // if (is_review_mode) {
    //    player_level_func_bound = static_cast<uint32_t>(config_const_parameter_index::me()->get(
    //        hello::config::EN_CPT_PLAYER_MAX_LEVEL));
    //    player_vip_level_func_bound = static_cast<uint32_t>(config_const_parameter_index::me()->get(
    //        hello::config::EN_CPT_PLAYER_MAX_VIP_LEVEL));
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
    protobuf_copy_message(*rsp_body->mutable_player_options(), user->get_player_options().custom_options());
  }

  return hello::err::EN_SUCCESS;
}

int task_action_player_info_get::on_success() { return get_result(); }

int task_action_player_info_get::on_failed() {
  get_rsp();
  return get_result();
}

hello::CSMsg &task_action_player_info_get::get_rsp() {
  if (nullptr == rsp_) {
    rsp_ = &add_rsp_msg();
    rsp_->mutable_body()->mutable_msc_player_getinfo_rsp();
    return *rsp_;
  }

  return *rsp_;
}
