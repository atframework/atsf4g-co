//
// Created by owt50 on 2016/9/26.
//

#ifndef DISPATCHER_TASK_ACTION_SS_REQ_BASE_H
#define DISPATCHER_TASK_ACTION_SS_REQ_BASE_H

#pragma once

#include <utility>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include "dispatcher_type_defines.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>

#include "task_action_base.h"

class router_manager_base;
class router_object_base;

class task_action_ss_req_base : public task_action_req_base<hello::SSMsg> {
public:
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    using base_type     = task_action_req_base<hello::SSMsg>;
    using msg_type      = base_type::msg_type;
    using msg_ref_type  = msg_type &;
    using msg_cref_type = const msg_type &;
#else
    typedef task_action_req_base<hello::SSMsg> base_type;
    typedef base_type::msg_type                msg_type;
    typedef msg_type &                         msg_ref_type;
    typedef const msg_type &                   msg_cref_type;
#endif

protected:
    using base_type::get_request;

public:
    using base_type::get_ret_code;
    using base_type::get_rsp_code;
    using base_type::get_task_id;
    using base_type::name;
    using base_type::set_ret_code;
    using base_type::set_rsp_code;
    using base_type::operator();

public:
    task_action_ss_req_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param);
    virtual ~task_action_ss_req_base();

    virtual int hook_run() UTIL_CONFIG_OVERRIDE;

    uint64_t get_request_bus_id() const;

    msg_ref_type add_rsp_msg(uint64_t dst_pd = 0);

    static int32_t            init_msg(msg_ref_type msg, uint64_t dst_pd);
    static int32_t            init_msg(msg_ref_type msg, uint64_t dst_pd, msg_cref_type req_msg);
    static const std::string &get_current_service_name();

    std::shared_ptr<dispatcher_implement> get_dispatcher() const UTIL_CONFIG_OVERRIDE;

protected:
    virtual void send_rsp_msg() UTIL_CONFIG_OVERRIDE;

    virtual bool is_router_offline_ignored() const; // 忽略路由对象不在线

    std::pair<bool, int> filter_router_msg(router_manager_base *&mgr, std::shared_ptr<router_object_base> &obj);

private:
    std::list<msg_type *> rsp_msgs_;
};

template <class TReqType, class TRspType>
class task_action_ss_rpc_base : public task_action_ss_req_base {
public:
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    using base_type         = task_action_ss_req_base;
    using msg_type          = base_type::msg_type;
    using msg_ref_type      = msg_type &;
    using msg_cref_type     = const msg_type &;
    using rpc_request_type  = TReqType;
    using rpc_response_type = TRspType;
#else
    typedef task_action_ss_req_base            base_type;
    typedef base_type::msg_type                msg_type;
    typedef msg_type &                         msg_ref_type;
    typedef const msg_type &                   msg_cref_type;
    typedef TReqType                           rpc_request_type;
    typedef TRspType                           rpc_response_type;
#endif

protected:
    using base_type::get_request;

public:
    using base_type::get_ret_code;
    using base_type::get_rsp_code;
    using base_type::get_task_id;
    using base_type::name;
    using base_type::set_ret_code;
    using base_type::set_rsp_code;
    using base_type::operator();

public:
    task_action_ss_rpc_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param)
        : base_type(COPP_MACRO_STD_MOVE(start_param)), has_unpack_request_(false), has_pack_response_(false), request_body_(nullptr), response_body_(nullptr) {}

    rpc_request_type &get_request_body() {
        // Unpack on first call to get_request_body(), so router transfer need not unpack message body
        if (!has_unpack_request_) {
            unpack_request();
        }

        if (nullptr == request_body_) {
            static rpc_request_type empty_msg;
            empty_msg.Clear();
            return empty_msg;
        }

        return *request_body_;
    }

    rpc_response_type &get_response_body() {
        if (nullptr == response_body_) {
            response_body_ = get_shared_context().template create<rpc_response_type>();
        }

        if (nullptr == response_body_) {
            static rpc_response_type empty_msg;
            empty_msg.Clear();
            return empty_msg;
        }

        return *response_body_;
    }

    static const std::string &get_request_type_url() { return rpc_request_type::descriptor()->full_name(); }

    static const std::string &get_response_type_url() { return rpc_response_type::descriptor()->full_name(); }

    virtual bool is_stream_rpc() const { return get_request().head().has_rpc_stream(); }

