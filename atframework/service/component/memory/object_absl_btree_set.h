// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <absl/container/btree_set.h>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace absl {
template <class Key, class Compare = std::less<Key>, class BackendAllocator = ::std::allocator<Key>>
using btree_set = ::absl::btree_set<Key, Compare, object_allocator::allocator<Key, BackendAllocator>>;
}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
