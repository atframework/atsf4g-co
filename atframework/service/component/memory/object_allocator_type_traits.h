// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_utils_build_feature.h>
#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <nostd/type_traits.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#ifdef max
#  undef max
#endif

#include "memory/object_allocator_def.h"
#include "memory/object_allocator_manager.h"
#include "memory/object_allocator_metrics.h"

namespace atframework {
namespace memory {
#if ((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
// Not all compilers support detected_or for template template_args now
#  if 0 && (defined(__cpp_template_template_args) && __cpp_template_template_args)
#    define object_allocator_manager_ALLOCATOR_NESTED_TYPE(TEST_CLASS, NESTED_TYPE, TYPE_ALIAS, DEFAULT_TYPE)       \
     private:                                                                                                       \
      template <class __TCNT>                                                                                       \
      using __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE = typename TEST_CLASS::NESTED_TYPE;      \
                                                                                                                    \
     public:                                                                                                        \
      using TYPE_ALIAS =                                                                                            \
          __nested_type_detected_or_t<DEFAULT_TYPE, __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE, \
                                      TEST_CLASS>
#  else
#    if defined(__cpp_concepts) && __cpp_concepts
#      define object_allocator_manager_ALLOCATOR_NESTED_TYPE(TEST_CLASS, NESTED_TYPE, TYPE_ALIAS, DEFAULT_TYPE) \
       private:                                                                                                 \
        template <class DefaultType, class TemplateType>                                                        \
        struct __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE {                                 \
          using type = DefaultType;                                                                             \
          using value_t = ::std::false_type;                                                                    \
        };                                                                                                      \
        template <class DefaultType, class TemplateType>                                                        \
          requires requires { typename TemplateType::NESTED_TYPE; }                                             \
        struct __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE<DefaultType, TemplateType> {      \
          using type = typename TemplateType::NESTED_TYPE;                                                      \
          using value_t = ::std::true_type;                                                                     \
        };                                                                                                      \
                                                                                                                \
       public:                                                                                                  \
        using TYPE_ALIAS =                                                                                      \
            typename __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE<DEFAULT_TYPE, TEST_CLASS>::type

#    else
#      define object_allocator_manager_ALLOCATOR_NESTED_TYPE(TEST_CLASS, NESTED_TYPE, TYPE_ALIAS, DEFAULT_TYPE) \
       private:                                                                                                 \
        template <class DefaultType, class TemplateType, class = void>                                          \
        struct __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE {                                 \
          using type = DefaultType;                                                                             \
          using value_t = ::std::false_type;                                                                    \
        };                                                                                                      \
        template <class DefaultType, class TemplateType>                                                        \
        struct __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE<                                  \
            DefaultType, TemplateType, ::std::void_t<typename TemplateType::NESTED_TYPE>> {                     \
          using type = typename TemplateType::NESTED_TYPE;                                                      \
          using value_t = ::std::true_type;                                                                     \
        };                                                                                                      \
                                                                                                                \
       public:                                                                                                  \
        using TYPE_ALIAS =                                                                                      \
            typename __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE<DEFAULT_TYPE, TEST_CLASS>::type
#    endif
#  endif
#else
#  define object_allocator_manager_ALLOCATOR_NESTED_TYPE(TEST_CLASS, NESTED_TYPE, TYPE_ALIAS, DEFAULT_TYPE)            \
   private:                                                                                                            \
    template <class __TCNT>                                                                                            \
    static                                                                                                             \
        typename __TCNT::NESTED_TYPE __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE##_helper(__TCNT*); \
    static DEFAULT_TYPE __object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE##_helper(...);                  \
                                                                                                                       \
   public:                                                                                                             \
    using TYPE_ALIAS = decltype(__object_allocator_manager_ALLOCATOR_NESTED_TYPE_##NESTED_TYPE##_helper(               \
        static_cast<TEST_CLASS*>(nullptr)))
#endif

// Inject std::allocator_traits for object_allocator_manager::allocator
template <class AllocatorSource, class TypeSource>
class __object_allocator_manager_allocator_rebind_detect_other {
  template <class AllocatorTarget, class TypeTarget>
  static UTIL_CONFIG_CONSTEXPR bool _object_allocator_manager_chk(
      typename AllocatorTarget::template rebind<TypeTarget>::other*) {
    return true;
  }

