// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <absl/container/node_hash_set.h>

namespace atframework {
namespace memory {
namespace absl {
template <class Key, class Hash = ::absl::container_internal::hash_default_hash<Key>,
          class Eq = ::absl::container_internal::hash_default_eq<Key>, class BackendAllocator = ::std::allocator<Key>>
using node_hash_set = ::absl::node_hash_set<Key, Hash, Eq, object_allocator::allocator<Key, BackendAllocator>>;
}
}  // namespace memory
}  // namespace atframework
