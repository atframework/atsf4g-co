//
// Created by owt50 on 2016/10/11.
//

#include "client_simulator.h"
#include <time/time_utility.h>


client_simulator::~client_simulator() {}

uint32_t client_simulator::pick_message_id(const msg_t &msg) const {
    if (false == msg.has_body()) {
        return 0;
    }

    std::vector<const google::protobuf::FieldDescriptor *> output;
    msg.body().GetReflection()->ListFields(msg.body(), &output);
    if (output.empty()) {
        return 0;
    }

    return static_cast<uint32_t>(output[0]->number());
}

std::string client_simulator::pick_message_name(const msg_t &msg) const {
    if (false == msg.has_body()) {
        return 0;
    }

    std::vector<const google::protobuf::FieldDescriptor *> output;
    msg.body().GetReflection()->ListFields(msg.body(), &output);
    if (output.empty()) {
        return 0;
    }

    return output[0]->name();
}

std::string client_simulator::dump_message(const msg_t &msg) { return msg.DebugString(); }

int client_simulator::pack_message(const msg_t &msg, void *buffer, size_t &sz) const {
    size_t msz = msg.ByteSizeLong();
    if (sz < msz) {
        util::cli::shell_stream ss(std::cerr);
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "package message require " << msz << " bytes, but only has " << sz << " bytes"
             << std::endl;
        return -1;
    }

    msg.SerializeWithCachedSizesToArray(reinterpret_cast< ::google::protobuf::uint8 *>(buffer));
    sz = static_cast<size_t>(msz);
    return 0;
}

int client_simulator::unpack_message(msg_t &msg, const void *buffer, size_t sz) const {
    if (false == msg.ParseFromArray(buffer, static_cast<int>(sz))) {
        util::cli::shell_stream ss(std::cerr);
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "unpackage message failed, " << msg.InitializationErrorString() << std::endl;
        return -1;
    }

    return 0;
}

int client_simulator::tick() { return 0; }

client_simulator *client_simulator::cast(simulator_base *b) { return dynamic_cast<client_simulator *>(b); }


client_simulator::cmd_sender_t &client_simulator::get_cmd_sender(util::cli::callback_param params) {
    return *reinterpret_cast<cmd_sender_t *>(params.get_ext_param());
}

client_simulator::msg_t &client_simulator::add_req(cmd_sender_t &sender) {
    sender.requests.push_back(msg_t());
    msg_t &msg = sender.requests.back();

    msg.mutable_head()->set_error_code(0);
    msg.mutable_head()->set_sequence(sender.player->alloc_sequence());
    msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());
    return msg;
}

client_simulator::msg_t &client_simulator::add_req(util::cli::callback_param params) { return add_req(get_cmd_sender(params)); }