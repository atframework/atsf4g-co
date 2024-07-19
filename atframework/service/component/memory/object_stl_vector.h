// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <vector>

namespace atframework {
namespace memory {
namespace stl {

template <class T, class BackendAllocator = ::std::allocator<T>>
using vector = std::vector<T, object_allocator::allocator<T, BackendAllocator>>;

}
}  // namespace memory
}  // namespace atframework
