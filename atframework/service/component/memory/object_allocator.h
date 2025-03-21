// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_utils_build_feature.h>
#include <config/compile_optimize.h>
#include <config/compiler_features.h>
#include <gsl/select-gsl.h>
#include <memory/rc_ptr.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "memory/object_allocator_manager.h"
#include "memory/object_allocator_type_traits.h"

// ============= Patch for some Compilers's mistake =============
#if defined(__GNUC__) && !defined(__clang__) && !defined(__apple_build_version__)
#  if (__GNUC__ * 100 + __GNUC_MINOR__ * 10) >= 460
#    pragma GCC diagnostic push
#  endif

#  if (__GNUC__ * 100 + __GNUC_MINOR__) == 1402
#    pragma GCC diagnostic ignored "-Wuninitialized"
#    pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#  endif
#endif

namespace atframework {
namespace memory {

class object_allocator {
 public:
  template <class... Args>
  using allocator = object_allocator_manager::allocator<Args...>;

  template <class Key, class Value, class BackendAllocator = ::std::allocator<std::pair<const Key, Value>>>
  using map_allocator = allocator<std::pair<const Key, Value>, BackendAllocator>;

  template <class T, class BackendDelete = ::std::default_delete<T>>
  using deletor = object_allocator_manager::deletor<T, BackendDelete>;

 public:
  template <class T, class... Args>
  ATFW_UTIL_FORCEINLINE static std::shared_ptr<T> make_shared(Args&&... args) {
    return object_allocator_manager::make_shared<T>(std::forward<Args>(args)...);
  }

  template <class T, class... Args>
  ATFW_UTIL_FORCEINLINE static std::shared_ptr<T> allocate_shared(Args&&... args) {
    return object_allocator_manager::allocate_shared<T>(std::forward<Args>(args)...);
  }

  template <class T, class... Args>
  ATFW_UTIL_FORCEINLINE static atfw::util::memory::strong_rc_ptr<T> make_strong_rc(Args&&... args) {
    return object_allocator_manager::make_strong_rc<T>(std::forward<Args>(args)...);
  }

  template <class T, class... Args>
  ATFW_UTIL_FORCEINLINE static atfw::util::memory::strong_rc_ptr<T> allocate_strong_rc(Args&&... args) {
    return object_allocator_manager::allocate_strong_rc<T>(std::forward<Args>(args)...);
  }
};

namespace stl {
template <class... Args>
using allocator = object_allocator_manager::allocator<Args...>;

template <class T, class BackendDelete = ::std::default_delete<T>>
using deletor = object_allocator_manager::deletor<T, BackendDelete>;

template <class T, class... Args>
ATFW_UTIL_FORCEINLINE static std::shared_ptr<T> make_shared(Args&&... args) {
  return object_allocator_manager::make_shared<T>(std::forward<Args>(args)...);
}

template <class T, class... Args>
ATFW_UTIL_FORCEINLINE static std::shared_ptr<T> allocate_shared(Args&&... args) {
  return object_allocator_manager::allocate_shared<T>(std::forward<Args>(args)...);
}

template <class T, class... Args>
ATFW_UTIL_FORCEINLINE static atfw::util::memory::strong_rc_ptr<T> make_strong_rc(Args&&... args) {
  return object_allocator_manager::make_strong_rc<T>(std::forward<Args>(args)...);
}

template <class T, class... Args>
ATFW_UTIL_FORCEINLINE static atfw::util::memory::strong_rc_ptr<T> allocate_strong_rc(Args&&... args) {
  return object_allocator_manager::allocate_strong_rc<T>(std::forward<Args>(args)...);
}
}  // namespace stl

}  // namespace memory
}  // namespace atframework

#if defined(__GNUC__) && !defined(__clang__) && !defined(__apple_build_version__)
#  if (__GNUC__ * 100 + __GNUC_MINOR__ * 10) >= 460
#    pragma GCC diagnostic pop
#  endif
#endif
