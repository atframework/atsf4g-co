// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_service_component_config.h>

#include <memory/object_allocator.h>

#include <queue>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN
namespace memory {
namespace stl {

template <class T, class BackendAllocator = ::std::allocator<T>>
using deque = std::deque<T, object_allocator::allocator<T, BackendAllocator>>;

template <class T, class Sequence = deque<T>>
using queue = std::queue<T, object_allocator::allocator<T>>;

}  // namespace stl
}  // namespace memory
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
