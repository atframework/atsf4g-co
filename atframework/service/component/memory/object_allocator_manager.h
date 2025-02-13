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

template <class T>
struct object_allocator_default_backend {
  using allocator = ::std::allocator<atfw::util::nostd::remove_cv_t<T>>;
};

template <class T>
struct object_allocator_backend;

template <class T>
struct object_allocator_backend : public object_allocator_default_backend<T> {};

class object_allocator_manager {
 private:
  template <class T, class = atfw::util::nostd::enable_if_t<!std::is_const<T>::value>>
  UTIL_SYMBOL_VISIBLE inline static T* to_mutable_address(T* in) noexcept {
    return in;
  }

  template <class T, class = atfw::util::nostd::enable_if_t<std::is_const<T>::value>>
  UTIL_SYMBOL_VISIBLE inline static atfw::util::nostd::remove_const_t<T>* to_mutable_address(T* in) noexcept {
    return const_cast<atfw::util::nostd::remove_const_t<T>*>(in);
  }

 public:
  template <class T, class BackendDelete = ::std::default_delete<T>>
  struct UTIL_SYMBOL_VISIBLE deletor;

  template <class T>
  struct UTIL_SYMBOL_VISIBLE deletor<T, ::std::default_delete<T>> {
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor() noexcept {}
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor&&) = default;

