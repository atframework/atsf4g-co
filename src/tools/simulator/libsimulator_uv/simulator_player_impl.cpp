//
// Created by owt50 on 2016/10/9.
//

#include <iostream>
#include <cstring>
#include <assert.h>

#include <cli/shell_font.h>
#include <time/time_utility.h>

#include "simulator_base.h"
#include "simulator_player_impl.h"

simulator_player_impl::simulator_player_impl(): is_closing_(false), owner_(NULL), network_id_(0) {
}

simulator_player_impl::~simulator_player_impl() {
    assert(NULL == owner_);
    util::cli::shell_stream ss(std::cerr);
    ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_GREEN
        << "player "<< id_ << " destroyed"<< std::endl;
}

bool simulator_player_impl::set_id(const std::string& id) {
    if (id_ == id) {
        return true;
    }

    if (NULL == owner_) {
        id_ = id;
        return true;
    }

    std::string old_id = id;
    old_id.swap(id_);
    if(!id_.empty() && false == owner_->insert_player(watcher_.lock())) {
        id_.swap(old_id);
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD
            << "insert player "<<id << " failed"<< std::endl;

        return false;
    }

    if (!old_id.empty()) {
        owner_->remove_player(old_id, false);
    }

    return true;
}


// ================== receive ===================
void simulator_player_impl::libuv_on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    simulator_player_impl* self = reinterpret_cast<simulator_player_impl*>(handle->data);
    assert(self);

    libuv_ptr_t net = self->find_network((uv_tcp_t*)handle);
    if (net) {
        self->on_alloc(net, suggested_size, buf);
    }
}

void simulator_player_impl::libuv_on_read_data(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    util::time::time_utility::update(NULL);

    simulator_player_impl* self = reinterpret_cast<simulator_player_impl*>(stream->data);
    assert(self);

    // if no more data or EAGAIN or break by signal, just ignore
    if (0 == nread || UV_EAGAIN == nread || UV_EAI_AGAIN == nread || UV_EINTR == nread) {
        return;
    }

    libuv_ptr_t net = self->find_network((uv_tcp_t*)stream);
    // if network error or reset by peer, move session into reconnect queue
    if (nread < 0) {
        // notify to close fd
        // close this connection
        self->close_net(net);
        return;
    }

    self->on_read_data(net, nread, buf);
}

// ================= connect ==================
void simulator_player_impl::libuv_on_connected(uv_connect_t *req, int status) {
    util::time::time_utility::update(NULL);

    ptr_t* self_sptr = reinterpret_cast<ptr_t*>(req->data);
    assert(self_sptr);
    ptr_t self = *self_sptr;
    req->data = NULL;

    libuv_ptr_t net = self->find_network(req);
    if (0 != status || !net) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "libuv_on_connected callback failed, msg: "<< uv_strerror(status)<< std::endl;
        fprintf(stderr, "libuv_tcp_connect_callback callback failed, msg: %s\n", uv_strerror(status));

        self->on_connected(net, status);
        net->tcp_sock.data = self.get();
        self->close_net(net);

        delete self_sptr;
        return;
    }

    req->handle->data = self.get();
    uv_read_start(req->handle, simulator_player_impl::libuv_on_alloc, simulator_player_impl::libuv_on_read_data);
    net->is_connected = true;

    int res = self->on_connected(net, status);
    if (res < 0) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player on_connected callback failed, ret: "<< res<< std::endl;

        uv_read_stop(req->handle);
        net->tcp_sock.data = self.get();
        self->close_net(net);
    }

    delete self_sptr;
}

