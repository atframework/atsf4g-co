// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <absl/container/node_hash_map.h>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace absl {
template <class Key, class Value, class Hash = ::absl::container_internal::hash_default_hash<Key>,
          class Eq = ::absl::container_internal::hash_default_eq<Key>,
          class BackendAllocator = ::std::allocator<::std::pair<const Key, Value>>>
using node_hash_map =
    ::absl::node_hash_map<Key, Value, Hash, Eq, object_allocator::map_allocator<Key, Value, BackendAllocator>>;
}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
