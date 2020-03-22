//
// Created by owent on 2018/05/01.
//

#include <config/logic_config.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <log/log_wrapper.h>

#include "router_manager_base.h"
#include "router_manager_set.h"
#include "router_object_base.h"


router_manager_base::router_manager_base(uint32_t type_id) : stat_size_(0), type_id_(type_id), is_closing_(false) {
    router_manager_set::me()->register_manager(this);
}
router_manager_base::~router_manager_base() { router_manager_set::me()->unregister_manager(this); }

bool router_manager_base::is_auto_mutable_object() const { return false; }

bool router_manager_base::is_auto_mutable_cache() const { return true; }

uint64_t router_manager_base::get_default_router_server_id(const router_object_base &router_cache) const { return 0; }

int router_manager_base::send_msg(router_object_base &obj, hello::SSMsg &msg) {
    // 如果正在转移过程中，追加到pending列表
    if (obj.check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
        obj.get_transfer_pending_list().push_back(hello::SSMsg());
        obj.get_transfer_pending_list().back().Swap(&msg);
    }

    return send_msg_raw(obj, msg);
}

int router_manager_base::send_msg(const key_t &key, hello::SSMsg &msg) {
    int                                 res = 0;
    std::shared_ptr<router_object_base> obj;
    res = mutable_cache(obj, key, NULL);
    if (res < 0) {
        return res;
    }

    if (!obj) {
        return hello::err::EN_ROUTER_NOT_FOUND;
    }

    return send_msg(*obj, msg);
}

int router_manager_base::send_msg_raw(router_object_base &obj, hello::SSMsg &msg) {
    // 这里必须直接发送

    hello::SSRouterHead *router_head = msg.mutable_head()->mutable_router();
    router_head->set_router_src_bus_id(logic_config::me()->get_self_bus_id()); // 源BUS ID是自己
    router_head->set_router_version(obj.get_router_version());
    router_head->set_object_type_id(obj.get_key().type_id);
    router_head->set_object_inst_id(obj.get_key().object_id);
    router_head->set_object_zone_id(obj.get_key().zone_id);
    //

    // 如果路由节点为0，可能是缓存过期，尝试拉取一次
    if (0 == obj.get_router_server_id()) {
        obj.pull_cache_inner(NULL);
    }

    if (0 == obj.get_router_server_id()) {
        WLOGERROR("router object (type=%u) %u:%u:0x%llx has no valid router server", get_type_id(), obj.get_key().type_id, obj.get_key().zone_id,
                  static_cast<unsigned long long>(obj.get_key().object_id_ull()));
        return hello::err::EN_ROUTER_NOT_IN_SERVER;
    }

    // 尝试打包数据到二进制body，这样如果目标服务器进程是转发消息则不需要解包
    if (msg.has_body() && msg.body().body_oneof_case() != hello::SSMsgBody::BODY_ONEOF_NOT_SET) {
        router_head->set_message_type(msg.body().body_oneof_case());

        msg.body().SerializeAsString().swap(*msg.mutable_body_bin());
        msg.clear_body();
    }

    return ss_msg_dispatcher::me()->send_to_proc(obj.get_router_server_id(), msg);
}

void router_manager_base::on_stop() { is_closing_ = false; }

int router_manager_base::pull_online_server(const key_t &, uint64_t &router_svr_id, uint64_t &router_svr_ver) {
    router_svr_id  = 0;
    router_svr_ver = 0;
    return 0;
}
