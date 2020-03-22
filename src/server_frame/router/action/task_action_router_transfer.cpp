//
// Created by owent on 2018/05/01.
//

#include "task_action_router_transfer.h"

#include <config/extern_service_types.h>
#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/logic_config.h>

#include <libatbus_protocol.h>

#include "../router_manager_base.h"
#include "../router_manager_set.h"

task_action_router_transfer::task_action_router_transfer(dispatcher_start_data_t COPP_MACRO_RV_REF param)
    : task_action_ss_req_base(COPP_MACRO_STD_MOVE(param)) {}
task_action_router_transfer::~task_action_router_transfer() {}

int task_action_router_transfer::operator()() {
    msg_cref_type req_msg = get_request();

    // 检查请求包数据是否正确
    if (!req_msg.has_body() || !req_msg.body().has_mss_router_transfer_req()) {
        WLOGERROR("msg body error");
        return hello::err::EN_SYS_PARAM;
    }

    add_rsp_msg().mutable_body()->mutable_mss_router_transfer_rsp();
    const hello::SSRouterTransferReq &req_body = req_msg.body().mss_router_transfer_req();
    router_manager_base *             mgr      = router_manager_set::me()->get_manager(req_body.object().object_type_id());
    if (NULL == mgr) {
        WLOGERROR("router manager %u invalid", req_body.object().object_type_id());
        return hello::err::EN_SUCCESS;
    }

    router_manager_base::key_t          key(req_body.object().object_type_id(), req_body.object().object_zone_id(), req_body.object().object_inst_id());
    std::shared_ptr<router_object_base> obj = mgr->get_base_cache(key);

    // 如果本地版本号更高就不用远程拉取了
    if (obj && obj->get_router_version() >= req_body.object().router_version()) {
        if (logic_config::me()->get_self_bus_id() != obj->get_router_server_id()) {
            set_rsp_code(hello::err::EN_ROUTER_IN_OTHER_SERVER);
        }

        return hello::err::EN_SUCCESS;
    }

    int res = mgr->mutable_object(obj, key, UTIL_CONFIG_NULLPTR);
    set_rsp_code(res);
    return hello::err::EN_SUCCESS;
}

int task_action_router_transfer::on_success() { return get_ret_code(); }

int task_action_router_transfer::on_failed() { return get_ret_code(); }