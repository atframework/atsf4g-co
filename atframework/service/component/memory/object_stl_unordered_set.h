// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <unordered_set>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace stl {

template <class T, class Hash = std::hash<T>, class Pred = std::equal_to<T>,
          class BackendAllocator = ::std::allocator<T>>
using unordered_set = std::unordered_set<T, Hash, Pred, object_allocator::allocator<T, BackendAllocator>>;

}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
