//
// Created by owt50 on 2016/10/9.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_PLAYER_IMPL_H
#define ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_PLAYER_IMPL_H

#pragma once

#include <list>
#include <std/smart_ptr.h>
#include <string>
#include <vector>

#include <uv.h>

class simulator_base;

class simulator_player_impl {
public:
    typedef std::shared_ptr<simulator_player_impl> ptr_t;
    struct libuv_data_t {
        uint32_t         id;
        uv_tcp_t         tcp_sock;
        uv_connect_t     connect_req;
        uv_getaddrinfo_t dns_req;
        uv_write_t       write_req;
        ptr_t            holder;
        int              port;
        bool             is_closing;
        bool             is_connected;
    };
    typedef std::shared_ptr<libuv_data_t> libuv_ptr_t;

protected:
    simulator_player_impl();
    virtual ~simulator_player_impl() = 0;

public:
    inline const std::string &get_id() const { return id_; }
    bool                      set_id(const std::string &id);

    inline const simulator_base *get_owner() const { return owner_; }
    inline simulator_base *      get_owner() { return owner_; }

    virtual int  on_connected(libuv_ptr_t net, int status)                         = 0;
    virtual void on_alloc(libuv_ptr_t net, size_t suggested_size, uv_buf_t *buf)   = 0;
    virtual void on_read_data(libuv_ptr_t net, ssize_t nread, const uv_buf_t *buf) = 0;
    virtual void on_read_message(libuv_ptr_t net, const void *buffer, size_t sz)   = 0;
    virtual void on_written_data(libuv_ptr_t net, int status)                      = 0;
    virtual int  on_write_message(libuv_ptr_t net, void *buffer, uint64_t sz)      = 0;
    virtual int  on_disconnected(libuv_ptr_t net);
    /**
     * @brief start to close player resource
     */
    virtual void on_close();
    /**
     * @brief already closed and can not access any resource any more
     */
    virtual void on_closed();

    virtual int connect(const std::string &host, int port);

    virtual int write_message(libuv_ptr_t net, void *buffer, uint64_t sz);
    virtual int read_message(const void *buffer, uint64_t sz);

    int close();

    bool close_net(libuv_ptr_t net);

    // this function must be thread-safe
    int insert_cmd(const std::string &cmd);

    inline bool is_closing() const { return is_closing_; }

private:
    static void libuv_on_dns_callback(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
    static void libuv_on_connected(uv_connect_t *req, int status);
    static void libuv_on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void libuv_on_read_data(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    static void libuv_on_written_data(uv_write_t *req, int status);
    static void libuv_on_closed(uv_handle_t *handle);

public:
    libuv_ptr_t add_network();
    libuv_ptr_t last_network();
    libuv_ptr_t find_network(uv_tcp_t *handle);
    libuv_ptr_t find_network(uv_connect_t *handle);
    libuv_ptr_t find_network(uv_getaddrinfo_t *handle);
    libuv_ptr_t find_network(uv_write_t *handle);

protected:
    std::list<libuv_ptr_t> network_;

private:
    bool                                 is_closing_;
    std::string                          id_;
    simulator_base *                     owner_;
    std::weak_ptr<simulator_player_impl> watcher_;
    uint32_t                             network_id_;

    friend class simulator_base;
};


#endif // ATF4G_CO_SIMULATOR_PLAYER_IMPL_H
