// Copyright 2024 atframework

#pragma once

#include <utility>

#include "memory/rc_ptr.h"

#include "memory/object_allocator.h"

#include "config/server_frame_build_feature.h"

#include "config/excel/config_traits.h"

namespace excel {

namespace traits {
template <>
struct EXCEL_CONFIG_SYMBOL_VISIBLE config_traits<type_guard> : public type_guard {
  template <class Y>
  using shared_ptr = util::memory::strong_rc_ptr<Y>;

  template <class Y, class... Args>
  inline static util::memory::strong_rc_ptr<Y> make_shared(Args&&... args) {
    return atfw::memory::stl::make_strong_rc<Y>(std::forward<Args>(args)...);
  }

  template <class Y, class... Args>
  inline static util::memory::strong_rc_ptr<Y> const_pointer_cast(Args&&... args) {
    return util::memory::const_pointer_cast<Y>(std::forward<Args>(args)...);
  }
};

}  // namespace traits

#ifndef EXCEL_CONFIG_LOADER_TRAITS
#  define EXCEL_CONFIG_LOADER_TRAITS
using excel_config_type_traits = ::excel::traits::config_traits<::excel::traits::type_guard>;
#endif

}  // namespace excel
