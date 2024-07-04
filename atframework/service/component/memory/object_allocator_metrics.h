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

#include <atomic>
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

struct UTIL_SYMBOL_VISIBLE object_allocator_metrics {
  std::string raw_name;
  std::string demangle_name;
  size_t unit_size = 0;

  size_t allocate_counter = 0;
  size_t deallocate_counter = 0;
  size_t constructor_counter = 0;
  size_t destructor_counter = 0;

  UTIL_FORCEINLINE object_allocator_metrics() noexcept {}
};

class object_allocator_metrics_controller {
 private:
  template <class T>
  UTIL_FORCEINLINE static const char* guess_raw_name() {
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
  UTIL_FORCEINLINE static const char* guess_pretty_name() {
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
  }

  ATFRAME_SERVICE_COMPONENT_MACRO_API static ::std::string try_parse_raw_name(const char* input);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static ::std::string try_parse_demangle_name(const char* input);

  ATFRAME_SERVICE_COMPONENT_MACRO_API static object_allocator_metrics* mutable_object_allocator_metrics_for_type(
      ::std::string raw_name, ::std::string demangle_name, size_t unit_size);

 public:
  template <class T>
  struct UTIL_SYMBOL_VISIBLE helper {
    static object_allocator_metrics* get_instance() {
      static object_allocator_metrics* object_statistics_inst = mutable_object_allocator_metrics_for_type(
          try_parse_raw_name(
              guess_raw_name<typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type>()),
          try_parse_demangle_name(
              guess_pretty_name<typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type>()),
          sizeof(typename ::std::remove_reference<typename ::std::remove_cv<T>::type>::type));
      return object_statistics_inst;
    }
  };

  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_constructor_counter(object_allocator_metrics* target, void*);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_allocate_counter(object_allocator_metrics* target, size_t count);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_destructor_counter(object_allocator_metrics* target, void*);
  ATFRAME_SERVICE_COMPONENT_MACRO_API static void add_deallocate_counter(object_allocator_metrics* target,
                                                                         size_t count);

  template <class U>
  UTIL_FORCEINLINE static void add_constructor_counter_template(void* p) {
    if (nullptr != p) {
      add_constructor_counter(helper<U>::get_instance(), p);
    }
  }

  template <class U>
  UTIL_FORCEINLINE static void add_allocate_counter_template(size_t count) {
    add_allocate_counter(helper<U>::get_instance(), count);
  }

  template <class U>
  UTIL_FORCEINLINE static void add_destructor_counter_template(void* p) {
    if (nullptr != p) {
      add_destructor_counter(helper<U>::get_instance(), p);
    }
  }

  template <class U>
  UTIL_FORCEINLINE static void add_deallocate_counter_template(size_t count) {
    add_deallocate_counter(helper<U>::get_instance(), count);
  }

  ATFRAME_SERVICE_COMPONENT_MACRO_API static void foreach_object_statistics(
      util::nostd::function_ref<void(const object_allocator_metrics&)> fn);
};

}  // namespace memory
}  // namespace atframework
