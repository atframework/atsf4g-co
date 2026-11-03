// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#pragma once

#include <memory/rc_ptr.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.mail.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_stl_unordered_map.h>

#include <config/server_frame_build_feature.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

// PROJECT_NAMESPACE_BEGIN
//     class DMailRecord;
//     class DMailRecord;
// PROJECT_NAMESPACE_END

struct mail_data_t {
  std::unique_ptr<PROJECT_NAMESPACE_ID::DMailRecord> record;
  std::unique_ptr<PROJECT_NAMESPACE_ID::DMailContent> content;
};
using mail_data_ptr_t = atfw::util::memory::strong_rc_ptr<mail_data_t>;
using mail_index_t = std::unordered_map<int64_t, mail_data_ptr_t>;
struct mail_box_t {
  mail_index_t mails;

  int32_t future_mail_count;
  std::chrono::system_clock::time_point future_cache_expire;

  inline mail_box_t() noexcept : future_mail_count(0), future_cache_expire(std::chrono::system_clock::from_time_t(0)) {}
};
using mail_box_ptr_t = atfw::util::memory::strong_rc_ptr<mail_box_t>;
using mail_box_const_ptr_t = atfw::util::memory::strong_rc_ptr<const mail_box_t>;
using mail_record_map_t = atfw::memory::stl::unordered_map<int64_t, PROJECT_NAMESPACE_ID::DMailRecord>;
