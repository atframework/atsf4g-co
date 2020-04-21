
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>


#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <libatgw_server_protocol.h>


#include <config/excel/config_manager.h>
#include <config/logic_config.h>

#include <logic/session_manager.h>
#include <router/router_manager_set.h>
#include <router/router_player_manager.h>

#include "handle_cs_msg.h"
#include "handle_ss_msg.h"

#include <data/player.h>

#ifdef _MSC_VER

#define INIT_CALL(MOD_NAME, ...)                                        \
    {                                                                   \
        int res = MOD_NAME::me()->init(__VA_ARGS__);                    \
        if (res < 0) {                                                  \
            WLOGERROR("initialize %s failed, res: %d", #MOD_NAME, res); \
            return res;                                                 \
        }                                                               \
    }

#define RELOAD_CALL(RET_VAR, MOD_NAME, ...)                         \
    {                                                               \
        int res = MOD_NAME::me()->reload(__VA_ARGS__);              \
        if (res < 0) {                                              \
            WLOGERROR("reload %s failed, res: %d", #MOD_NAME, res); \
            RET_VAR = res;                                          \
        }                                                           \
    }

#else
#define INIT_CALL(MOD_NAME, args...)                                    \
    {                                                                   \
        int res = MOD_NAME::me()->init(args);                           \
        if (res < 0) {                                                  \
            WLOGERROR("initialize %s failed, res: %d", #MOD_NAME, res); \
            return res;                                                 \
        }                                                               \
    }

#define RELOAD_CALL(RET_VAR, MOD_NAME, args...)                     \
    {                                                               \
        int res = MOD_NAME::me()->reload(args);                     \
        if (res < 0) {                                              \
            WLOGERROR("reload %s failed, res: %d", #MOD_NAME, res); \
            RET_VAR = res;                                          \
        }                                                           \
    }

#endif

struct app_handle_on_msg {
    app_handle_on_msg() {}

    int operator()(atapp::app &app, const atapp::app::msg_t &msg, const void *buffer, size_t len) {
        if (atbus::protocol::msg::kDataTransformReq != msg.msg_body_case() || 0 == msg.head().src_bus_id()) {
            WLOGERROR("receive a message from unknown source or invalid body case");
            return app.get_bus_node()->send_data(msg.head().src_bus_id(), msg.head().type(), buffer, len);
        }

        int ret = 0;
        switch (msg.head().type()) {
        case ::atframe::component::service_type::EN_ATST_GATEWAY: {
            ret = cs_msg_dispatcher::me()->dispatch(msg, buffer, len);
            break;
        }

        case ::atframe::component::message_type::EN_ATST_SS_MSG: {
            ret = ss_msg_dispatcher::me()->dispatch(msg, buffer, len);
            break;
        }

        default: {
            WLOGERROR("receive a message of invalid type:%d", msg.head().type());
            break;
        }
        }

        return ret;
    }
};

static int app_handle_on_receive_send_response(atapp::app &app, atapp::app::app_id_t src_pd, atapp::app::app_id_t dst_pd, const atapp::app::msg_t &msg) {
    if (msg.head().ret() < 0) {
        WLOGERROR("send data from 0%llx to 0x%llx failed", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    } else {
        WLOGDEBUG("receive response for sending data from 0%llx to 0x%llx", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    }

    int ret = 0;
    switch (msg.head().type()) {
    case ::atframe::component::message_type::EN_ATST_SS_MSG: {
        ret = ss_msg_dispatcher::me()->on_receive_send_data_response(msg);
        break;
    }

    default: { break; }
    }

    return ret;
}

static int app_handle_on_connected(atapp::app &app, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx connected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}

static int app_handle_on_disconnected(atapp::app &app, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx disconnected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}


class main_service_module : public atapp::module_impl {
private:
    static router_player_cache::object_ptr_t create_player_fn(uint64_t user_id, uint32_t zone_id, const std::string &openid) {
        return std::static_pointer_cast<player_cache>(player::create(user_id, zone_id, openid));
    }

public:
    virtual int init() {
        stop_log_tm_ = 0;
        WLOGINFO("============ server initialize ============");
        INIT_CALL(logic_config, get_app()->get_id());
        INIT_CALL(excel::config_manager);

        // logic managers
        INIT_CALL(task_manager);
        INIT_CALL(session_manager);

        {
            INIT_CALL(router_manager_set);
            // register all router managers
            router_player_manager::me();
        }

        // register handles
        INIT_CALL(app_handle_ss_msg);
        INIT_CALL(app_handle_cs_msg);

        // reload will be triggered before init, so reload again here

        // setup how to create player
        router_player_manager::me()->set_create_object_fn(create_player_fn);

        return excel::config_manager::me()->reload_all();
    };

    virtual int reload() {
        WLOGINFO("============ server reload ============");
        int                       ret = 0;
        util::config::ini_loader &cfg = get_app()->get_configure();

        RELOAD_CALL(ret, logic_config, cfg);
        ret = excel::config_manager::me()->reload_all();

        RELOAD_CALL(ret, task_manager);

        return ret;
    }

    virtual int stop() {
        time_t now = util::time::time_utility::get_sys_now();
        if (now != stop_log_tm_) {
            stop_log_tm_ = now;
            WLOGINFO("============ server stop ============");
        }
        int ret = router_manager_set::me()->stop();
        if (ret < 0) {
            WLOGERROR("router_manager_set stop failed, res: %d", ret);
            return ret;
        }

        // 保存任务未完成，需要继续等待
        if (!router_manager_set::me()->is_closed()) {
            ret = 1;
        }

        return ret;
    }

    virtual int timeout() {
        WLOGINFO("============ server timeout ============");
        router_manager_set::me()->force_close();
        return 0;
    }

    virtual const char *name() const { return "main_service_module"; }

    virtual int tick() {
        int ret = 0;
        ret += session_manager::me()->proc();
        ret += task_manager::me()->tick(util::time::time_utility::get_sys_now(), 1000 * util::time::time_utility::get_now_usec());
        ret += router_manager_set::me()->tick();

        return ret;
    }

private:
    time_t stop_log_tm_;
};

int main(int argc, char *argv[]) {
    atapp::app app;

    // project directory
    {
        std::string proj_dir;
        util::file_system::dirname(__FILE__, 0, proj_dir, 4);
        util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
    }

    app.add_module(std::make_shared<main_service_module>());
    app.add_module(cs_msg_dispatcher::me());
    app.add_module(ss_msg_dispatcher::me());
    app.add_module(db_msg_dispatcher::me());

    // setup message handle
    app.set_evt_on_recv_msg(app_handle_on_msg());
    app.set_evt_on_forward_response(app_handle_on_receive_send_response);
    app.set_evt_on_app_connected(app_handle_on_connected);
    app.set_evt_on_app_disconnected(app_handle_on_disconnected);

    // run
    return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
