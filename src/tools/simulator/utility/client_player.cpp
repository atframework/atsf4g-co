//
// Created by owt50 on 2016/10/11.
//

#include <limits.h>

#include <iostream>

#include "client_player.h"
#include "client_simulator.h"
#include <cli/shell_font.h>
#include <libatgw_inner_v1_c.h>
#include <proto_base.h>


#define GTCLI2PLAYER(ctx) (*reinterpret_cast<client_player *>(libatgw_inner_v1_c_get_private_data(ctx)))

// ======================== 以下为协议处理回调 ========================
static int32_t proto_inner_callback_on_write(libatgw_inner_v1_c_context ctx, void *buffer, uint64_t sz, int32_t *is_done) {
    client_player::libuv_ptr_t net = GTCLI2PLAYER(ctx).find_network(ctx);
    if (!net || NULL == buffer) {
        if (NULL != is_done) {
            *is_done = 1;
        }

        return -1;
    }

    int ret = GTCLI2PLAYER(ctx).write_message(net, buffer, sz);
    if (NULL != is_done) {
        // if not writting, notify write finished
        *is_done = (0 != ret) ? 1 : 0;
    }

    return ret;
}

static int proto_inner_callback_on_message(libatgw_inner_v1_c_context ctx, const void *buffer, uint64_t sz) {
    GTCLI2PLAYER(ctx).read_message(buffer, sz);
    return 0;
}

// useless
static int proto_inner_callback_on_new_session(libatgw_inner_v1_c_context ctx, uint64_t *sess_id) {
    printf("create session 0x%llx\n", NULL == sess_id ? 0 : static_cast<unsigned long long>(*sess_id));
    return 0;
}

// useless
static int proto_inner_callback_on_reconnect(libatgw_inner_v1_c_context ctx, uint64_t sess_id) {
    printf("reconnect session 0x%llx\n", static_cast<unsigned long long>(sess_id));
    return 0;
}

static int proto_inner_callback_on_close(libatgw_inner_v1_c_context ctx, int32_t reason) {
    util::cli::shell_stream ss(std::cout);

    if ((reason < 0 || reason > 0x10000) && ::atframe::gateway::close_reason_t::EN_CRT_EOF != reason) {
        GTCLI2PLAYER(ctx).close();
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << "player " << GTCLI2PLAYER(ctx).get_id() << "(" << GTCLI2PLAYER(ctx).get_user_id()
             << ") closed for reason " << reason << std::endl;
    } else {
        GTCLI2PLAYER(ctx).close_net(GTCLI2PLAYER(ctx).find_network(ctx));
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << "player " << GTCLI2PLAYER(ctx).get_id() << "(" << GTCLI2PLAYER(ctx).get_user_id()
             << ") closed for reason " << reason << ", we will reconnect soon." << std::endl;
    }
    return 0;
}

static int proto_inner_callback_on_handshake(libatgw_inner_v1_c_context ctx, int32_t status) {
    GTCLI2PLAYER(ctx).connect_done(ctx);

    if (0 != status) {
        std::stringstream ss_pack;
        ss_pack << "[Error] player " << GTCLI2PLAYER(ctx).get_id() << " handshake failed, status: " << status << std::endl;

        char msg[4096];
        libatgw_inner_v1_c_get_info(ctx, msg, sizeof(msg));
        ss_pack << (char *)msg;

        util::cli::shell_stream ss(std::cerr);
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << ss_pack.str() << std::endl;
        GTCLI2PLAYER(ctx).close_net(GTCLI2PLAYER(ctx).find_network(ctx));
        return -1;
    }

    return 0;
}

static int proto_inner_callback_on_handshake_update(libatgw_inner_v1_c_context ctx, int32_t status) { return 0; }

static int proto_inner_callback_on_error(libatgw_inner_v1_c_context ctx, const char *filename, int line, int errcode, const char *errmsg) {
    util::cli::shell_stream ss(std::cerr);
    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "[Error][" << filename << ":" << line << "]: error code: " << errcode << ", msg: " << errmsg
         << std::endl;
    return 0;
}

void client_player::init_handles() {
    libatgw_inner_v1_c_gset_on_write_start_fn(proto_inner_callback_on_write);
    libatgw_inner_v1_c_gset_on_message_fn(proto_inner_callback_on_message);
    libatgw_inner_v1_c_gset_on_init_new_session_fn(proto_inner_callback_on_new_session);
    libatgw_inner_v1_c_gset_on_init_reconnect_fn(proto_inner_callback_on_reconnect);
    libatgw_inner_v1_c_gset_on_close_fn(proto_inner_callback_on_close);
    libatgw_inner_v1_c_gset_on_handshake_done_fn(proto_inner_callback_on_handshake);
    libatgw_inner_v1_c_gset_on_handshake_update_fn(proto_inner_callback_on_handshake_update);
    libatgw_inner_v1_c_gset_on_error_fn(proto_inner_callback_on_error);
}


client_player::client_player()
    : platform_type_(hello::EN_PTI_WINDOWS), package_version_("255.255.255.255"), resource_version_("255.255.255.255"), protocol_version_("255.255.255.255"),
      user_id_(0), sequence_(0), gamesvr_index_(0), is_connecting_(false) {
    account_.set_account_type(hello::EN_ATI_ACCOUNT_INNER);
    account_.set_channel_id(hello::EN_PCI_NONE);
}