void simulator_player_impl::libuv_on_dns_callback(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
    util::time::time_utility::update(NULL);

    ptr_t* self_sptr = reinterpret_cast<ptr_t*>(req->data);
    assert(self_sptr);
    ptr_t self = *self_sptr;
    req->data = NULL;

    libuv_ptr_t net = self->find_network(req);
    do {
        if (0 != status || !net) {
            util::cli::shell_stream ss(std::cerr);
            ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
                << "uv_getaddrinfo callback failed, "<< uv_strerror(status)<< std::endl;
            break;
        }

        if (NULL != net->tcp_sock.data) {
            break;
        }

        sockaddr_storage real_addr;
        uv_tcp_init(req->loop, &net->tcp_sock);
        net->tcp_sock.data = self.get();

        if (AF_INET == res->ai_family) {
            sockaddr_in *res_c = (struct sockaddr_in *)(res->ai_addr);
            char ip[17] = {0};
            uv_ip4_name(res_c, ip, sizeof(ip));
            uv_ip4_addr(ip, net->port, (struct sockaddr_in *)&real_addr);
        } else if (AF_INET6 == res->ai_family) {
            sockaddr_in6 *res_c = (struct sockaddr_in6 *)(res->ai_addr);
            char ip[40] = {0};
            uv_ip6_name(res_c, ip, sizeof(ip));
            uv_ip6_addr(ip, net->port, (struct sockaddr_in6 *)&real_addr);
        } else {
            util::cli::shell_stream ss(std::cerr);
            ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
                << "uv_tcp_connect failed, ai_family not supported: "<< res->ai_family<< std::endl;

            self->close_net(net);
            break;
        }

        net->connect_req.data = self_sptr;
        assert(self_sptr);
        assert(*self_sptr);
        int res_code = uv_tcp_connect(&net->connect_req, &net->tcp_sock, (struct sockaddr *)&real_addr, libuv_on_connected);
        if (0 != res_code) {
            util::cli::shell_stream ss(std::cerr);
            ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
                << "uv_tcp_connect failed, "<<  uv_strerror(res_code)<< std::endl;

            net->connect_req.data = NULL;
            self->close_net(net);
            break;
        }
    } while (false);

    if (!net || net->connect_req.data != self_sptr) {
        delete self_sptr;
    }

    // free addrinfo
    if (NULL != res) {
        uv_freeaddrinfo(res);
    }
}

int simulator_player_impl::on_disconnected(libuv_ptr_t net) { return 0; }

void simulator_player_impl::on_close() {}
void simulator_player_impl::on_closed() {}

int simulator_player_impl::connect(const std::string& host, int port) {
    util::time::time_utility::update(NULL);

    if (is_closing_ || NULL == owner_) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player connect to "<< host<< ":"<< port<< " failed, "<< (is_closing_? "is closing": "has not owner")<< std::endl;
        return -1;
    }

    libuv_ptr_t net = add_network();
    net->port = port;
    ptr_t* async_data = new ptr_t(watcher_.lock());
    net->dns_req.data = async_data;
    assert(net->dns_req.data);
    assert(*async_data);

    int ret = uv_getaddrinfo(owner_->get_loop(), &net->dns_req, libuv_on_dns_callback, host.c_str(), NULL, NULL);
    if (0 != ret) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player connect to "<< host<< ":"<< port<< " failed, "<< uv_strerror(ret)<< std::endl;

        delete (ptr_t*)net->dns_req.data;
        net->dns_req.data = NULL;
        return -1;
    }

    return ret;
}

// ================= write data =================
void simulator_player_impl::libuv_on_written_data(uv_write_t *req, int status) {
    ptr_t* self_sptr = reinterpret_cast<ptr_t*>(req->data);
    assert(self_sptr);
    req->data = NULL;
    ptr_t self = (*self_sptr);

    libuv_ptr_t net = self->find_network(req);
    if (net) {
        net->write_req.data = NULL;
        net->holder.reset();

        self->on_written_data(net, status);
    }
}

int simulator_player_impl::write_message(libuv_ptr_t net, void *buffer, uint64_t sz) {
    if (is_closing_) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "closed or closing player "<<id_ << " can not send any data"<< std::endl;
        return -1;
    }

    if (!net || NULL == net->tcp_sock.data) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player "<<id_ << " socket not available"<< std::endl;
        return -1;
    }

    if (net->holder) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player "<<id_ << " write data failed, another writhing is running."<< std::endl;
        return -1;
    }

    net->holder = watcher_.lock();
    net->write_req.data = &net->holder;
    uv_buf_t bufs[1] = {uv_buf_init(reinterpret_cast<char *>(buffer), static_cast<unsigned int>(sz))};
    int ret = uv_write(&net->write_req, (uv_stream_t*)&net->tcp_sock, bufs, 1, libuv_on_written_data);
    if (0 != ret) {
        net->write_req.data = NULL;
        net->holder.reset();

        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << "player "<<id_ << " write data failed,"<< uv_strerror(ret)<< std::endl;
    }

    return ret;
}

int simulator_player_impl::read_message(const void *buffer, uint64_t sz) {
    if (is_closing_) {
        util::cli::shell_stream ss(std::cout);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW
            << "closed or closing player "<<id_ << " do not deal with any message any more"<< std::endl;
        return -1;
    }

    if (NULL == owner_) {
        util::cli::shell_stream ss(std::cout);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW
            << "player "<<id_ << " without simulator manager can not deal with any message"<< std::endl;
        return -1;
    }

    return owner_->dispatch_message(watcher_.lock(), buffer, sz);
}

