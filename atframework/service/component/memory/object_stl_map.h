// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <map>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace stl {

template <class Key, class Value, class Compare = std::less<Key>,
          class BackendAllocator = ::std::allocator<std::pair<const Key, Value>>>
using map = std::map<Key, Value, Compare, object_allocator::map_allocator<Key, Value, BackendAllocator>>;

}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
