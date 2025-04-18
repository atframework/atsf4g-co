// Copyright 2021 atframework
// Created by owent on 2016-10-11.
//

#include <cli/cmd_option.h>
#include <common/string_oprs.h>
#include <random/random_generator.h>

#include <utility/protobuf_mini_dumper.h>

#include <lua/cpp/lua_engine/lua_binding_utils.h>
#include <lua/cpp/lua_engine/lua_binding_wrapper.h>
#include <lua/cpp/lua_engine/lua_engine.h>

#include <simulator_active.h>
#include <utility/client_config.h>
#include <utility/client_simulator.h>

#include <rpc/gamesvrclientservice/gamesvrclientservice.h>
#include <rpc/loginsvrclientservice/loginsvrclientservice.h>

namespace proto {
namespace player {
void on_cmd_login_auth(util::cli::callback_param params) {
  SIMULATOR_CHECK_PARAMNUM(params, 1);
  client_simulator::cmd_sender_t &sender = client_simulator::get_cmd_sender(params);
  sender.player = sender.self->create_player<client_player>(client_config::host, client_config::port);

  if (!sender.player) {
    SIMULATOR_ERR_MSG() << "create player and try to connect to " << client_config::host << ":" << client_config::port
                        << " failed" << std::endl;
    return;
  }
  sender.player->set_id(params[0]->to_cpp_string());

  // token
  PROJECT_NAMESPACE_ID::CSLoginAuthReq req_body;
  req_body.set_open_id(params[0]->to_cpp_string());
  req_body.set_resource_version(sender.player->get_resource_version());
  req_body.set_package_version(sender.player->get_package_version());
  req_body.set_protocol_version(sender.player->get_protocol_version());

  if (params.get_params_number() > 1) {
    sender.player->set_system_id(params[1]->to_int32());
  }

  if (params.get_params_number() > 2) {
    sender.player->get_account().set_account_type(params[2]->to_int32());
  }

  if (params.get_params_number() > 3) {
    sender.player->get_account().set_access(params[3]->to_cpp_string());
  } else {
    sender.player->get_account().set_access("");
  }

  if (params.get_params_number() > 4) {
    sender.player->set_gamesvr_index(params[4]->to_int32());
  }

  protobuf_copy_message(*req_body.mutable_account(), sender.player->get_account());
  req_body.set_system_id(static_cast<PROJECT_NAMESPACE_ID::EnSystemID>(sender.player->get_system_id()));

  client_simulator::msg_t &msg = client_simulator::add_req(params);
  rpc::loginsvrclientservice::package_login_auth(msg, req_body);
}

void on_rsp_login_auth(client_simulator::player_ptr_t player, client_simulator::msg_t &msg) {
  if (msg.head().error_code() < 0) {
    SIMULATOR_INFO_MSG() << "player " << player->get_id() << " login auth failed, err: " << msg.head().error_code()
                         << std::endl;
    player->close();
    return;
  }

  PROJECT_NAMESPACE_ID::SCLoginAuthRsp rsp_body;
  if (!rsp_body.ParseFromArray(msg.body_bin().data(), msg.body_bin().size())) {
    SIMULATOR_INFO_MSG() << "parse login auth rsp failed, err: " << rsp_body.InitializationErrorString() << std::endl;
    player->close();
    return;
  }

  player->set_id(rsp_body.open_id());
  player->set_login_code(rsp_body.login_code());

  int sz = rsp_body.login_address_size();
  if (sz <= 0) {
    SIMULATOR_INFO_MSG() << "player " << player->get_id() << " login auth failed, has no gamesvr." << std::endl;
    player->close();
    return;
  }

  int index = 0;
  if (0 == player->get_gamesvr_index()) {
    atfw::util::random::mt19937_64 rnd_engine;
    index = rnd_engine.random_between<int>(0, sz);
  } else if (player->get_gamesvr_index() > 0) {
    index = (player->get_gamesvr_index() - 1) % sz;
  } else {
    index = (player->get_gamesvr_index() + sz) % sz;
  }

  player->set_user_id(rsp_body.user_id());
  player->set_gamesvr_addr(rsp_body.login_address(index));

  player->get_owner()->exec_cmd(player, "Player LoginGame");
}

void on_cmd_login(util::cli::callback_param params) {
  SIMULATOR_CHECK_PLAYER_PARAMNUM(params, 0);

  client_simulator::cmd_sender_t &sender = client_simulator::get_cmd_sender(params);
  if (sender.player->get_gamesvr_addr().empty()) {
    SIMULATOR_ERR_MSG() << "player " << sender.player->get_id() << " has no gamesvr address" << std::endl;
    sender.player->close();
    return;
  }

  if (sender.player->get_login_code().empty()) {
    SIMULATOR_ERR_MSG() << "player " << sender.player->get_id() << " has access token" << std::endl;
    sender.player->close();
    return;
  }

  // ==================== 重建连接 =======================
  std::string url = sender.player->get_gamesvr_addr();
  std::string::size_type p = url.find_last_of(":");
  if (p == std::string::npos || p == url.size() - 1) {
    SIMULATOR_ERR_MSG() << "gameurl: " << url << " invalid" << std::endl;
    sender.player->close();
    return;
  }
  std::string gamesvr_ip = url.substr(0, p);
  std::string gamesvr_port = url.substr(p + 1, url.size() - p - 1);
  int port = 0;
  atfw::util::string::str2int(port, gamesvr_port.c_str());
  int res = sender.player->connect(gamesvr_ip.c_str(), port);
  if (res) {
    SIMULATOR_ERR_MSG() << "LoginGame connect to " << gamesvr_ip << ":" << gamesvr_port << " error, ret:" << res
                        << std::endl;
    sender.player->close();
    return;
  }

  PROJECT_NAMESPACE_ID::CSLoginReq req_body;

  req_body.set_login_code(sender.player->get_login_code());
  req_body.set_open_id(sender.player->get_id());
  req_body.set_user_id(sender.player->get_user_id());
  protobuf_copy_message(*req_body.mutable_account(), sender.player->get_account());

  client_simulator::msg_t &req = client_simulator::add_req(params);
  rpc::gamesvrclientservice::package_login(req, req_body);
}

void on_rsp_login(client_simulator::player_ptr_t player, client_simulator::msg_t &msg) {
  SIMULATOR_INFO_MSG() << "player " << player->get_id() << " login finished" << std::endl;

  if (msg.head().error_code() < 0) {
    player->close();
  } else {
    player->get_owner()->set_current_player(player);

    do {
      client_simulator *owner = static_cast<client_simulator *>(player->get_owner());
      if (nullptr == owner) {
        break;
      }
      if (nullptr == owner->get_lua_engine()) {
        break;
      }

      lua_State *L = owner->get_lua_engine()->get_lua_state();
      if (nullptr == L) {
        break;
      }

      ::script::lua::lua_auto_block autoBlock(L);
      if (::script::lua::fn::mutable_env_table(L, player->mutable_lua_env_table().c_str())) {
        lua_pushliteral(L, "player");
        ::script::lua::auto_push(L, player);
        lua_settable(L, -3);
      }
    } while (false);

    if (!client_config::lua_player_code.empty()) {
      player->lua_run_code(client_config::lua_player_code);
    }

    if (!client_config::lua_player_file.empty()) {
      player->lua_run_file(client_config::lua_player_file);
    }
  }
}

void on_cmd_ping(util::cli::callback_param params) {
  SIMULATOR_CHECK_PLAYER_PARAMNUM(params, 0);

  client_simulator::msg_t &req = client_simulator::add_req(params);
  PROJECT_NAMESPACE_ID::CSPingReq req_body;
  rpc::gamesvrclientservice::package_ping(req, req_body);
}

void on_cmd_get_info(util::cli::callback_param params) {
  SIMULATOR_CHECK_PLAYER_PARAMNUM(params, 1);

  const ::google::protobuf::Descriptor *mds = PROJECT_NAMESPACE_ID::CSPlayerGetInfoReq::descriptor();
  int field_count = mds->field_count();

  PROJECT_NAMESPACE_ID::CSPlayerGetInfoReq req_body;

  std::string seg_name;
  const google::protobuf::Reflection *reflet = req_body.GetReflection();
  for (size_t i = 0; i < params.get_params_number(); ++i) {
    if ("all" == params[i]->to_cpp_string()) {
      for (int j = 0; j < field_count; ++j) {
        reflet->SetBool(&req_body, mds->field(j), true);
      }

      break;
    }

    seg_name = "need_";
    seg_name += params[i]->to_cpp_string();

    const google::protobuf::FieldDescriptor *fd = req_body.GetDescriptor()->FindFieldByName(seg_name);
    if (nullptr == fd) {
      SIMULATOR_ERR_MSG() << "player get info segment name " << seg_name << " not found" << std::endl;
    } else {
      if (fd->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        SIMULATOR_ERR_MSG() << "player get info segment name " << seg_name << " is not a bool" << std::endl;
      } else {
        reflet->SetBool(&req_body, fd, true);
      }
    }
  }

  client_simulator::msg_t &req = client_simulator::add_req(params);
  rpc::gamesvrclientservice::package_player_get_info(req, req_body);
}

}  // namespace player
}  // namespace proto