// =================== close =======================
void simulator_player_impl::libuv_on_closed(uv_handle_t *handle) {
    ptr_t* self_sptr = reinterpret_cast<ptr_t*>(handle->data);
    assert(self_sptr);
    handle->data = NULL;
    ptr_t self = *self_sptr;

    libuv_ptr_t net = self->find_network((uv_tcp_t*)handle);
    if (net) {
        net->tcp_sock.data = NULL;
        self->on_disconnected(net);
    }

    while(!self->network_.empty() && (!self->network_.front() || self->network_.front()->is_closing)) {
        self->network_.pop_front();
    }

    if (self->network_.empty() && self->is_closing_) {
        self->on_closed();
    }
    delete self_sptr;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::add_network() {
    libuv_ptr_t ret = std::make_shared<libuv_data_t>();
    memset(&ret->tcp_sock, 0, sizeof(ret->tcp_sock));
    memset(&ret->connect_req, 0, sizeof(ret->connect_req));
    memset(&ret->dns_req, 0, sizeof(ret->dns_req));
    memset(&ret->write_req, 0, sizeof(ret->write_req));
    ret->is_closing = false;
    ret->is_connected = false;
    ret->id = ++network_id_;
    ret->port = 0;

    network_.push_back(ret);
    return ret;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::last_network() {
    for(std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++ iter) {
        if (*iter) {
            return *iter;
        }
    }

    return NULL;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::find_network(uv_tcp_t* handle) {
    for(std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++ iter) {
        if ((*iter) && handle == &(*iter)->tcp_sock) {
            return (*iter);
        }
    }

    return NULL;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::find_network(uv_connect_t* handle) {
    for(std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++ iter) {
        if ((*iter) && handle == &(*iter)->connect_req) {
            return (*iter);
        }
    }

    return NULL;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::find_network(uv_getaddrinfo_t* handle) {
    for(std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++ iter) {
        if ((*iter) && handle == &(*iter)->dns_req) {
            return (*iter);
        }
    }

    return NULL;
}

simulator_player_impl::libuv_ptr_t simulator_player_impl::find_network(uv_write_t* handle) {
    for(std::list<libuv_ptr_t>::reverse_iterator iter = network_.rbegin(); iter != network_.rend(); ++ iter) {
        if ((*iter) && handle == &(*iter)->write_req) {
            return (*iter);
        }
    }

    return NULL;
}


int simulator_player_impl::close() {
    if (is_closing_) {
        return 0;
    }
    is_closing_ = true;

    on_close();

    // owner not available any more
    ptr_t self = watcher_.lock();
    assert(self.get());
    if (NULL != owner_) {
        owner_->remove_player(self);
        owner_ = NULL;
    }

    // close fd
    bool has_closing_fd = false;
    for(std::list<libuv_ptr_t>::iterator iter = network_.begin(); iter != network_.end(); ++ iter) {
        if (close_net(*iter)) {
            has_closing_fd = true;
        }
    }

    if (!has_closing_fd) {
        on_closed();
    }

    return 0;
}

bool simulator_player_impl::close_net(libuv_ptr_t net) {
    if (!net) {
        return false;
    }

    if (NULL == net->tcp_sock.data) {
        return false;
    }

    if (net->is_closing) {
        return false;
    }

    ptr_t* async_data = new ptr_t(watcher_.lock());
    net->tcp_sock.data = async_data;
    net->is_closing = true;
    assert(net->tcp_sock.data);
    assert(*async_data);

    uv_close((uv_handle_t *) &net->tcp_sock, libuv_on_closed);
    return true;
}

// ======================== insert cmd ======================
// this function must be thread-safe
int simulator_player_impl::insert_cmd(const std::string &cmd) {
    if (is_closing_) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD
            << "insert cmd into closed or closing player "<<id_ << " failed"<< std::endl;
        return -1;
    }

    if (NULL == owner_) {
        util::cli::shell_stream ss(std::cerr);
        ss()<< util::cli::shell_font_style::SHELL_FONT_COLOR_RED
            << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD
            << "insert cmd into player "<<id_ << " without simulator manager failed"<< std::endl;
        return -1;
    }

    return owner_->insert_cmd(watcher_.lock(), cmd);
}