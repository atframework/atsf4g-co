//
// Created by owt50 on 2016/9/26.
//

#include <std/explicit_declare.h>

#include <log/log_wrapper.h>
#include <logic/player_manager.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/ss_msg_dispatcher.h>

#include <config/logic_config.h>

#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>

#include <rpc/router/router_object_base.h>

#include "task_action_ss_req_base.h"

task_action_ss_req_base::task_action_ss_req_base(dispatcher_start_data_t COPP_MACRO_RV_REF start_param) {
    msg_type *ss_msg = ss_msg_dispatcher::me()->get_protobuf_msg<msg_type>(start_param.message);
    if (NULL != ss_msg) {
        get_request().Swap(ss_msg);

        set_player_id(get_request().head().player_user_id());
    }
}

task_action_ss_req_base::~task_action_ss_req_base() {}

int task_action_ss_req_base::hook_run() {
    // 路由对象系统支持
    if (get_request().head().has_router()) {
        std::pair<bool, int> res = filter_router_msg();
        if (false == res.first) {
            return res.second;
        }

        WLOGDEBUG("task %s [0x%llx] receive router message body:\n%s", name(), get_task_id_llu(), protobuf_mini_dumper_get_readable(get_request_body()));
    }

    return base_type::hook_run();
}

uint64_t task_action_ss_req_base::get_request_bus_id() const {
    msg_cref_type msg = get_request();
    return msg.head().bus_id();
}

hello::SSMsgBody &task_action_ss_req_base::get_request_body() {
    hello::SSMsg &req_msg = get_request();
    if (!req_msg.body_bin().empty() && (!req_msg.has_body() || req_msg.body().body_oneof_case() == hello::SSMsgBody::BODY_ONEOF_NOT_SET)) {
        if (false == req_msg.mutable_body()->ParseFromString(req_msg.body_bin())) {
            WLOGERROR("task %s [0x%llx] unpack message body failed, msg: %s", name(), get_task_id_llu(),
                      req_msg.mutable_body()->InitializationErrorString().c_str());
        }
    }

    return *req_msg.mutable_body();
}

task_action_ss_req_base::msg_ref_type task_action_ss_req_base::add_rsp_msg(uint64_t dst_pd) {
    rsp_msgs_.push_back(msg_type());
    msg_ref_type msg = rsp_msgs_.back();

    msg.mutable_head()->set_error_code(get_rsp_code());
    dst_pd = 0 == dst_pd ? get_request_bus_id() : dst_pd;

    init_msg(msg, dst_pd, get_request());
    return msg;
}


int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd) {
    msg.mutable_head()->set_bus_id(dst_pd);
    msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());

    return 0;
}

int32_t task_action_ss_req_base::init_msg(msg_ref_type msg, uint64_t dst_pd, msg_cref_type req_msg) {
    protobuf_copy_message(*msg.mutable_head(), req_msg.head());
    init_msg(msg, dst_pd);

    // set task information
    if (0 != req_msg.head().src_task_id()) {
        msg.mutable_head()->set_dst_task_id(req_msg.head().src_task_id());
    } else {
        msg.mutable_head()->set_dst_task_id(0);
    }

    if (0 != req_msg.head().dst_task_id()) {
        msg.mutable_head()->set_src_task_id(req_msg.head().dst_task_id());
    } else {
        msg.mutable_head()->set_src_task_id(0);
    }

    return 0;
}

void task_action_ss_req_base::send_rsp_msg() {
    if (rsp_msgs_.empty()) {
        return;
    }

    for (std::list<msg_type>::iterator iter = rsp_msgs_.begin(); iter != rsp_msgs_.end(); ++iter) {
        if (0 == (*iter).head().bus_id()) {
            WLOGERROR("task %s [0x%llx] send message to unknown server", name(), get_task_id_llu());
            continue;
        }
        (*iter).mutable_head()->set_error_code(get_rsp_code());

        // send message using ss dispatcher
        int32_t res = ss_msg_dispatcher::me()->send_to_proc((*iter).head().bus_id(), *iter);
        if (res) {
            WLOGERROR("task %s [0x%llx] send message to server 0x%llx failed, res: %d", name(), get_task_id_llu(),
                      static_cast<unsigned long long>((*iter).head().bus_id()), res);
        }
    }

    rsp_msgs_.clear();
}


namespace detail {
    struct filter_router_msg_res_t {
        bool is_on_current_server;
        bool enable_retry;
        int  result;
        inline filter_router_msg_res_t(bool cur, bool retry, int res) : is_on_current_server(cur), enable_retry(retry), result(res) {}
    };

    static int try_fetch_router_cache(router_manager_base &mgr, router_manager_base::key_t key, std::shared_ptr<router_object_base> &obj) {
        int res = 0;
        obj     = mgr.get_base_cache(key);

        // 如果不存在那么实体一定不在这台机器上，但是可能在其他机器上，需要拉取一次确认
        if (!obj) {
            if (!mgr.is_auto_mutable_cache()) {
                return hello::err::EN_ROUTER_NOT_FOUND;
            }
            res = mgr.mutable_cache(obj, key, NULL);
            if (res < 0 || !obj) {
                WLOGERROR("router object %u:%u:0x%llx fetch cache failed, res: %d", key.type_id, key.zone_id, key.object_id_ull(), res);
                return res;
            }
        }

        return res;
    }

