// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <map>

namespace atframework {
namespace memory {
namespace stl {

template <class Key, class Value, class Compare = std::less<Key>,
          class BackendAllocator = ::std::allocator<std::pair<const Key, Value>>>
using map = std::map<Key, Value, Compare, object_allocator::map_allocator<Key, Value, BackendAllocator>>;

}
}  // namespace memory
}  // namespace atframework
