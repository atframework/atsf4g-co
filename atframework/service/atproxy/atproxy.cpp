
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <std/ref.h>
#include <vector>

#include <uv.h>

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <time/time_utility.h>

#include "atproxy_manager.h"

static int app_handle_on_send_fail(atapp::app &, atapp::app::app_id_t src_pd, atapp::app::app_id_t dst_pd, const atbus::protocol::msg &) {
    WLOGERROR("send data from 0x%llx to 0x%llx failed", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    return 0;
}

struct app_handle_on_connected {
    std::reference_wrapper<atframe::proxy::atproxy_manager> atproxy_mgr_module;
    app_handle_on_connected(atframe::proxy::atproxy_manager &mod) : atproxy_mgr_module(mod) {}

    int operator()(atapp::app &app, atbus::endpoint &ep, int status) {
        WLOGINFO("node 0x%llx connected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);

        atproxy_mgr_module.get().on_connected(app, ep.get_id());
        return 0;
    }
};

struct app_handle_on_disconnected {
    std::reference_wrapper<atframe::proxy::atproxy_manager> atproxy_mgr_module;
    app_handle_on_disconnected(atframe::proxy::atproxy_manager &mod) : atproxy_mgr_module(mod) {}

    int operator()(atapp::app &app, atbus::endpoint &ep, int status) {
        WLOGINFO("node 0x%llx disconnected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);

        atproxy_mgr_module.get().on_disconnected(app, ep.get_id());
        return 0;
    }
};

int main(int argc, char *argv[]) {
    atapp::app                                       app;
    std::shared_ptr<atframe::component::etcd_module> etcd_mod = std::make_shared<atframe::component::etcd_module>();
    if (!etcd_mod) {
        fprintf(stderr, "create etcd module failed\n");
        return -1;
    }

    std::shared_ptr<atframe::proxy::atproxy_manager> proxy_mgr_mod = std::make_shared<atframe::proxy::atproxy_manager>(etcd_mod);
    if (!proxy_mgr_mod) {
        fprintf(stderr, "create atproxy manager module failed\n");
        return -1;
    }

    // project directory
    {
        std::string proj_dir;
        util::file_system::dirname(__FILE__, 0, proj_dir, 4);
        util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
    }

    // setup module
    app.add_module(etcd_mod);
    app.add_module(proxy_mgr_mod);

    // setup message handle
    app.set_evt_on_forward_response(app_handle_on_send_fail);
    app.set_evt_on_app_connected(app_handle_on_connected(*proxy_mgr_mod));
    app.set_evt_on_app_disconnected(app_handle_on_disconnected(*proxy_mgr_mod));

    // run
    return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
