// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstdint>
#include <map>
#include <memory>

#include <atframework/testing/runtime.h>

#include "router/router_manager_base.h"
#include "router/router_object_base.h"

namespace atsf4g {
namespace testing {

class mock_router;

// Router object with a pre-seeded router server id. All persistence operations are no-ops returning
// success; no real IO is ever triggered.
class RPC_UNIT_TEST_API router_test_object : public router_object_base {
 public:
  explicit router_test_object(const key_t &key);

  const char *name() const override { return "atsf4g::testing::router_test_object"; }

  rpc::result_code_type pull_object(rpc::context &ctx, void *priv_data) override;
  rpc::result_code_type save_object(rpc::context &ctx, void *priv_data) override;
  rpc::result_code_type save(rpc::context &ctx, void *priv_data, io_task_guard &guard) override;
};

// Router manager that resolves keys synchronously to pre-seeded test objects, without any real cache
// pull. Construction/destruction auto-registers into router_manager_set (base class behavior).
class RPC_UNIT_TEST_API router_test_manager : public router_manager_base {
 public:
  explicit router_test_manager(uint32_t type_id);
  ~router_test_manager() override;

  const char *name() const override { return "atsf4g::testing::router_test_manager"; }

  std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const override;
  rpc::result_code_type mutable_cache(rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key,
                                      void *priv_data, router_object_base::io_task_guard &io_guard) override;
  rpc::result_code_type mutable_object(rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key,
                                       void *priv_data, router_object_base::io_task_guard &io_guard) override;
  rpc::result_code_type remove_cache(rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache,
                                     void *priv_data, router_object_base::io_task_guard &io_guard) override;
  rpc::result_code_type remove_object(rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache,
                                      void *priv_data, router_object_base::io_task_guard &io_guard) override;

  // Pre-seed a test object routed to router_server_id. Returns the created object (also kept by the
  // manager) or nullptr when the key already exists.
  std::shared_ptr<router_test_object> add_object(const key_t &key, uint64_t router_server_id,
                                                 uint64_t router_version = 1);

  void remove_all_objects();
  size_t object_count() const noexcept { return objects_.size(); }

 private:
  std::map<key_t, std::shared_ptr<router_test_object>> objects_;
};

// Router facade bound to one runtime. Managers created here are destroyed at runtime teardown so a
// case can not leak manager registrations into the next fixture.
class RPC_UNIT_TEST_API mock_router {
 public:
  mock_router();
  ~mock_router();

  mock_router(const mock_router &) = delete;
  mock_router &operator=(const mock_router &) = delete;

  // Create and register a router_test_manager. Returns nullptr when a manager with the same type id
  // already exists.
  router_test_manager *create_manager(uint32_t type_id);
  void destroy_manager(uint32_t type_id);

  router_test_manager *get_manager(uint32_t type_id) const;

 private:
  friend class runtime;
  void unbind();

  std::map<uint32_t, std::unique_ptr<router_test_manager>> managers_;
};

}  // namespace testing
}  // namespace atsf4g
