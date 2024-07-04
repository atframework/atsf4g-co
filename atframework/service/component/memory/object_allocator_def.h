// Copyright 2024 atframework
// Created by owent

#pragma once

#include <config/atframe_utils_build_feature.h>
#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <config/atframe_services_build_feature.h>

#include <type_traits>

#if (!defined(__cplusplus) && !defined(_MSVC_LANG)) || \
    !((defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L))
#  define ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR
#else
#  define ATFRAMEWORK_OBJECT_ALLOCATOR_CONSTEXPR constexpr
#endif

namespace atframework {
namespace memory {
namespace type_traits {
#if defined(__cpp_concepts) && __cpp_concepts
template <typename T>
  requires(!::std::is_array<T>::value)
using non_array = T;
#else
template <typename T>
using non_array = typename ::std::enable_if<!::std::is_array<T>::value, T>::type;
#endif

/// @cond undocumented
template <typename T>
struct __is_array_known_bounds : public ::std::false_type {};

template <typename T, ::std::size_t ArraySize>
struct __is_array_known_bounds<T[ArraySize]> : public ::std::true_type {};

template <typename T>
struct __is_array_unknown_bounds : public ::std::false_type {};

template <typename T>
struct __is_array_unknown_bounds<T[]> : public ::std::true_type {};

// Constraint for overloads taking array types with unknown bound, U[].
#if defined(__cpp_concepts) && __cpp_concepts
template <typename T>
  requires ::std::is_array<T>::value && (::std::extent<T>::value == 0)
using unbounded_array = T;
#else
template <typename T>
using unbounded_array = typename ::std::enable_if<__is_array_unknown_bounds<T>::value, T>::type;
#endif

// Constraint for overloads taking array types with known bound, U[N].
#if defined(__cpp_concepts) && __cpp_concepts
template <typename T>
  requires(::std::extent<T>::value != 0)
using bounded_array = T;
#else
template <typename T>
using bounded_array = typename ::std::enable_if<__is_array_known_bounds<T>::value, T>::type;
#endif

}  // namespace type_traits
}  // namespace memory
}  // namespace atframework
