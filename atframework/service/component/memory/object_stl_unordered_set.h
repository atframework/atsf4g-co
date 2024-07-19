// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <unordered_set>

namespace atframework {
namespace memory {
namespace stl {

template <class T, class Hash = std::hash<T>, class Pred = std::equal_to<T>,
          class BackendAllocator = ::std::allocator<T>>
using unordered_set = std::unordered_set<T, Hash, Pred, object_allocator::allocator<T, BackendAllocator>>;

}
}  // namespace memory
}  // namespace atframework
