// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_utils_build_feature.h>
#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <gsl/select-gsl.h>
#include <memory/rc_ptr.h>
#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "memory/object_allocator_def.h"
#include "memory/object_allocator_metrics.h"

namespace atframework {
namespace memory {

template <class T>
struct object_allocator_default_backend {
  using allocator = ::std::allocator<T>;
};

template <class T>
struct object_allocator_backend;

template <class T>
struct object_allocator_backend : public object_allocator_default_backend<T> {};

class object_allocator_manager {
 public:
  template <class T, class BackendDelete = ::std::default_delete<T>>
  struct UTIL_SYMBOL_VISIBLE deletor;

  template <class T>
  struct UTIL_SYMBOL_VISIBLE deletor<T, ::std::default_delete<T>> {
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor() noexcept {}
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor&&) = default;

    template <class Up, class = util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const ::std::default_delete<Up>&) noexcept {}

    template <class Up, class = util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor<Up, ::std::default_delete<Up>>&) noexcept {}

    void operator()(T* ptr) const {
      if (nullptr != ptr) {
        object_allocator_metrics_controller::add_destructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
        object_allocator_metrics_controller::add_deallocate_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), 1);
      }

      ::std::default_delete<T> backend_delete;
      backend_delete(ptr);
    }
  };

  template <class T, class BackendDelete>
  struct UTIL_SYMBOL_VISIBLE deletor {
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor() noexcept {}
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor&&) = default;

    template <class D>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(D&& d) noexcept : backend_delete_(std::forward<D>(d)) {}

    template <class Up, class UpBackendDelete, class = util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor<Up, UpBackendDelete>& other) noexcept
        : backend_delete_(other.backend_delete_) {}

    template <class Up, class UpBackendDelete, class = util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor<Up, UpBackendDelete>&& other) noexcept
        : backend_delete_(std::move(other.backend_delete_)) {}

    void operator()(T* ptr) const {
      if (nullptr != ptr) {
        object_allocator_metrics_controller::add_destructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
        object_allocator_metrics_controller::add_deallocate_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), 1);
      }
      backend_delete_(ptr);
    }

    BackendDelete backend_delete_;
  };

  template <class T, class BackendAllocator = ::std::allocator<T>>
  struct allocator {
    using background_allocator_type = BackendAllocator;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    // constructors
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator() noexcept {}

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const BackendAllocator& backend) noexcept
        : backend_allocator_{backend} {}

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const allocator&) = default;

    template <class U, class UBackendAllocator>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const allocator<U, UBackendAllocator>& other) noexcept
        : backend_allocator_(other.backend_allocator_) {}

    // STL wiil rebind rebind_alloc to allocator<U, BackendAllocator>, in which BackendAllocator may not be right
    // So we always use rebind<U>::other to support allocator rebinding
    template <class U>
    struct rebind {
      using __rebind_backend_type_other =
          typename ::std::allocator_traits<background_allocator_type>::template rebind_alloc<U>;
      using other = allocator<U, __rebind_backend_type_other>;
    };

