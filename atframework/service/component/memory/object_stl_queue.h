// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <queue>

namespace atframework {
namespace memory {
namespace stl {

template <class T, class BackendAllocator = ::std::allocator<T>>
using deque = std::deque<T, object_allocator::allocator<T, BackendAllocator>>;

template <class T, class Sequence = deque<T>>
using queue = std::queue<T, object_allocator::allocator<T>>;

}  // namespace stl
}  // namespace memory
}  // namespace atframework