    static filter_router_msg_res_t auto_mutable_router_object(uint64_t self_bus_id, router_manager_base &mgr, router_manager_base::key_t key,
                                                              std::shared_ptr<router_object_base> &obj) {
        // 如果开启了自动拉取object，尝试拉取object
        if (!mgr.is_auto_mutable_object()) {
            WLOGINFO("router object key=%u:%u:0x%llx not found and not auto mutable object", key.type_id, key.zone_id, key.object_id_ull());
            return filter_router_msg_res_t(false, false, hello::err::EN_ROUTER_NOT_IN_SERVER);
        }

        int res = mgr.mutable_object(obj, key, NULL);
        if (res < 0) {
            WLOGERROR("router object %u:%u:0x%llx repair object failed, res: %d", key.type_id, key.zone_id, key.object_id_ull(), res);
            // 失败则删除缓存重试
            mgr.remove_cache(key, obj, NULL);

            return filter_router_msg_res_t(false, true, res);
        }

        // Check log
        if (self_bus_id != obj->get_router_server_id()) {
            WLOGERROR("router object %u:%u:0x%llx auto mutable object failed, expect server id 0x%llx, real server id 0x %llx", key.type_id, key.zone_id,
                      key.object_id_ull(), static_cast<unsigned long long>(self_bus_id), static_cast<unsigned long long>(obj->get_router_server_id()));
        }

        return filter_router_msg_res_t(true, true, res);
    }

    static filter_router_msg_res_t check_local_router_object(uint64_t self_bus_id, router_manager_base &mgr, router_manager_base::key_t key,
                                                             std::shared_ptr<router_object_base> &obj) {
        // 路由对象命中当前节点，要开始执行任务逻辑
        if (obj->is_writable()) {
            return filter_router_msg_res_t(true, true, hello::err::EN_SUCCESS);
        }

        // 这里可能是服务器崩溃过，导致数据库记录对象在本机上，但实际上没有。所以这里升级一次做个数据修复
        int res = mgr.mutable_object(obj, key, NULL);
        if (res < 0) {
            WLOGERROR("router object %u:%u:0x%llx repair object failed, res: %d", key.type_id, key.zone_id, key.object_id_ull(), res);
            // 失败则删除缓存重试
            mgr.remove_cache(key, obj, NULL);

            return filter_router_msg_res_t(false, true, res);
        }

        // Check log
        if (self_bus_id != obj->get_router_server_id()) {
            WLOGERROR("router object %u:%u:0x%llx repair object failed, expect server id 0x%llx, real server id 0x %llx", key.type_id, key.zone_id,
                      key.object_id_ull(), static_cast<unsigned long long>(self_bus_id), static_cast<unsigned long long>(obj->get_router_server_id()));
        }

        // 恢复成功，直接开始执行任务逻辑
        return filter_router_msg_res_t(true, true, hello::err::EN_SUCCESS);
    }

