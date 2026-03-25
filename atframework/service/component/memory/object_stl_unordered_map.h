// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <unordered_map>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace stl {

template <class Key, class Value, class Hash = std::hash<Key>, class Pred = std::equal_to<Key>,
          class BackendAllocator = ::std::allocator<std::pair<const Key, Value>>>
using unordered_map =
    std::unordered_map<Key, Value, Hash, Pred, object_allocator::map_allocator<Key, Value, BackendAllocator>>;

}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
