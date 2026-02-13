// Copyright 2021 atframework
// Created by owent

#include <atframe/atapp.h>
#include <common/file_system.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include <time/time_utility.h>

#include <atgateway/protocol/libatgw_server_protocol.h>
#include <config/atframe_service_types.h>

#include <map>

using session_gw_map_t = std::map<uint64_t, uint64_t>;

struct app_command_handler_kickoff {
  atfw::atapp::app *app_;
  session_gw_map_t *gw_;
  app_command_handler_kickoff(atfw::atapp::app *app, session_gw_map_t *gw) : app_(app), gw_(gw) {}
  int operator()(util::cli::callback_param params) {
    if (params.get_params_number() <= 0) {
      FWLOGERROR("kickoff command must require session id");
      return 0;
    }

    uint64_t sess_id = params[0]->to_uint64();
    session_gw_map_t::iterator iter = gw_->find(sess_id);
    if (iter == gw_->end()) {
      FWLOGWARNING("try to kickoff {}, but session not found", sess_id);
      return 0;
    } else {
      FWLOGINFO("kickoff {}", sess_id);
    }

    ::atfw::gateway::server_message msg;
    msg.mutable_head()->set_session_id(sess_id);

    msg.mutable_body()->mutable_kickoff_session();

    std::string packed_buffer;
    if (false == msg.SerializeToString(&packed_buffer)) {
      FWLOGERROR("try to kickoff {} with serialize failed: {}", sess_id, msg.InitializationErrorString());
      return 0;
    }

    return app_->get_bus_node()->send_data(
        iter->second, 0,
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(packed_buffer.data()),
                                       packed_buffer.size()});
  }
};

struct app_handle_on_msg {
  session_gw_map_t *gw_;
  explicit app_handle_on_msg(session_gw_map_t *gw) : gw_(gw) {}

  int operator()(atfw::atapp::app &app, const atfw::atapp::app::message_sender_t &source,
                 const atfw::atapp::app::message_t &msg) {
    switch (msg.type) {
      case static_cast<int32_t>(::atfw::component::service_type::kAtGateway): {
        ::atfw::gateway::server_message req_msg;
        if (false == req_msg.ParseFromArray(reinterpret_cast<const void *>(msg.data.data()),
                                            static_cast<int>(msg.data.size()))) {
          FWLOGERROR("receive msg of {} bytes from {:#x} parse failed: {}", msg.data.size(), source.id,
                     req_msg.InitializationErrorString());
          return 0;
        }

        switch (req_msg.body().cmd_case()) {
          case ::atfw::gateway::server_message_body::kPost: {
            // keep all data not changed and send back
            int res = app.get_bus_node()->send_data(
                source.id, 0,
                gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(msg.data.data()),
                                               msg.data.size()});
            if (res < 0) {
              FWLOGERROR("send back post data to {:#x}({}) failed, res: {}", source.id, source.name, res);
            } else {
              FWLOGDEBUG("receive msg {} and send back to {:#x}({}) done", req_msg.body().post().content(), source.id,
                         source.name);
            }
            break;
          }
          case ::atfw::gateway::server_message_body::kAddSession: {
            FWLOGINFO("create new session {}, address: {}:{}", req_msg.head().session_id(),
                      req_msg.body().add_session().client_ip(), req_msg.body().add_session().client_port());

            if (0 != req_msg.head().session_id()) {
              (*gw_)[req_msg.head().session_id()] = source.id;
            }
            break;
          }
          case ::atfw::gateway::server_message_body::kRemoveSession: {
            FWLOGINFO("remove session {}", req_msg.head().session_id());

            gw_->erase(req_msg.head().session_id());
            break;
          }
          default:
            FWLOGERROR("receive a unsupport atgateway message of invalid cmd: {}",
                       static_cast<int>(req_msg.body().cmd_case()));
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

static int app_handle_on_forward_response(atfw::atapp::app &app, const atfw::atapp::app::message_sender_t &source,
                                          const atfw::atapp::app::message_t &msg, int32_t error_code) {
  if (error_code < 0) {
    FWLOGERROR("send data from {:#x}({}) to {:#x}({}) failed, sequence: {}, code: {}", app.get_id(), app.get_app_name(),
               source.id, source.name, msg.message_sequence, error_code);
  } else {
    FWLOGINFO("send data from {:#x}({}) to {:#x}({}) got response, sequence: {}, ", app.get_id(), app.get_app_name(),
              source.id, source.name, msg.message_sequence);
  }
  return 0;
}

static int app_handle_on_connected(atfw::atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("app {:#x} connected, status: {}", ep.get_id(), status);
  return 0;
}

static int app_handle_on_disconnected(atfw::atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("app {:#x} disconnected, status: {}", ep.get_id(), status);
  return 0;
}

int main(int argc, char *argv[]) {
  atfw::atapp::app app;

  session_gw_map_t gws;

  // project directory
  {
    std::string proj_dir;
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // setup cmd
  atfw::util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
  cmgr->bind_cmd("kickoff", app_command_handler_kickoff(&app, &gws))
      ->set_help_msg("kickoff <session id>                   kickoff a client.");

  // setup message handle
  app.set_evt_on_forward_request(app_handle_on_msg(&gws));
  app.set_evt_on_forward_response(app_handle_on_forward_response);
  app.set_evt_on_app_connected(app_handle_on_connected);
  app.set_evt_on_app_disconnected(app_handle_on_disconnected);

  // run
  return app.run(uv_default_loop(), argc, (const char **)argv, nullptr);
}
