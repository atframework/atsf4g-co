//
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ROUTER_MANAGER_BASE_H
#define ROUTER_ROUTER_MANAGER_BASE_H

#pragma once

#include <std/smart_ptr.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include "router_object_base.h"

class router_manager_base {
public:
    typedef router_object_base::key_t key_t;

protected:
    explicit router_manager_base(uint32_t type_id);

public:
    virtual ~router_manager_base();
    virtual const char *name() const = 0;

    inline uint32_t                             get_type_id() const { return type_id_; };
    virtual std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const                                                      = 0;
    virtual int                                 mutable_cache(std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data)  = 0;
    virtual int                                 mutable_object(std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data) = 0;

    virtual bool remove_cache(const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data)  = 0;
    virtual bool remove_object(const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data) = 0;

    virtual bool     is_auto_mutable_object() const;
    virtual bool     is_auto_mutable_cache() const;
    virtual uint64_t get_default_router_server_id(const router_object_base &router_cache) const;

    int send_msg(router_object_base &obj, hello::SSMsg &msg);
    int send_msg(const key_t &key, hello::SSMsg &msg);

    inline size_t size() const { return stat_size_; }

    inline bool is_closing() const { return is_closing_; }

    virtual void on_stop();

    virtual int pull_online_server(const key_t &key, uint64_t &router_svr_id, uint64_t &router_svr_ver);

protected:
    int send_msg_raw(router_object_base &obj, hello::SSMsg &msg);

protected:
    size_t stat_size_;

private:
    uint32_t type_id_;
    bool     is_closing_;
};


#endif //_ROUTER_ROUTER_MANAGER_BASE_H
