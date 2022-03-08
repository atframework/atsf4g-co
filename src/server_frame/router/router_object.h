// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <assert.h>
#include <memory>

#include "router/router_object_base.h"

template <typename TObj, typename TChild>
class router_object : public router_object_base {
 public:
  using key_t = router_object_base::key_t;
  using flag_t = router_object_base::flag_t;
  using value_type = TObj;
  using self_type = TChild;
  using object_ptr_t = std::shared_ptr<value_type>;
  using ptr_t = std::shared_ptr<self_type>;
  using flag_guard = typename router_object_base::flag_guard;

 public:
  explicit router_object(const object_ptr_t &data, const key_t &k) : router_object_base(k), obj_(data) { assert(obj_); }
  explicit router_object(const object_ptr_t &data, key_t &&k) : router_object_base(k), obj_(data) { assert(obj_); }

  inline bool is_object_equal(const object_ptr_t &checked) const { return checked == obj_; }
  inline bool is_object_equal(const value_type &checked) const { return &checked == obj_.get(); }

  const object_ptr_t &get_object() {
    refresh_visit_time();
    return obj_;
  }

  /**
   * @brief 保存到数据库，如果成功会更新最后保存时间
   * @return
   */
  rpc::result_code_type save(rpc::context &ctx, void *priv_data) override {
    if (!is_writable()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
    }

    return save_object_inner(ctx, priv_data);
  }

  // =========================== 子类需要实现以下接口 ===========================
  // 可选 - 不接入的话会调用pull_object(void *priv_data)
  // rpc::result_code_type pull_cache(rpc::context ctx, void *priv_data) override;

  // 必需 - 注意事项见 router_object_base::pull_cache
  // rpc::result_code_type pull_object(rpc::context ctx, void *priv_data) override;

  // 必需 - 注意事项见 router_object_base::save_object
  // rpc::result_code_type save_object(rpc::context ctx, void *priv_data) override;

 protected:
  inline const object_ptr_t &object() const { return obj_; }

 private:
  object_ptr_t obj_;
};