protected:
    virtual void send_rsp_msg() UTIL_CONFIG_OVERRIDE {
        if (!has_pack_response_ && !is_stream_rpc()) {
            pack_response();
        }
    }

private:
    void unpack_request() {
        has_unpack_request_ = true;

        request_body_ = get_shared_context().template create<rpc_request_type>();
        if (nullptr == request_body_) {
            return;
        }

        // Check message type
        if (get_request().head().has_rpc_request()) {
            if (get_request_type_url() != get_request().head().rpc_request().type_url()) {
                FWLOGERROR("task {} [{}] except message {}, real got {}", name(), get_task_id(), get_request_type_url().c_str(),
                           get_request().head().rpc_request().type_url().c_str());
            }
        } else if (get_request().head().has_rpc_stream()) {
            if (get_request_type_url() != get_request().head().rpc_stream().type_url()) {
                FWLOGERROR("task {} [{}] except message {}, real got {}", name(), get_task_id(), get_request_type_url().c_str(),
                           get_request().head().rpc_stream().type_url().c_str());
            }
        }

        if (false == request_body_->ParseFromString(get_request().body_bin())) {
            FWLOGERROR("task {} [{}] try to parse message {} failed, msg: {}", name(), get_task_id(), get_request_type_url().c_str(),
                       request_body_->InitializationErrorString().c_str());
        } else {
            FWLOGDEBUG("task {} [{}] parse rpc request message {} success:\n{}", name(), get_task_id(), get_request_type_url().c_str(),
                       protobuf_mini_dumper_get_readable(*request_body_));
        }
    }

    void pack_response() {
        has_pack_response_ = true;

        hello::SSMsg &    rsp  = add_rsp_msg();
        hello::SSMsgHead *head = rsp.mutable_head();
        if (nullptr == head) {
            FWLOGERROR("task {} [{}] pack response but malloc header failed", name(), get_task_id());
            return;
        }

        if (get_request().head().has_rpc_request()) {
            head->clear_rpc_request();
            head->mutable_rpc_response()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            head->mutable_rpc_response()->set_rpc_name(get_request().head().rpc_request().rpc_name());
            head->mutable_rpc_response()->set_type_url(get_response_type_url());
        } else {
            head->clear_rpc_stream();
            head->mutable_rpc_stream()->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            head->mutable_rpc_stream()->set_rpc_name(get_request().head().rpc_stream().rpc_name());
            head->mutable_rpc_stream()->set_type_url(get_response_type_url());

            head->mutable_rpc_stream()->set_caller(get_current_service_name());
            head->mutable_rpc_stream()->set_callee(get_request().head().rpc_stream().caller());
        }

        if (nullptr != response_body_) {
            if (false == response_body_->SerializeToString(rsp.mutable_body_bin())) {
                FWLOGERROR("task {} [{}] try to serialize message {} failed, msg: {}", name(), get_task_id(), get_response_type_url().c_str(),
                           response_body_->InitializationErrorString().c_str());
            } else {
                FWLOGDEBUG("task {} [{}] serialize rpc response message {} success:\n{}", name(), get_task_id(), get_response_type_url().c_str(),
                           protobuf_mini_dumper_get_readable(*response_body_));
            }
        }
    }

private:
    bool               has_unpack_request_;
    bool               has_pack_response_;
    rpc_request_type * request_body_;
    rpc_response_type *response_body_;
};

#endif // ATF4G_CO_TASK_ACTION_SS_REQ_BASE_H
