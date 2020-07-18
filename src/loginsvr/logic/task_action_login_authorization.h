//
// Created by owent on 2016/10/6.
//

#ifndef LOGIC_ACTION_TASK_ACTION_LOGIN_AUTHORIZATION_H
#define LOGIC_ACTION_TASK_ACTION_LOGIN_AUTHORIZATION_H

#pragma once

#include <utility/environment_helper.h>

#include <dispatcher/task_action_cs_req_base.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>


class task_action_login_authorization : public task_action_cs_req_base {
public:
    typedef task_action_cs_req_base::msg_type      msg_type;
    typedef task_action_cs_req_base::msg_ref_type  msg_ref_type;
    typedef task_action_cs_req_base::msg_cref_type msg_cref_type;

    typedef int (task_action_login_authorization::*auth_fn_t)(const ::hello::CSLoginAuthReq &);

public:
    using task_action_cs_req_base::operator();

public:
    task_action_login_authorization(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~task_action_login_authorization();

    virtual int operator()();

    virtual int on_success();
    virtual int on_failed();

private:
    int32_t check_proto_update(uint32_t iVer);

    auth_fn_t get_verify_fn(uint32_t account_type);

    void init_login_data(hello::table_login &tb, const ::hello::CSLoginAuthReq &req, int64_t player_uid, uint32_t channel_id);

    /**
     * @brief openid加上平台号
     * @param req 请求包
     * @return 最终openid
     */
    std::string make_openid(const hello::CSLoginAuthReq &req);

    /**
     * @brief 校验本地账户
     * @param req 请求包
     * @return 回包的错误码或0
     */
    int verify_plat_account(const ::hello::CSLoginAuthReq &req);

private:
    bool                    is_new_player_;
    uint32_t                strategy_type_;
    uint32_t                zone_id_;
    std::string             version_;
    std::string             final_open_id_;
    uint64_t                final_user_id_;
    hello::table_login      login_data_;
    hello::DClientUpdateCfg update_info_; // 更新信息

    google::protobuf::RepeatedPtrField< ::google::protobuf::string> gamesvr_addrs_; // 登录地址

    static UTIL_ENV_AUTO_SET(std::string) white_skip_openids_;
};


#endif //_LOGIC_ACTION_TASK_ACTION_LOGIN_AUTHORIZATION_H