    template <class Up, class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const ::std::default_delete<Up>&) noexcept {}

    template <class Up, class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
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
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor() noexcept(
        std::is_nothrow_constructible<BackendDelete>::value) {}
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor&&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor& operator=(const deletor&) = default;
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor& operator=(deletor&&) = default;

    template <class D>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(D&& d) noexcept(
        std::is_nothrow_constructible<BackendDelete, D>::value) {
      new (backend_deletor_buffer()) BackendDelete(::std::forward<D>(d));
    }

    template <class Up, class UpBackendDelete,
              class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(const deletor<Up, UpBackendDelete>& other) noexcept(
        std::is_nothrow_constructible<BackendDelete, const UpBackendDelete&>::value) {
      new (backend_deletor_buffer()) BackendDelete(*other.backend_deletor());
    }

    template <class Up, class UpBackendDelete,
              class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor(deletor<Up, UpBackendDelete>&& other) noexcept(
        std::is_nothrow_constructible<BackendDelete, UpBackendDelete&&>::value) {
      new (backend_deletor_buffer()) BackendDelete(std::move(*other.backend_deletor()));
    }

    template <class Up, class UpBackendDelete,
              class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor& operator=(const deletor<Up, UpBackendDelete>& other) noexcept(
        std::is_nothrow_assignable<BackendDelete, const UpBackendDelete&>::value) {
      *backend_deletor() = *other.backend_deletor();
      return *this;
    }

    template <class Up, class UpBackendDelete,
              class = atfw::util::nostd::enable_if_t<::std::is_convertible<Up*, T*>::value>>
    ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR deletor& operator=(deletor<Up, UpBackendDelete>&& other) noexcept(
        std::is_nothrow_assignable<BackendDelete, UpBackendDelete&&>::value) {
      *backend_deletor() = std::move(*other.backend_deletor());
      return *this;
    }

    inline ~deletor() noexcept(std::is_nothrow_destructible<BackendDelete>::value) {
      backend_deletor()->~BackendDelete();
    }

    void operator()(T* ptr) const {
      if (nullptr != ptr) {
        object_allocator_metrics_controller::add_destructor_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), reinterpret_cast<void*>(ptr));
        object_allocator_metrics_controller::add_deallocate_counter(
            object_allocator_metrics_controller::helper<T>::get_instance(), 1);
      }
      (*backend_deletor())(ptr);
    }

   private:
    template <class, class>
    friend struct UTIL_SYMBOL_VISIBLE deletor;

    inline void* backend_deletor_buffer() noexcept { return reinterpret_cast<void*>(&backend_delete_buffer_); }

    inline const void* backend_deletor_buffer() const noexcept {
      return reinterpret_cast<const void*>(&backend_delete_buffer_);
    }

    inline BackendDelete* backend_deletor() noexcept {
      return reinterpret_cast<BackendDelete*>(backend_deletor_buffer());
    }

    inline const BackendDelete* backend_deletor() const noexcept {
      return reinterpret_cast<const BackendDelete*>(backend_deletor_buffer());
    }

    atfw::util::nostd::aligned_storage_t<sizeof(BackendDelete), alignof(BackendDelete)> backend_delete_buffer_;
  };

  template <class T, class BackendAllocator = ::std::allocator<T>>
  struct UTIL_SYMBOL_VISIBLE allocator {
    using background_allocator_type = BackendAllocator;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    // constructors
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator() noexcept(
        std::is_nothrow_constructible<background_allocator_type>::value) {
      new (backend_allocator_buffer()) background_allocator_type();
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const BackendAllocator& backend) noexcept(
        std::is_nothrow_constructible<background_allocator_type, const BackendAllocator&>::value) {
      new (backend_allocator_buffer()) background_allocator_type(backend);
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const allocator& other) noexcept(
        std::is_nothrow_copy_constructible<background_allocator_type>::value) {
      new (backend_allocator_buffer()) background_allocator_type(*other.backend_allocator());
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(allocator&& other) noexcept(
        std::is_nothrow_move_constructible<background_allocator_type>::value) {
      new (backend_allocator_buffer()) background_allocator_type(std::move(*other.backend_allocator()));
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator& operator=(const allocator& other) noexcept(
        std::is_nothrow_copy_assignable<background_allocator_type>::value) {
      *backend_allocator() = *other.backend_allocator();
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator& operator=(allocator&& other) noexcept(
        std::is_nothrow_move_assignable<background_allocator_type>::value) {
      *backend_allocator() = std::move(*other.backend_allocator());
    }

    template <class U, class UBackendAllocator>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(const allocator<U, UBackendAllocator>& other) noexcept(
        std::is_nothrow_constructible<background_allocator_type, const UBackendAllocator&>::value) {
      new (backend_allocator_buffer()) background_allocator_type(*other.backend_allocator());
    }

    template <class U, class UBackendAllocator>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator(allocator<U, UBackendAllocator>&& other) noexcept(
        std::is_nothrow_constructible<background_allocator_type, UBackendAllocator&&>::value) {
      new (backend_allocator_buffer()) background_allocator_type(std::move(*other.backend_allocator()));
    }

    template <class U, class UBackendAllocator>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator&
    operator=(const allocator<U, UBackendAllocator>& other) noexcept(
        std::is_nothrow_assignable<BackendAllocator, const UBackendAllocator&>::value) {
      *backend_allocator() = *other.backend_allocator();
      return *this;
    }

    template <class U, class UBackendAllocator>
    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator&
    operator=(allocator<U, UBackendAllocator>&& other) noexcept(
        std::is_nothrow_assignable<BackendAllocator, UBackendAllocator&&>::value) {
      *backend_allocator() = std::move(*other.backend_allocator());
      return *this;
    }

    inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR ~allocator() noexcept(
        std::is_nothrow_destructible<background_allocator_type>::value) {
      backend_allocator()->~background_allocator_type();
    }

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

    template <class = atfw::util::nostd::enable_if_t<!std::is_const<pointer>::value>>
    inline const_pointer address(const_reference x) const noexcept {
      return &x;
    }

#  if ((defined(__cplusplus) && __cplusplus <= 201103L) || (defined(_MSVC_LANG) && _MSVC_LANG <= 201103L))
    inline size_type max_size() const { return backend_allocator()->max_size(); }
#  else
    inline size_type max_size() const noexcept { return backend_allocator()->max_size(); }
#  endif

    template <class U, class... Args>
    inline void construct(U* p, Args&&... args) {
      backend_allocator()->construct(p, std::forward<Args>(args)...);
      object_allocator_metrics_controller::add_constructor_counter_template<T>(to_mutable_address(p));
    }

    template <class U, class... Args>
    inline void destroy(U* p) {
      object_allocator_metrics_controller::add_destructor_counter_template<T>(to_mutable_address(p));
      backend_allocator()->destroy(p);
    }
#endif

#if ((defined(__cplusplus) && __cplusplus >= 202302L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)) && \
    defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
    EXPLICIT_NODISCARD_ATTR UTIL_CONFIG_CONSTEXPR std::allocation_result<T*, size_type> allocate_at_least(size_type n) {
      auto ret = backend_allocator()->allocate_at_least(n);
      object_allocator_metrics_controller::add_allocate_counter_template<T>(ret.second);
      return ret;
    }
#endif

#if ((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
    EXPLICIT_NODISCARD_ATTR inline ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR T* allocate(size_type n) {
      auto ret = backend_allocator()->allocate(n);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }
#elif ((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
    inline T* allocate(size_type n) {
      auto ret = backend_allocator()->allocate(n);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }

    inline T* allocate(size_type n, const void* hint) {
      auto ret = backend_allocator()->allocate(n, hint);
      if (nullptr != ret) {
        object_allocator_metrics_controller::add_allocate_counter_template<T>(n);
      }
      return ret;
    }

#else
    inline T* allocate(size_type n, const void* hint = nullptr) {
      auto ret = backend_allocator()->allocate(n, hint);
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
      backend_allocator()->deallocate(p, n);
    }

    friend inline bool operator==(const allocator& self,
                                  const allocator& other) noexcept(noexcept(*self.backend_allocator() ==
                                                                            *other.backend_allocator())) {
      return *self.backend_allocator() == *other.backend_allocator();
    }

    template <class U, class UBackendAllocator>
    friend inline bool operator==(const allocator& self, const allocator<U, UBackendAllocator>& other) noexcept(
        noexcept(*self.backend_allocator() == *other.backend_allocator())) {
      return *self.backend_allocator() == *other.backend_allocator();
    }

    friend inline bool operator!=(const allocator& self,
                                  const allocator& other) noexcept(noexcept(*self.backend_allocator() !=
                                                                            *other.backend_allocator())) {
      return *self.backend_allocator() != *other.backend_allocator();
    }

    template <class U, class UBackendAllocator>
    friend inline bool operator!=(const allocator& self, const allocator<U, UBackendAllocator>& other) noexcept(
        noexcept(*self.backend_allocator() != *other.backend_allocator())) {
      return *self.backend_allocator() != *other.backend_allocator();
    }

   private:
    template <class, class>
    friend struct UTIL_SYMBOL_VISIBLE allocator;

    inline void* backend_allocator_buffer() noexcept { return reinterpret_cast<void*>(&backend_allocator_buffer_); }

    inline const void* backend_allocator_buffer() const noexcept {
      return reinterpret_cast<const void*>(&backend_allocator_buffer_);
    }

    inline background_allocator_type* backend_allocator() noexcept {
      return reinterpret_cast<background_allocator_type*>(backend_allocator_buffer());
    }

    inline const background_allocator_type* backend_allocator() const noexcept {
      return reinterpret_cast<const background_allocator_type*>(backend_allocator_buffer());
    }

    atfw::util::nostd::aligned_storage_t<sizeof(background_allocator_type), alignof(background_allocator_type)>
        backend_allocator_buffer_;
  };

 public:
  template <class T, class... Args>
  UTIL_SYMBOL_VISIBLE inline static ::std::shared_ptr<T> make_shared(Args&&... args) {
    allocator<T, typename object_allocator_backend<T>::allocator> alloc;
    ::std::shared_ptr<T> ret = ::std::allocate_shared<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ret.get()));
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
          object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ret.get()));
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
            object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ptr));
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
            object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ptr));
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
            object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ptr));
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
            object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ptr));
      }
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), ::std::extent<T>::value);
    }

    return ret;
  }
