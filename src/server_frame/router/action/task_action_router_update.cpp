//
// Created by owent on 2018/05/01.
//

#include <log/log_wrapper.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <libatbus_protocol.h>

#include <config/logic_config.h>
#include <time/time_utility.h>

#include "../router_manager_base.h"
#include "../router_manager_set.h"


#include "task_action_router_update.h"

task_action_router_update::task_action_router_update(dispatcher_start_data_t COPP_MACRO_RV_REF param) : task_action_ss_req_base(COPP_MACRO_STD_MOVE(param)) {}
task_action_router_update::~task_action_router_update() {}

int task_action_router_update::operator()() {
    msg_cref_type req_msg = get_request();

    // 检查请求包数据是否正确
    if (!req_msg.body().has_mss_router_update_sync()) {
        WLOGERROR("message dispatcher error");
        return hello::err::EN_SYS_PARAM;
    }

    const hello::SSRouterUpdateSync &req_body = req_msg.body().mss_router_update_sync();
    router_manager_base *            mgr      = router_manager_set::me()->get_manager(req_body.object().object_type_id());
    if (NULL == mgr) {
        WLOGERROR("router manager %u invalid", req_body.object().object_type_id());
        return hello::err::EN_SUCCESS;
    }

    router_manager_base::key_t          key(req_body.object().object_type_id(), req_body.object().object_zone_id(), req_body.object().object_inst_id());
    std::shared_ptr<router_object_base> obj = mgr->get_base_cache(key);
    if (!obj) {
        return hello::err::EN_SUCCESS;
    }

    if (obj->get_router_version() < req_body.object().router_version()) {
        // router_src_bus_id字段是复用的
        obj->set_router_server_id(req_body.object().router_src_bus_id(), req_body.object().router_version());
    }

    return hello::err::EN_SUCCESS;
}

int task_action_router_update::on_success() { return get_ret_code(); }

int task_action_router_update::on_failed() { return get_ret_code(); }