  template <class, class>
  static UTIL_CONFIG_CONSTEXPR bool _object_allocator_manager_chk(...) {
    return false;
  }

 public:
  static UTIL_CONFIG_CONSTEXPR const bool __value = _object_allocator_manager_chk<AllocatorSource, TypeSource>(nullptr);
};

#if !((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
template <class Allocator, class T>
UTIL_CONFIG_CONSTEXPR const bool __object_allocator_manager_allocator_rebind_detect_other<Allocator, T>::__value;
#endif

template <class Allocator, class T,
          bool = __object_allocator_manager_allocator_rebind_detect_other<Allocator, T>::__value>
struct __object_allocator_manager_allocator_allocator_rebind;

template <class Allocator, class T>
struct __object_allocator_manager_allocator_allocator_rebind<Allocator, T, true> {
  using __rebind_type_other = typename Allocator::template rebind<T>;
  using type = typename __rebind_type_other::other;
};

template <class Allocator>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool_helper;

template <class T, class BackendAllocator>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool_helper<
    object_allocator_manager::allocator<T, BackendAllocator>> : public ::std::true_type {};

template <class>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool_helper : public ::std::false_type {};

template <class Allocator, class T,
          bool = __object_allocator_manager_allocator_allocator_rebind_object_pool_helper<Allocator>::value>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool;

template <class Up, class BackendAllocator, class T>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool<
    object_allocator_manager::allocator<Up, BackendAllocator>, T, true> {
  // This is useless because we always use ::object_allocator_manager::allocator<Up, BackendAllocator>::rebind<T>::other
  // here
  using __rebind_backend_alloc = typename ::std::allocator_traits<BackendAllocator>::template rebind_alloc<T>;
  using type = object_allocator_manager::allocator<T, __rebind_backend_alloc>;
};

template <template <class, class...> class Allocator, class T, class Up, class... Args>
struct __object_allocator_manager_allocator_allocator_rebind_object_pool<Allocator<Up, Args...>, T, false> {
  using type = Allocator<T, Args...>;
};

template <template <class, class...> class Allocator, class T, class Up, class... Args>
struct __object_allocator_manager_allocator_allocator_rebind<Allocator<Up, Args...>, T, false> {
  using type =
      typename __object_allocator_manager_allocator_allocator_rebind_object_pool<Allocator<Up, Args...>, T>::type;
};

}  // namespace memory
}  // namespace atframework

