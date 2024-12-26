// Copyright 2024 atframework
// Created by owent

#pragma once

#include <memory/object_allocator.h>

#include <memory/lru_map.h>

namespace atframework {
namespace memory {
namespace util {
template <class Key, class Value, class Hash = ::std::hash<Key>, class Eq = ::std::equal_to<Key>,
          class TOption = atfw::util::memory::lru_map_option<atfw::util::memory::compat_strong_ptr_mode::kStl>,
          class BackendAllocator = ::std::allocator<
              std::pair<const Key, typename atfw::util::memory::lru_map_type_traits<Key, Value, TOption>::iterator>>>
using lru_map = atfw::util::memory::lru_map<
    Key, Value, Hash, Eq, TOption,
    object_allocator::map_allocator<
        Key, typename atfw::util::memory::lru_map_type_traits<Key, Value, TOption>::iterator, BackendAllocator>>;

template <class Key, class Value, class Hash = ::std::hash<Key>, class Eq = ::std::equal_to<Key>,
          class TOption = atfw::util::memory::lru_map_option<atfw::util::memory::compat_strong_ptr_mode::kStrongRc>,
          class BackendAllocator = ::std::allocator<
              std::pair<const Key, typename atfw::util::memory::lru_map_type_traits<Key, Value, TOption>::iterator>>>
using lru_map_st = atfw::util::memory::lru_map<
    Key, Value, Hash, Eq, TOption,
    object_allocator::map_allocator<
        Key, typename atfw::util::memory::lru_map_type_traits<Key, Value, TOption>::iterator, BackendAllocator>>;
}  // namespace util
}  // namespace memory
}  // namespace atframework