    static filter_router_msg_res_t try_filter_router_msg(EXPLICIT_UNUSED_ATTR int retry_times, uint64_t request_bus_id, hello::SSMsg &request_msg,
                                                         router_manager_base &mgr, router_manager_base::key_t key, std::shared_ptr<router_object_base> &obj) {
        obj.reset();

        const hello::SSRouterHead &router = request_msg.head().router();
        int32_t                    res    = try_fetch_router_cache(mgr, key, obj);
        if (res == hello::err::EN_ROUTER_NOT_FOUND) {
            return filter_router_msg_res_t(false, false, res);
        }

        if (!obj) {
            if (res >= 0) {
                res = hello::err::EN_ROUTER_NOT_FOUND;
            }
            return filter_router_msg_res_t(false, false, res);
        }

        // 如果正在迁移，追加到pending队列，本task直接退出
        if (obj->check_flag(router_object_base::flag_t::EN_ROFT_TRANSFERING)) {
            obj->get_transfer_pending_list().push_back(hello::SSMsg());
            obj->get_transfer_pending_list().back().Swap(&request_msg);

            return filter_router_msg_res_t(false, false, hello::err::EN_SUCCESS);
        }

        // 如果本地版本号低于来源服务器，刷新一次路由表。正常情况下这里不可能走到，如果走到了。需要删除缓存再来一次
        if (obj->get_router_version() < router.router_version()) {
            WLOGERROR("router object %u:%u:0x%llx has invalid router version, refresh cache", key.type_id, key.zone_id, key.object_id_ull());
            mgr.remove_cache(key, obj, NULL);
            return filter_router_msg_res_t(false, true, hello::err::EN_ROUTER_NOT_FOUND);
        }

        uint64_t self_bus_id = logic_config::me()->get_self_bus_id();
        // 可能本地缓存的路由信息过期，路由节点返回0的话说明最后一次登记时对象离线了，这时候只能尝试去数据库获取一次新的信息
        if (0 == obj->get_router_server_id() && !mgr.is_auto_mutable_object() && !obj->is_writable()) {
            uint64_t renew_router_server_id = 0;
            uint64_t renew_router_version   = 0;
            res                             = mgr.pull_online_server(key, renew_router_server_id, renew_router_version);
            if (res < 0) {
                return filter_router_msg_res_t(false, true, res);
            }

            if (0 == renew_router_server_id) {
                return filter_router_msg_res_t(false, false, hello::err::EN_ROUTER_NOT_IN_SERVER);
            }

            if (!obj->is_writable() && 0 == obj->get_router_server_id() && renew_router_version > obj->get_router_version()) {
                obj->set_router_server_id(renew_router_server_id, renew_router_version);
            }
        }

        if (0 == obj->get_router_server_id()) {
            filter_router_msg_res_t auto_res = auto_mutable_router_object(self_bus_id, mgr, key, obj);
            if (auto_res.result < 0) {
                return auto_res;
            }
        }

        // 如果和本地的路由缓存匹配则break直接开始消息处理
        if (obj && self_bus_id == obj->get_router_server_id()) {
            return check_local_router_object(self_bus_id, mgr, key, obj);
        }

        // 路由消息转发
        if (obj && 0 != obj->get_router_server_id()) {
            res = mgr.send_msg(*obj, request_msg);

            // 如果路由转发成功，需要禁用掉回包和通知事件，也不需要走逻辑处理了
            if (res < 0) {
                WLOGERROR("try to transfer router object %u:%u:x0%llx to 0x%llx failed, res: %d", key.type_id, key.zone_id, key.object_id_ull(),
                          static_cast<unsigned long long>(obj->get_router_server_id()), res);
            }

            return filter_router_msg_res_t(false, false, res);
        }

        // 这个分支理论上也不会跑到，前面已经枚举了所有流程分支了
        WLOGERROR("miss router object %u:%u:x0%llx prediction code", key.type_id, key.zone_id, key.object_id_ull());
        return filter_router_msg_res_t(false, true, hello::err::EN_ROUTER_NOT_IN_SERVER);
    }
} // namespace detail

std::pair<bool, int> task_action_ss_req_base::filter_router_msg() {
    // request 可能会被move走，所以这里copy一份
    hello::SSRouterHead router;
    protobuf_copy_message(router, get_request().head().router());

    // find router manager in router set
    router_manager_base *mgr = router_manager_set::me()->get_manager(router.object_type_id());
    if (NULL == mgr) {
        WLOGERROR("router manager %u not found", router.object_type_id());
        return std::make_pair(false, hello::err::EN_ROUTER_TYPE_INVALID);
    }

    router_manager_base::key_t key(router.object_type_id(), router.object_zone_id(), router.object_inst_id());

    int                                 retry_times = 0;
    int                                 last_result = 0;
    std::shared_ptr<router_object_base> obj;

    // 最多重试3次，故障恢复过程中可能发生抢占，这时候正常情况下第二次就应该会成功
    while ((++retry_times) <= 3) {
        detail::filter_router_msg_res_t res = detail::try_filter_router_msg(retry_times, get_request_bus_id(), get_request(), *mgr, key, obj);
        if (res.is_on_current_server) {
            return std::make_pair(true, res.result);
        }

        last_result = res.result;

        // 如果路由转发成功或者路由转移期间待处理的消息队列添加成功
        // 需要禁用掉回包和通知事件，也不需要走逻辑处理了
        if (last_result >= 0) {
            disable_rsp_msg();
            disable_finish_evt();
            break;
        }

        // 某些情况下不需要重试
        if (!res.enable_retry) {
            break;
        }
    }

    // 如果本地路由版本号大于来源，通知来源更新路由表
    if (obj && obj->get_router_version() > router.router_version()) {
        hello::SSRouterUpdateSync sync_msg;
        hello::SSRouterHead *     router_head = sync_msg.mutable_object();
        if (NULL != router_head) {
            router_head->set_router_src_bus_id(obj->get_router_server_id());
            router_head->set_router_version(obj->get_router_version());
            router_head->set_object_type_id(key.type_id);
            router_head->set_object_inst_id(key.object_id);
            router_head->set_object_zone_id(key.zone_id);
        }

        // 只通知直接来源
        rpc::router::robj::send_update_sync(get_request_bus_id(), sync_msg);
    }

    // 失败则要回发转发失败
    set_rsp_code(last_result);

    // 如果忽略路由节点不在线,直接返回0即可
    if (hello::err::EN_ROUTER_NOT_IN_SERVER == last_result && is_router_offline_ignored()) {
        last_result = 0;
    }

    if (obj && last_result < 0) {
        obj->send_transfer_msg_failed(COPP_MACRO_STD_MOVE(get_request()));
    }
    return std::make_pair(false, last_result);
}

bool task_action_ss_req_base::is_router_offline_ignored() { return false; }
