// Copyright 2026 atframework

#include <atframework/testing/mock_router.h>

#include "rpc/rpc_common_types.h"

namespace atframework {
namespace testing {

router_test_object::router_test_object(const key_t &key) : router_object_base(key) {}

rpc::result_code_type router_test_object::pull_object(rpc::context &, void *) { RPC_RETURN_CODE(0); }

rpc::result_code_type router_test_object::save_object(rpc::context &, void *) { RPC_RETURN_CODE(0); }

rpc::result_code_type router_test_object::save(rpc::context &, void *, io_task_guard &) { RPC_RETURN_CODE(0); }

router_test_manager::router_test_manager(uint32_t type_id) : router_manager_base(type_id) {}

router_test_manager::~router_test_manager() = default;

std::shared_ptr<router_object_base> router_test_manager::get_base_cache(const key_t &key) const {
  auto iter = objects_.find(key);
  if (iter == objects_.end()) {
    return nullptr;
  }
  return iter->second;
}

rpc::result_code_type router_test_manager::mutable_cache(rpc::context &, std::shared_ptr<router_object_base> &out,
                                                         const key_t &key, void *,
                                                         router_object_base::io_task_guard &) {
  auto iter = objects_.find(key);
  if (iter == objects_.end()) {
    out.reset();
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }
  out = iter->second;
  RPC_RETURN_CODE(0);
}

rpc::result_code_type router_test_manager::mutable_object(rpc::context &, std::shared_ptr<router_object_base> &out,
                                                          const key_t &key, void *,
                                                          router_object_base::io_task_guard &) {
  auto iter = objects_.find(key);
  if (iter == objects_.end()) {
    out.reset();
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }
  out = iter->second;
  RPC_RETURN_CODE(0);
}

rpc::result_code_type router_test_manager::remove_cache(rpc::context &, const key_t &key,
                                                        std::shared_ptr<router_object_base>, void *,
                                                        router_object_base::io_task_guard &) {
  objects_.erase(key);
  RPC_RETURN_CODE(0);
}

rpc::result_code_type router_test_manager::remove_object(rpc::context &, const key_t &key,
                                                         std::shared_ptr<router_object_base>, void *,
                                                         router_object_base::io_task_guard &) {
  objects_.erase(key);
  RPC_RETURN_CODE(0);
}

std::shared_ptr<router_test_object> router_test_manager::add_object(const key_t &key, uint64_t router_server_id,
                                                                    uint64_t router_version) {
  if (objects_.find(key) != objects_.end()) {
    return nullptr;
  }

  auto object = std::make_shared<router_test_object>(key);
  if (!object) {
    return nullptr;
  }
  object->set_router_server_id(router_server_id, router_version);
  objects_[key] = object;
  return object;
}

void router_test_manager::remove_all_objects() { objects_.clear(); }

mock_router::mock_router() = default;

mock_router::~mock_router() { unbind(); }

router_test_manager *mock_router::create_manager(uint32_t type_id) {
  if (managers_.find(type_id) != managers_.end()) {
    return nullptr;
  }

  auto manager = std::make_unique<router_test_manager>(type_id);
  if (!manager) {
    return nullptr;
  }
  router_test_manager *ret = manager.get();
  managers_[type_id] = std::move(manager);
  return ret;
}

void mock_router::destroy_manager(uint32_t type_id) { managers_.erase(type_id); }

router_test_manager *mock_router::get_manager(uint32_t type_id) const {
  auto iter = managers_.find(type_id);
  if (iter == managers_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

void mock_router::unbind() { managers_.clear(); }

}  // namespace testing
}  // namespace atframework