#endif

  template <class T, class... Args>
  UTIL_SYMBOL_VISIBLE inline static atfw::util::memory::strong_rc_ptr<T> make_strong_rc(Args&&... args) {
    allocator<T, typename object_allocator_backend<T>::allocator> alloc;
    atfw::util::memory::strong_rc_ptr<T> ret =
        atfw::util::memory::allocate_strong_rc<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }

  template <class T, class Alloc, class... Args>
  UTIL_SYMBOL_VISIBLE inline static atfw::util::memory::strong_rc_ptr<type_traits::non_array<T>> allocate_strong_rc(
      const Alloc& backend_alloc, Args&&... args) {
    allocator<T, Alloc> alloc{backend_alloc};
    atfw::util::memory::strong_rc_ptr<T> ret =
        atfw::util::memory::allocate_strong_rc<T>(alloc, std::forward<Args>(args)...);

    if (ret) {
      object_allocator_metrics_controller::add_constructor_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), to_mutable_address(ret.get()));
      object_allocator_metrics_controller::add_allocate_counter(
          object_allocator_metrics_controller::helper<T>::get_instance(), 1);
    }

    return ret;
  }
};

}  // namespace memory
}  // namespace atframework

#if defined(__GNUC__) && !defined(__clang__) && !defined(__apple_build_version__)
#  if (__GNUC__ * 100 + __GNUC_MINOR__ * 10) >= 460
#    pragma GCC diagnostic pop
#  endif
#endif
