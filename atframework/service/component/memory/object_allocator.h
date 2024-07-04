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

#include "memory/object_allocator_manager.h"
#include "memory/object_allocator_type_traits.h"

namespace atframework {
namespace memory {

class object_allocator {
 public:
  template <class... Args>
  using allocator = object_allocator_manager::allocator<Args...>;

  template <class T>
  using deletor = object_allocator_manager::deletor<T>;

 public:
  template <class T, class... Args>
  UTIL_FORCEINLINE static std::shared_ptr<T> make_shared(Args&&... args) {
    return object_allocator_manager::make_shared<T>(std::forward<Args>(args)...);
  }

  template <class T, class... Args>
  UTIL_FORCEINLINE static std::shared_ptr<T> allocate_shared(Args&&... args) {
    return object_allocator_manager::allocate_shared<T>(std::forward<Args>(args)...);
  }

  template <class T, class... Args>
  UTIL_FORCEINLINE static util::memory::strong_rc_ptr<T> make_strong_rc(Args&&... args) {
    return object_allocator_manager::make_strong_rc<T>(std::forward<Args>(args)...);
  }
};

}  // namespace memory
}  // namespace atframework
