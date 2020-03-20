
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>

#include <config/atframe_service_types.h>
#include <libatgw_server_protocol.h>

#include <modules/etcd_module.h>

typedef std::map<uint64_t, uint64_t> session_gw_map_t;

struct app_command_handler_kickoff {
    atapp::app *      app_;
    session_gw_map_t *gw_;
    app_command_handler_kickoff(atapp::app *app, session_gw_map_t *gw) : app_(app), gw_(gw) {}
    int operator()(util::cli::callback_param params) {
        if (params.get_params_number() <= 0) {
            WLOGERROR("kickoff command must require session id");
            return 0;
        }

        uint64_t                   sess_id = params[0]->to_uint64();
        session_gw_map_t::iterator iter    = gw_->find(sess_id);
        if (iter == gw_->end()) {
            WLOGWARNING("try to kickoff 0x%llx, but session not found", static_cast<unsigned long long>(sess_id));
            return 0;
        } else {
            WLOGINFO("kickoff 0x%llx", static_cast<unsigned long long>(sess_id));
        }

        ::atframe::gw::ss_msg msg;
        msg.mutable_head()->set_session_id(sess_id);

        msg.mutable_body()->mutable_kickoff_session();

        std::string packed_buffer;
        if(false == msg.SerializeToString(&packed_buffer)) {
            WLOGERROR("try to kickoff %llx with serialize failed: %s", 
            static_cast<unsigned long long>(sess_id), msg.InitializationErrorString().c_str());
            return 0;
        }

        return app_->get_bus_node()->send_data(iter->second, 0, packed_buffer.data(), packed_buffer.size());
    }
};

struct app_handle_on_msg {
    session_gw_map_t *gw_;
    app_handle_on_msg(session_gw_map_t *gw) : gw_(gw) {}

    int operator()(atapp::app &app, const atapp::app::msg_t &msg, const void *buffer, size_t len) {
        if (atbus::protocol::msg::kDataTransformReq != msg.msg_body_case()) {
            WLOGERROR("receive msg with %llu bytes from x0%llx: %s",
                static_cast<unsigned long long>(len), static_cast<unsigned long long>(msg.head().src_bus_id()), 
                msg.DebugString().c_str()
            );
            return 0;
        }

        if (0 == msg.head().src_bus_id()) {
            WLOGERROR("receive a message from unknown source");
            return app.get_bus_node()->send_data(msg.head().src_bus_id(), msg.head().type(), buffer, len);
        }

        switch (msg.head().type()) {
        case ::atframe::component::service_type::EN_ATST_GATEWAY: {
            ::atframe::gw::ss_msg req_msg;
            if (false == req_msg.ParseFromArray(reinterpret_cast<const void*>(buffer), static_cast<int>(len))) {
                WLOGERROR("receive msg of %llu bytes from x0%llx parse failed: %s",
                    static_cast<unsigned long long>(len), static_cast<unsigned long long>(msg.head().src_bus_id()), 
                    req_msg.InitializationErrorString().c_str()
                );
                return 0;
            }

            switch (req_msg.body().cmd_case()) {
            case ::atframe::gw::ss_msg_body::kPost: {
                // keep all data not changed and send back
                int res = app.get_bus_node()->send_data(msg.data_transform_req().from(), 0, buffer, len);
                if (res < 0) {
                    WLOGERROR("send back post data to 0x%llx failed, res: %d", 
                        static_cast<unsigned long long>(msg.data_transform_req().from()), res);
                } else {
                    WLOGDEBUG("receive msg %s and send back to 0x%llx done",
                              req_msg.body().post().content().c_str(),
                              static_cast<unsigned long long>(msg.data_transform_req().from()));
                }
                break;
            }
            case ::atframe::gw::ss_msg_body::kAddSession: {
                WLOGINFO("create new session 0x%llx, address: %s:%d", static_cast<unsigned long long>(req_msg.head().session_id()),
                         req_msg.body().add_session().client_ip().c_str(), req_msg.body().add_session().client_port());

                if (0 != req_msg.head().session_id()) {
                    (*gw_)[req_msg.head().session_id()] = msg.data_transform_req().from();
                }
                break;
            }
            case ::atframe::gw::ss_msg_body::kRemoveSession: {
                WLOGINFO("remove session 0x%llx", static_cast<unsigned long long>(req_msg.head().session_id()));

                gw_->erase(req_msg.head().session_id());
                break;
            }
            default:
                WLOGERROR("receive a unsupport atgateway message of invalid cmd:%d", static_cast<int>(req_msg.body().cmd_case()));
                break;
            }

            break;
        }

        default:
            WLOGERROR("receive a message of invalid type:%d", msg.head().type());
            break;
        }

        return 0;
    }
};

static int app_handle_on_forward_response(atapp::app &, atapp::app::app_id_t src_pd, atapp::app::app_id_t dst_pd, const atbus::protocol::msg &m) {
    if (m.head().ret() < 0) {
        WLOGERROR("send data from 0x%llx to 0x%llx failed", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    } else {
        WLOGINFO("send data from 0x%llx to 0x%llx got response", static_cast<unsigned long long>(src_pd), static_cast<unsigned long long>(dst_pd));
    }
    return 0;
}

static int app_handle_on_connected(atapp::app &, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx connected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}

static int app_handle_on_disconnected(atapp::app &, atbus::endpoint &ep, int status) {
    WLOGINFO("app 0x%llx disconnected, status: %d", static_cast<unsigned long long>(ep.get_id()), status);
    return 0;
}

int main(int argc, char *argv[]) {
    atapp::app                                       app;
    std::shared_ptr<atframe::component::etcd_module> etcd_mod = std::make_shared<atframe::component::etcd_module>();
    if (!etcd_mod) {
        fprintf(stderr, "create etcd module failed\n");
        return -1;
    }

    session_gw_map_t gws;

    // project directory
    {
        std::string proj_dir;
        util::file_system::dirname(__FILE__, 0, proj_dir, 4);
        util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
    }

    // setup cmd
    util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
    cmgr->bind_cmd("kickoff", app_command_handler_kickoff(&app, &gws))->set_help_msg("kickoff <session id>                   kickoff a client.");

    // setup module
    app.add_module(etcd_mod);

    // setup message handle
    app.set_evt_on_recv_msg(app_handle_on_msg(&gws));
    app.set_evt_on_forward_response(app_handle_on_forward_response);
    app.set_evt_on_app_connected(app_handle_on_connected);
    app.set_evt_on_app_disconnected(app_handle_on_disconnected);

    // run
    return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