SIMULATOR_ACTIVE(player_account, base) {
  client_simulator::cast(base)->reg_req()["Player"]["Login"].bind(
      proto::player::on_cmd_login_auth,
      "<openid> [platform type=0] [account type=1] [access=''] [use gamesvr=0] login into loginsvr");
  client_simulator::cast(base)->reg_rsp(rpc::loginsvrclientservice::get_full_name_of_login_auth(),
                                        proto::player::on_rsp_login_auth);

  client_simulator::cast(base)->reg_req()["Player"]["LoginGame"].bind(proto::player::on_cmd_login,
                                                                      "login into gamesvr");
  client_simulator::cast(base)->reg_rsp(rpc::gamesvrclientservice::get_full_name_of_login(),
                                        proto::player::on_rsp_login);

  client_simulator::cast(base)->reg_req()["Player"]["Ping"].bind(proto::player::on_cmd_ping, "send ping package");

  client_simulator::cast(base)->reg_req()["Player"]["GetInfo"].bind(proto::player::on_cmd_get_info,
                                                                    "[segments...] get player data");
  // special auto complete for GetInfo
  {
    // 通过protobuf反射的智能补全设置
    client_simulator::cast(base)->reg_req()["Player"]["GetInfo"]["all"];
    const ::google::protobuf::Descriptor *mds = PROJECT_NAMESPACE_ID::CSPlayerGetInfoReq::descriptor();
    int field_count = mds->field_count();

    for (int i = 0; i < field_count; ++i) {
      client_simulator::cast(base)->reg_req()["Player"]["GetInfo"][mds->field(i)->name().substr(5)];
    }
  }
}
