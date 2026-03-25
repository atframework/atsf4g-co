// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <set>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace stl {

template <class T, class Compare = std::less<T>, class BackendAllocator = ::std::allocator<T>>
using set = std::set<T, Compare, object_allocator::allocator<T, BackendAllocator>>;

}
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