client_player::~client_player() {
    for (std::map<uint32_t, libatgw_inner_v1_c_context>::iterator iter = proto_handles_.begin(); iter != proto_handles_.end(); ++iter) {
        libatgw_inner_v1_c_destroy(iter->second);
    }
    proto_handles_.clear();
}

int client_player::connect(const std::string &host, int port) {
    int ret = simulator_player_impl::connect(host, port);
    if (ret >= 0) {
        is_connecting_ = true;
    }

    return ret;
}

int client_player::on_connected(libuv_ptr_t net, int status) {
    libatgw_inner_v1_c_context proto_handle = mutable_proto_context(net);
    libatgw_inner_v1_c_set_private_data(proto_handle, this);


    std::string all_avail_types;
    uint64_t    type_sz = libatgw_inner_v1_c_global_get_crypt_size();
    for (uint64_t i = 0; i < type_sz; ++i) {
        if (0 != i) {
            all_avail_types += ":";
        }
        all_avail_types += libatgw_inner_v1_c_global_get_crypt_name(i);
    }

    int32_t res = libatgw_inner_v1_c_start_session(proto_handle, all_avail_types.c_str());
    if (res < 0) {
        util::cli::shell_stream ss(std::cerr);
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "start session failed, res" << res << std::endl;
        is_connecting_ = false;
        close_net(net);
        destroy_proto_context(net);
    }

    return res;
}

void client_player::on_alloc(libuv_ptr_t net, size_t suggested_size, uv_buf_t *buf) {
    libatgw_inner_v1_c_context proto_handle = mutable_proto_context(net);
    uint64_t                   len          = static_cast<uint64_t>(buf->len);
    libatgw_inner_v1_c_read_alloc(proto_handle, suggested_size, &buf->base, &len);

#if _MSC_VER
    buf->len = static_cast<ULONG>(len);
#else
    buf->len = static_cast<size_t>(len);
#endif
}

void client_player::on_read_data(libuv_ptr_t net, ssize_t nread, const uv_buf_t *buf) {
    libatgw_inner_v1_c_context proto_handle = mutable_proto_context(net);
    int32_t                    errcode      = 0;
    libatgw_inner_v1_c_read(proto_handle, nread, buf->base, static_cast<uint64_t>(nread), &errcode);
}

void client_player::on_read_message(libuv_ptr_t net, const void *buffer, size_t sz) {
    client_simulator *owner = client_simulator::cast(get_owner());
    if (NULL == owner) {
        util::cli::shell_stream ss(std::cerr);
        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "player " << get_id() << " already removed." << std::endl;
        return;
    }

    read_message(buffer, sz);
}

void client_player::on_written_data(libuv_ptr_t net, int status) {
    libatgw_inner_v1_c_context proto_handle = mutable_proto_context(net);
    libatgw_inner_v1_c_write_done(proto_handle, status);
}

int client_player::on_write_message(libuv_ptr_t net, void *buffer, uint64_t sz) {
    libatgw_inner_v1_c_context proto_handle = mutable_proto_context(net);

    if (!is_connecting_) {
        return libatgw_inner_v1_c_post_msg(proto_handle, buffer, sz);
    } else {
        pending_msg_.push_back(std::vector<unsigned char>());
        pending_msg_.back().assign((unsigned char *)buffer, (unsigned char *)buffer + sz);
        return 0;
    }
}

int client_player::on_disconnected(libuv_ptr_t net) {
    destroy_proto_context(net);
    return 0;
}

void client_player::on_close() {}

void client_player::on_closed() {}

uint32_t client_player::alloc_sequence() { return ++sequence_; }

libatgw_inner_v1_c_context client_player::mutable_proto_context(libuv_ptr_t net) {
    uint32_t id = 0;
    if (net) {
        id = net->id;
    }

    std::map<uint32_t, libatgw_inner_v1_c_context>::iterator iter = proto_handles_.find(id);
    if (iter != proto_handles_.end()) {
        return iter->second;
    }

    libatgw_inner_v1_c_context ret = libatgw_inner_v1_c_create();
    proto_handles_[id]             = ret;
    libatgw_inner_v1_c_set_private_data(ret, this);
    return ret;
}

void client_player::destroy_proto_context(libuv_ptr_t net) {
    uint32_t id = 0;
    if (net) {
        id = net->id;
    }

    std::map<uint32_t, libatgw_inner_v1_c_context>::iterator iter = proto_handles_.find(id);
    if (iter != proto_handles_.end()) {
        libatgw_inner_v1_c_destroy(iter->second);
        proto_handles_.erase(iter);
    }
}

client_player::libuv_ptr_t client_player::find_network(libatgw_inner_v1_c_context ctx) {
    uint32_t id = 0;
    for (std::map<uint32_t, libatgw_inner_v1_c_context>::iterator iter = proto_handles_.begin(); iter != proto_handles_.end(); ++iter) {
        if (iter->second == ctx) {
            id = iter->first;
            break;
        }
    }

    for (std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++iter) {
        if ((*iter) && id == (*iter)->id) {
            return (*iter);
        }
    }

    return NULL;
}

void client_player::connect_done(libatgw_inner_v1_c_context ctx) {
    for (size_t i = 0; i < pending_msg_.size(); ++i) {
        libatgw_inner_v1_c_post_msg(ctx, pending_msg_[i].data(), pending_msg_[i].size());
    }

    pending_msg_.clear();
    is_connecting_ = false;
}