#if (!defined(__cplusplus) && !defined(_MSVC_LANG)) || \
    !((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    inline pointer address(reference x) const noexcept { return &x; }

    inline const_pointer address(const_reference x) const noexcept { return &x; }

#  if ((defined(__cplusplus) && __cplusplus <= 201103L) || (defined(_MSVC_LANG) && _MSVC_LANG <= 201103L))
    inline size_type max_size() const { return backend_allocator_.max_size(); }
#  else
    inline size_type max_size() const noexcept { return backend_allocator_.max_size(); }
#  endif

    template <class U, class... Args>
    inline void construct(U* p, Args&&... args) {
      backend_allocator_.construct(p, std::forward<Args>(args)...);
      object_allocator_metrics_controller::add_constructor_counter_template<T>(reinterpret_cast<void*>(p));
    }

    template <class U, class... Args>
    inline void destroy(U* p) {
      object_allocator_metrics_controller::add_destructor_counter_template<T>(reinterpret_cast<void*>(p));
      backend_allocator_.destroy(p);
    }
#endif

#if ((defined(__cplusplus) && __cplusplus >= 202302L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)) && \
    defined(__cpp_lib_allocate_at_least)
    EXPLICIT_NODISCARD_ATTR UTIL_CONFIG_CONSTEXPR std::allocation_result<T*, size_type> allocate_at_least(size_type n) {
      auto ret = backend_allocator_.allocate_at_least(n);
      object_allocator_metrics_controller::add_allocate_counter_template<T>(ret.second);
      return ret;
    }
#endif

#if ((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
    EXPLICIT_NODISCARD_ATTR inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR T* allocate(size_type n) {
      auto ret = backend_allocator_.allocate(n);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }
#elif ((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
    inline T* allocate(size_type n) {
      auto ret = backend_allocator_.allocate(n);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }

    inline T* allocate(size_type n, const void* hint) {
      auto ret = backend_allocator_.allocate(n, hint);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }

#else
    inline T* allocate(size_type n, const void* hint = nullptr) {
      auto ret = backend_allocator_.allocate(n, hint);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }
#endif

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR void deallocate(T* p, size_type n) {
      if (nullptr == p) {
        return;
      }

      object_allocator_metrics_controller::add_deallocate_counter_template<T>(n);
      backend_allocator_.deallocate(p, n);
    }

    UTIL_SYMBOL_VISIBLE inline bool operator==(const allocator&) const noexcept { return true; }

    template <class U, class UBackendAllocator>
    UTIL_SYMBOL_VISIBLE inline bool operator==(const allocator<U, UBackendAllocator>&) const noexcept {
      return false;
    }

    background_allocator_type backend_allocator_;
  };

 public:
  template <class T, class... Args>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<T> make_shared(Args&&... args) {
    allocator<T, typename object_allocator_backend<T>::allocator> alloc;
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }

  template <class T, class Alloc, class... Args>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<type_traits::non_array<T>> allocate_shared(
      const Alloc& backend_alloc, Args&&... args) {
    allocator<T, Alloc> alloc{backend_alloc};
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }

#if ((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
  template <class T, class Alloc>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<type_traits::unbounded_array<T>> allocate_shared(
      const Alloc& backend_alloc, ::std::size_t N) {
    allocator<T, Alloc> alloc{backend_alloc};
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, N);

    if (ret) {
      for (auto& ptr : *ret) {
        object_allocator_metrics_controller::add_constructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
      }
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), N);
    }

    return ret;
  }

  template <class T, class Alloc>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<type_traits::bounded_array<T>> allocate_shared(
      const Alloc& backend_alloc) {
    allocator<T, Alloc> alloc{backend_alloc};
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc);

    if (ret) {
      for (auto& ptr : *ret) {
        object_allocator_metrics_controller::add_constructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
      }
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), ::std::extent<T>::value);
    }

    return ret;
  }

  template <class T, class Alloc>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<type_traits::unbounded_array<T>> allocate_shared(
      const Alloc& backend_alloc, ::std::size_t N, const std::remove_extent_t<T>& u) {
    allocator<T, Alloc> alloc{backend_alloc};
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, N, u);

    if (ret) {
      for (auto& ptr : *ret) {
        object_allocator_metrics_controller::add_constructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
      }
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), N);
    }

    return ret;
  }

  template <class T, class Alloc>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<type_traits::bounded_array<T>> allocate_shared(
      const Alloc& backend_alloc, const std::remove_extent_t<T>& u) {
    allocator<T, Alloc> alloc{backend_alloc};
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, u);

    if (ret) {
      for (auto& ptr : *ret) {
        object_allocator_metrics_controller::add_constructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
      }
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), ::std::extent<T>::value);
    }

    return ret;
  }
#endif

  template <class T, class... Args>
  UTIL_SYMBOL_VISIBLE inline static util::memory::strong_rc_ptr<T> make_strong_rc(Args&&... args) {
    allocator<T, typename object_allocator_backend<T>::allocator> alloc;
    util::memory::strong_rc_ptr<T> ret = util::memory::allocate_strong_rc<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }

  template <class T, class Alloc, class... Args>
  UTIL_SYMBOL_VISIBLE inline static util::memory::strong_rc_ptr<type_traits::non_array<T>> allocate_strong_rc(
      const Alloc& backend_alloc, Args&&... args) {
    allocator<T, Alloc> alloc{backend_alloc};
    util::memory::strong_rc_ptr<T> ret = util::memory::allocate_strong_rc<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }
};

}  // namespace memory
}  // namespace atframework
