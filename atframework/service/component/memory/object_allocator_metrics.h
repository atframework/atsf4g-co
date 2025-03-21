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

#include <config/atframe_services_build_feature.h>

#include <string>

#if defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI
#  include <typeinfo>
#endif

#if (!defined(__cplusplus) && !defined(_MSVC_LANG)) || \
    !((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
#  define ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR
#else
#  define ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR constexpr
#endif

namespace atframework {
namespace memory {

struct ATFW_UTIL_SYMBOL_VISIBLE object_allocator_metrics {
  gsl::string_view raw_name;
  gsl::string_view demangle_name;
  size_t unit_size = 0;

  size_t allocate_counter = 0;
  size_t deallocate_counter = 0;
  size_t constructor_counter = 0;
  size_t destructor_counter = 0;

  ATFW_UTIL_FORCEINLINE object_allocator_metrics() noexcept {}
};

class object_allocator_metrics_controller {
 public:
  struct object_allocator_metrics_storage;

 private:
  template <class T>
  ATFW_UTIL_FORCEINLINE static const char* guess_raw_name() {
#if defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI
#  if defined(_MSC_VER)
    return typeid(T).raw_name();
#  else
    return typeid(T).name();
#  endif
#elif defined(_MSC_VER)
    return __FUNCDNAME__;
#else
    return nullptr;
#endif
  }

  template <class T>
  ATFW_UTIL_FORCEINLINE static const char* guess_pretty_name() {
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
  }

  ATFRAME_SERVICE_COMPONENT_MACRO_API static ::std::string try_parse_raw_name(const char* input);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static ::std::string try_parse_demangle_name(const char* input);

  ATFRAME_SERVICE_COMPONENT_MACRO_API static object_allocator_metrics_storage*
  mutable_object_allocator_metrics_for_type(::std::string raw_name, ::std::string demangle_name, size_t unit_size,
                                            bool& destroyed_flag);

 public:
  template <class T>
  ATFW_UTIL_FORCEINLINE static ::std::string parse_raw_name() {
    return try_parse_raw_name(
        guess_raw_name<typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type>());
  }

  template <class T>
  ATFW_UTIL_FORCEINLINE static ::std::string parse_demangle_name() {
    return try_parse_demangle_name(
        guess_pretty_name<typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type>());
  }

  template <class T>
  struct ATFW_UTIL_SYMBOL_VISIBLE helper {
    static object_allocator_metrics_storage* get_instance() {
      static bool object_statistics_destroyed = false;
      static object_allocator_metrics_storage* object_statistics_inst = mutable_object_allocator_metrics_for_type(
          parse_raw_name<T>(), parse_demangle_name<T>(),
          sizeof(typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type),
          object_statistics_destroyed);
      if (object_statistics_destroyed) {
        return nullptr;
      }
      return object_statistics_inst;
    }
  };  // namespace memory

  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_constructor_counter(object_allocator_metrics_storage* target,
                                                                          void*);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_allocate_counter(object_allocator_metrics_storage* target,
                                                                       size_t count);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_destructor_counter(object_allocator_metrics_storage* target,
                                                                         void*);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_deallocate_counter(object_allocator_metrics_storage* target,
                                                                         size_t count);

  template <class U>
  ATFW_UTIL_FORCEINLINE static void add_constructor_counter_template(void* p) {
    if (nullptr != p) {
      add_constructor_counter(helper<U>::get_instance(), p);
    }
  }

  template <class U>
  ATFW_UTIL_FORCEINLINE static void add_allocate_counter_template(size_t count) {
    add_allocate_counter(helper<U>::get_instance(), count);
  }

  template <class U>
  ATFW_UTIL_FORCEINLINE static void add_destructor_counter_template(void* p) {
    if (nullptr != p) {
      add_destructor_counter(helper<U>::get_instance(), p);
    }
  }

  template <class U>
  ATFW_UTIL_FORCEINLINE static void add_deallocate_counter_template(size_t count) {
    add_deallocate_counter(helper<U>::get_instance(), count);
  }

  ATFRAME_SERVICE_COMPONENT_MACRO_API static void foreach_object_statistics(
      atfw::util::nostd::function_ref<void(const object_allocator_metrics&)> fn);
};  // namespace atframework

}  // namespace memory
}  // namespace atframework
