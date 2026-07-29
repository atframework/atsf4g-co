// Copyright 2024 atframework

#pragma once

#include <utility>  // IWYU pragma: keep

#include "memory/rc_ptr.h"  // IWYU pragma: keep

#include "memory/object_allocator.h"  // IWYU pragma: keep

#include "config/server_frame_build_feature.h"

#include "config/excel/config_traits.h"  // IWYU pragma: keep

namespace excel {

namespace traits {
template <>
struct EXCEL_CONFIG_SYMBOL_VISIBLE config_traits<type_guard> : public type_guard {
  template <class Y>
  using shared_ptr = atfw::util::memory::strong_rc_ptr<Y>;

  template <class Y, class... Args>
  inline static atfw::util::memory::strong_rc_ptr<Y> make_shared(Args&&... args) {
    // Some versions of STL have bug and will cause warnings by mistake, which may trigger -Werror/-WX to fail the
    // build. Use include guard to ignore them.
    // NOLINTNEXTLINE(build/include,readability-duplicate-include)
#include "config/compiler/internal/stl_compact_prefix.h.inc"  // IWYU pragma: keep
    return atfw::memory::stl::make_strong_rc<Y>(std::forward<Args>(args)...);
    // NOLINTNEXTLINE(build/include,readability-duplicate-include)
#include "config/compiler/internal/stl_compact_suffix.h.inc"  // IWYU pragma: keep
  }

  template <class Y, class... Args>
  inline static atfw::util::memory::strong_rc_ptr<Y> const_pointer_cast(Args&&... args) {
    return atfw::util::memory::const_pointer_cast<Y>(std::forward<Args>(args)...);
  }
};

}  // namespace traits

#ifndef EXCEL_CONFIG_LOADER_TRAITS
#  define EXCEL_CONFIG_LOADER_TRAITS
using excel_config_type_traits = ::excel::traits::config_traits<::excel::traits::type_guard>;
#endif

}  // namespace excel
