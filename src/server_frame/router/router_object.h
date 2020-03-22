//
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ROUTER_OBJECT_H
#define ROUTER_ROUTER_OBJECT_H

#pragma once

#include <assert.h>
#include <std/smart_ptr.h>

#include "router_object_base.h"

#include <protocol/pbdesc/svr.const.err.pb.h>

template <typename TObj, typename TChild>
class router_object : public router_object_base {
public:
    typedef router_object_base::key_t key_t;
    typedef router_object_base::flag_t flag_t;
    typedef TObj value_type;
    typedef TChild self_type;
    typedef std::shared_ptr<value_type> object_ptr_t;
    typedef std::shared_ptr<self_type> ptr_t;
    typedef typename router_object_base::flag_guard flag_guard;

public:
    router_object(const object_ptr_t &data, const key_t &k) : router_object_base(k), obj_(data) { assert(obj_); }

    router_object(const object_ptr_t &data, key_t &&k) : router_object_base(k), obj_(data) { assert(obj_); }

    inline bool is_object_equal(const object_ptr_t &checked) const { return checked == obj_; }

    const object_ptr_t &get_object() {
        refresh_visit_time();
        return obj_;
    }

    /**
     * @brief 保存到数据库，如果成功会更新最后保存时间
     * @return
     */
    virtual int save(void *priv_data) UTIL_CONFIG_OVERRIDE {
        if (!is_writable()) {
            return hello::err::EN_ROUTER_NOT_WRITABLE;
        }

        int ret = save_object_inner(priv_data);
        return ret;
    }

    // =========================== 子类需要实现以下接口 ===========================
    // virtual int pull_cache(void *priv_data);     // 可选 - 不接入的话会调用pull_object(void *priv_data)
    // virtual int pull_object(void *priv_data);    // 必需 - 注意事项见 router_object_base::pull_cache
    // virtual int save_object(void *priv_data);    // 必需 - 注意事项见 router_object_base::save_object

protected:
    inline const object_ptr_t &object() const { return obj_; }

private:
    object_ptr_t obj_;
};


#endif //_ROUTER_ROUTER_OBJECT_H