namespace std {

template <class AllocValueType, class BackendAllocator>
struct ATFW_UTIL_SYMBOL_VISIBLE
allocator_traits<::atfw::memory::object_allocator_manager::allocator<AllocValueType, BackendAllocator>> {
  using allocator_type = ::atfw::memory::object_allocator_manager::allocator<AllocValueType, BackendAllocator>;
  using value_type = typename allocator_type::value_type;

#if ((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
// Not all compilers support detected_or for template template_args now
#  if 0 && (defined(__cpp_template_template_args) && __cpp_template_template_args)
#    if defined(__cpp_concepts) && __cpp_concepts >= 202002L
  // Implementation of the detection idiom (negative case).
  template <class DefaultType, template <class...> class DetectTemplateType, class... TemplateArgs>
  struct __nested_type_detected_or {
    using type = DefaultType;
    using value_t = ::std::false_type;
  };

  // Implementation of the detection idiom (positive case).
  template <class DefaultType, template <class...> class DetectTemplateType, class... TemplateArgs>
    requires requires { typename DetectTemplateType<TemplateArgs...>; }
  struct __nested_type_detected_or<DefaultType, DetectTemplateType, TemplateArgs...> {
    using type = DetectTemplateType<TemplateArgs...>;
    using value_t = ::std::true_type;
  };
#    else
  /// Implementation of the detection idiom (negative case).
  template <class DefaultType, class _AlwaysVoid, template <class...> class DetectTemplateType, class... TemplateArgs>
  struct __nested_type_detector {
    using type = DefaultType;
    using value_t = ::std::false_type;
  };

  /// Implementation of the detection idiom (positive case).
  template <class DefaultType, template <class...> class DetectTemplateType, class... TemplateArgs>
  struct __nested_type_detector<DefaultType, ::std::void_t<DetectTemplateType<TemplateArgs...>>, DetectTemplateType,
                                TemplateArgs...> {
    using type = DetectTemplateType<TemplateArgs...>;
    using value_t = ::std::true_type;
  };

  template <class DefaultType, template <class...> class DetectTemplateType, class... TemplateArgs>
  using __nested_type_detected_or = __nested_type_detector<DefaultType, void, DetectTemplateType, TemplateArgs...>;
#    endif  // __cpp_concepts

  template <class DefaultType, template <class...> class DetectTemplateType, class... TemplateArgs>
  using __nested_type_detected_or_t =
      typename __nested_type_detected_or<DefaultType, DetectTemplateType, TemplateArgs...>::type;
#  endif
#endif

  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, pointer, pointer, value_type*);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(
      allocator_type, const_pointer, const_pointer,
      typename ::std::pointer_traits<pointer>::template rebind<const value_type>);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, void_pointer, void_pointer,
                                                 typename ::std::pointer_traits<pointer>::template rebind<void>);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, const_void_pointer, const_void_pointer,
                                                 typename ::std::pointer_traits<pointer>::template rebind<const void>);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, difference_type, difference_type,
                                                 typename ::std::pointer_traits<pointer>::difference_type);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, size_type, size_type,
                                                 typename ::std::make_unsigned<difference_type>::type);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, propagate_on_container_copy_assignment,
                                                 propagate_on_container_copy_assignment, ::std::false_type);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, propagate_on_container_move_assignment,
                                                 propagate_on_container_move_assignment, ::std::false_type);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, propagate_on_container_swap,
                                                 propagate_on_container_swap, ::std::false_type);
  object_allocator_manager_ALLOCATOR_NESTED_TYPE(allocator_type, is_always_equal, is_always_equal,
                                                 typename ::std::is_empty<allocator_type>::type);

  template <typename U>
  using rebind_alloc =
      typename ::atfw::memory::__object_allocator_manager_allocator_allocator_rebind<allocator_type, U>::type;

  template <typename U>
  using rebind_traits = allocator_traits<rebind_alloc<U>>;

  EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR pointer
  allocate(allocator_type& a, size_type n) {
    return a.allocate(n);
  }

 private:
  // allocate
  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static auto _S_allocate(AllocOther& __a, size_type __n, const_void_pointer __hint,
                                                          int) -> decltype(__a.allocate(__n, __hint)) {
    return __a.allocate(__n, __hint);
  }

  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static pointer _S_allocate(AllocOther& __a, size_type __n, ...) {
    return __a.allocate(__n);
  }

  // construct
  template <typename U, typename... _Args>
  struct __construct_helper {
    template <typename AllocOther,
              typename = decltype(std::declval<AllocOther*>()->construct(std::declval<U*>(), std::declval<_Args>()...))>
    static ::std::true_type __test(int);

    template <typename>
    static ::std::false_type __test(...);

    using type = decltype(__test<allocator_type>(0));
    static UTIL_CONFIG_CONSTEXPR const bool value = type::value;
  };

  template <typename U, typename... _Args>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR
      atfw::util::nostd::enable_if_t<__construct_helper<U, _Args...>::value, void>
      _S_construct(allocator_type& __a, U* __p,
                   _Args&&... __args) noexcept(noexcept(__a.construct(__p, std::forward<_Args>(__args)...))) {
    __a.construct(__p, std::forward<_Args>(__args)...);
  }

  template <typename U, typename... _Args>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR atfw::util::nostd::enable_if_t<
      !__construct_helper<U, _Args...>::value && ::std::is_constructible<U, _Args...>::value, void>
  _S_construct(allocator_type&, U* __p, _Args&&... __args) noexcept(std::is_nothrow_constructible<U, _Args...>::value) {
    ::atfw::memory::object_allocator_metrics_controller::add_constructor_counter_template<value_type>(
        reinterpret_cast<void*>(__p));

#if ((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
    ::std::construct_at(__p, std::forward<_Args>(__args)...);
#else
    ::new ((void*)__p) U(std::forward<_Args>(__args)...);
#endif
  }

  // destroy
  template <typename AllocatorOther, typename U>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR auto _S_destroy(
      AllocatorOther& __a, U* __p, int) noexcept(noexcept(__a.destroy(__p))) -> decltype(__a.destroy(__p)) {
    __a.destroy(__p);
  }

  template <typename AllocatorOther, typename U>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR void _S_destroy(
      AllocatorOther&, U* __p, ...) noexcept(std::is_nothrow_destructible<U>::value) {
    ::atfw::memory::object_allocator_metrics_controller::add_destructor_counter_template<value_type>(
        reinterpret_cast<void*>(__p));

#if ((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
    ::std::destroy_at(__p);
#else
    __p->~U();
#endif
  }

  // max_size
  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR auto _S_max_size(const AllocOther& __a,
                                                                                                 int)
      -> decltype(__a.max_size()) {
    return __a.max_size();
  }

  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR size_type _S_max_size(const AllocOther&,
                                                                                                      ...) {
    return ::std::numeric_limits<size_type>::max();
  }

  // select_on_container_copy_construction
  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR auto _S_select(AllocOther& __a, int)
      -> decltype(__a.select_on_container_copy_construction()) {
    return __a.select_on_container_copy_construction();
  }

  template <typename AllocOther>
  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR AllocOther _S_select(AllocOther& __a,
                                                                                                     ...) {
    return __a;
  }

 public:
  EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR pointer
  allocate(allocator_type& a, size_type n, const_void_pointer hint) {
    return _S_allocate(a, n, hint, 0);
  }

