// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <set>

namespace atframework {
namespace memory {
namespace stl {

template <class T, class Compare = std::less<T>, class BackendAllocator = ::std::allocator<T>>
using set = std::set<T, Compare, object_allocator::allocator<T, BackendAllocator>>;

}
}  // namespace memory
}  // namespace atframework
