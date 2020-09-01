
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

    int operator()(atapp::app &app, const atapp::app::message_sender_t& source, const atapp::app::message_t &msg) {
        switch (msg.type) {
        case ::atframe::component::service_type::EN_ATST_GATEWAY: {
            ::atframe::gw::ss_msg req_msg;
            if (false == req_msg.ParseFromArray(msg.data, static_cast<int>(msg.data_size))) {
                FWLOGERROR("receive msg of {} bytes from {:#x} parse failed: {}",
                    msg.data_size, source.id, 
                    req_msg.InitializationErrorString()
                );
                return 0;
            }

            switch (req_msg.body().cmd_case()) {
            case ::atframe::gw::ss_msg_body::kPost: {
                // keep all data not changed and send back
                int res = app.get_bus_node()->send_data(source.id, 0, msg.data, msg.data_size);
                if (res < 0) {
                    WLOGERROR("send back post data to 0x%llx failed, res: %d", 
                        static_cast<unsigned long long>(source.id), res);
                } else {
                    WLOGDEBUG("receive msg %s and send back to 0x%llx done",
                              req_msg.body().post().content().c_str(),
                              static_cast<unsigned long long>(source.id));
                }
                break;
            }
            case ::atframe::gw::ss_msg_body::kAddSession: {
                WLOGINFO("create new session 0x%llx, address: %s:%d", static_cast<unsigned long long>(req_msg.head().session_id()),
                         req_msg.body().add_session().client_ip().c_str(), req_msg.body().add_session().client_port());

                if (0 != req_msg.head().session_id()) {
                    (*gw_)[req_msg.head().session_id()] = source.id;
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
            FWLOGERROR("receive a message of invalid type: {}", msg.type);
            break;
        }

        return 0;
    }
};

static int app_handle_on_forward_response(atapp::app & app, const atapp::app::message_sender_t& source, const atapp::app::message_t & msg, int32_t error_code) {
    if (error_code < 0) {
        FWLOGERROR("send data from {:#x} to {:#x} failed, sequence: {}, code: {}", app.get_id(), source.id, msg.msg_sequence, error_code);
    } else {
        FWLOGINFO("send data from {:#x} to {:#x} got response, sequence: {}, ", app.get_id(), source.id, msg.msg_sequence);
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

    // setup message handle
    app.set_evt_on_forward_request(app_handle_on_msg(&gws));
    app.set_evt_on_forward_response(app_handle_on_forward_response);
    app.set_evt_on_app_connected(app_handle_on_connected);
    app.set_evt_on_app_disconnected(app_handle_on_disconnected);

    // run
    return app.run(uv_default_loop(), argc, (const char **)argv, NULL);
}