#if ((defined(__cplusplus) && __cplusplus >= 202302L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)) && \
    defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
  EXPLICIT_NODISCARD_ATTR
  ATFW_UTIL_SYMBOL_VISIBLE inline static UTIL_CONFIG_CONSTEXPR ::std::allocation_result<pointer, size_type>
  allocate_at_least(allocator_type& a, size_type n) {
    return a.allocate_at_least(n);
  }
#endif

  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR void deallocate(allocator_type& a,
                                                                                                pointer p,
                                                                                                size_type n) {
    a.deallocate(p, n);
  }

  template <class T, class... Args>
  ATFW_UTIL_SYMBOL_VISIBLE inline static UTIL_CONFIG_CONSTEXPR void construct(
      allocator_type& a, T* p, Args&&... args) noexcept(noexcept(_S_construct(a, p, std::forward<Args>(args)...))) {
    _S_construct(a, p, std::forward<Args>(args)...);
  }

  template <class T>
  ATFW_UTIL_SYMBOL_VISIBLE inline static UTIL_CONFIG_CONSTEXPR void destroy(allocator_type& a,
                                                                            T* p) noexcept(noexcept(_S_destroy(a, p,
                                                                                                               0))) {
    _S_destroy(a, p, 0);
  }

  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR size_type
  max_size(const allocator_type& a) noexcept {
    return _S_max_size(a, 0);
  }

  ATFW_UTIL_SYMBOL_VISIBLE inline static ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR allocator_type
  select_on_container_copy_construction(const allocator_type& a) {
    return _S_select(a, 0);
  }
};
}  // namespace std
