// Copyright 2021 atframework
// Created by owent on 2016-10-11.
//

#include "utility/client_simulator.h"

#include <common/file_system.h>
#include <time/time_utility.h>

#include <lua/cpp/lua_engine/lua_engine.h>

#include <config/logic_config.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace detail {
struct on_sys_cmd_lua_run_code {
  client_simulator *owner;
  explicit on_sys_cmd_lua_run_code(client_simulator *s) : owner(s) {}

  void operator()(util::cli::callback_param params) {
    if (!owner->get_lua_engine()) {
      atfw::util::cli::shell_stream ss(std::cerr);
      ss() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "Lua engine disabled." << std::endl;
    } else {
      for (size_t i = 0; i < params.get_params_number(); ++i) {
        owner->get_lua_engine()->run_code(params[i]->to_string());
      }
    }
  }
};

struct on_sys_cmd_lua_run_file {
  client_simulator *owner;
  explicit on_sys_cmd_lua_run_file(client_simulator *s) : owner(s) {}

  void operator()(util::cli::callback_param params) {
    if (!owner->get_lua_engine()) {
      atfw::util::cli::shell_stream ss(std::cerr);
      ss() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "Lua engine disabled." << std::endl;
    } else {
      for (size_t i = 0; i < params.get_params_number(); ++i) {
        owner->get_lua_engine()->run_file(params[i]->to_string());
      }
    }
  }
};
}  // namespace detail

client_simulator::client_simulator() {
  lua_engine_ = ::script::lua::lua_engine::create();
  lua_engine_->init();
}

client_simulator::~client_simulator() {}

void client_simulator::on_start() {
  if (lua_engine_) {
    std::string dirname;
    atfw::util::file_system::dirname(get_exec(), 0, dirname);
    dirname = atfw::util::file_system::get_abs_path(dirname.c_str());
    dirname += atfw::util::file_system::DIRECTORY_SEPARATOR;
    dirname += "lua";
    std::string boostrap_script = (dirname + atfw::util::file_system::DIRECTORY_SEPARATOR) + "main.lua";
    if (::util::file_system::is_exist(boostrap_script.c_str())) {
      lua_engine_->add_search_path(dirname, true);
      lua_engine_->run_file(boostrap_script.c_str());
    } else {
      atfw::util::cli::shell_stream ss(std::cerr);
      ss() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "Lua bootstrap script " << boostrap_script
           << " not found, just skip it." << std::endl;
    }
  }
}

void client_simulator::on_inited() {
  if (lua_engine_) {
    reg_req()["GlobalLua"]["RunCode"].bind(detail::on_sys_cmd_lua_run_code(this),
                                           "[lua code...] run lua code without player");
    reg_req()["GlobalLua"]["RunFile"].bind(detail::on_sys_cmd_lua_run_file(this),
                                           "[lua file...] run lua file without player");
  }
}

uint32_t client_simulator::pick_message_id(const msg_t &) const { return 0; }

std::string client_simulator::pick_message_name(const msg_t &msg) const {
  if (msg.head().has_rpc_response()) {
    return msg.head().rpc_response().rpc_name();
  }

  if (msg.head().has_rpc_stream()) {
    return msg.head().rpc_stream().rpc_name();
  }

  if (msg.head().has_rpc_request()) {
    return msg.head().rpc_request().rpc_name();
  }

  return "";
}

std::string client_simulator::dump_message(const msg_t &msg) {
  std::stringstream ss;
  ss << "Head: " << msg.head().DebugString() << std::endl;
  if (msg.body_bin().empty() == false) {
    const ::google::protobuf::Descriptor *descriptor = nullptr;
    if (msg.head().has_rpc_request()) {
      descriptor = ::google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          msg.head().rpc_request().type_url());
    } else if (msg.head().has_rpc_response()) {
      descriptor = ::google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          msg.head().rpc_response().type_url());
    } else if (msg.head().has_rpc_stream()) {
      descriptor = ::google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          msg.head().rpc_stream().type_url());
    }

    if (nullptr != descriptor) {
      std::unique_ptr<::google::protobuf::Message> message{
          ::google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New()};
      if (message->ParseFromString(msg.body_bin())) {
        ss << "Body: " << message->DebugString() << std::endl;
      } else {
        ss << "Parse body failed: " << message->InitializationErrorString() << std::endl;
      }
    }
  }
  return ss.str();
}

int client_simulator::pack_message(const msg_t &msg, void *buffer, size_t &sz) const {
  size_t msz = msg.ByteSizeLong();
  if (sz < msz) {
    atfw::util::cli::shell_stream ss(std::cerr);
    ss() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "package message require " << msz
         << " bytes, but only has " << sz << " bytes" << std::endl;
    return -1;
  }

  msg.SerializeWithCachedSizesToArray(reinterpret_cast<::google::protobuf::uint8 *>(buffer));
  sz = static_cast<size_t>(msz);
  return 0;
}

int client_simulator::unpack_message(msg_t &msg, const void *buffer, size_t sz) const {
  if (false == msg.ParseFromArray(buffer, static_cast<int>(sz))) {
    atfw::util::cli::shell_stream ss(std::cerr);
    ss() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "unpackage message failed, "
         << msg.InitializationErrorString() << std::endl;
    return -1;
  }

  return 0;
}

int client_simulator::tick() {
  if (lua_engine_) {
    lua_engine_->proc();
  }

  return 0;
}

const PROJECT_NAMESPACE_ID::DConstSettingsType &client_simulator::get_const_settings() {
  static PROJECT_NAMESPACE_ID::DConstSettingsType ret;
  static std::once_flag ret_init_flag;
  std::call_once(ret_init_flag, []() {
    auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("com.const.proto");
    if (nullptr != desc && desc->options().HasExtension(PROJECT_NAMESPACE_ID::CONST_SETTINGS)) {
      ret.CopyFrom(desc->options().GetExtension(PROJECT_NAMESPACE_ID::CONST_SETTINGS));
    }
  });

  return ret;
}

const atframework::ConstSettingsType &client_simulator::get_atframework_settings() {
  static atframework::ConstSettingsType ret;
  static std::once_flag ret_init_flag;
  std::call_once(ret_init_flag, []() {
    auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("atframework.proto");
    if (nullptr != desc && desc->options().HasExtension(atframework::CONST_SETTINGS)) {
      ret.CopyFrom(desc->options().GetExtension(atframework::CONST_SETTINGS));
    }
  });

  return ret;
}

client_simulator *client_simulator::cast(simulator_base *b) { return dynamic_cast<client_simulator *>(b); }

client_simulator::cmd_sender_t &client_simulator::get_cmd_sender(util::cli::callback_param params) {
  return *reinterpret_cast<cmd_sender_t *>(params.get_ext_param());
}

client_simulator::msg_t &client_simulator::add_req(cmd_sender_t &sender) {
  sender.requests.push_back(msg_t());
  msg_t &msg = sender.requests.back();

  msg.mutable_head()->set_error_code(0);
  msg.mutable_head()->set_client_sequence(sender.player->alloc_sequence());
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());
  msg.mutable_head()->set_op_type(PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP);
  return msg;
}

client_simulator::msg_t &client_simulator::add_req(util::cli::callback_param params) {
  return add_req(get_cmd_sender(params));
}
