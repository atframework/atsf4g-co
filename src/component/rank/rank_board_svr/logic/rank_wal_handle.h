#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.pb.h>
#include <protocol/pbdesc/com.struct.rank.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <distributed_system/wal_publisher.h>

#include <data/player_key_hash_helper.h>

#include <stdint.h>
#include <functional>
#include <memory>
#include "config/server_frame_build_feature.h"

class rank;

namespace rpc {
class context;
}

// Stateless object, no data need
struct rank_storage_type {};

struct rank_wal_publisher_context {
  std::reference_wrapper<rpc::context> context;  // 可能要发起异步任务
  std::reference_wrapper<int32_t> result_code;

  explicit rank_wal_publisher_context(rpc::context& ctx, int32_t& output_result);
};

struct rank_wal_publisher_log_action_getter {
  inline PROJECT_NAMESPACE_ID::DRankEventLog::EventCase operator()(
      const PROJECT_NAMESPACE_ID::DRankEventLog& log) noexcept {
    return log.event_case();
  }
};

using rank_wal_subscriber_private_data = util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::DRankSubscriberData>;

struct rank_log_action_hash_t {
  inline size_t operator()(const PROJECT_NAMESPACE_ID::DRankEventLog::EventCase& key) const noexcept {
    return std::hash<int>()(key);
  }
};

struct rank_log_action_equal_t {
  inline bool operator()(const PROJECT_NAMESPACE_ID::DRankEventLog::EventCase& l,
                         const PROJECT_NAMESPACE_ID::DRankEventLog::EventCase& r) const noexcept {
    return l == r;
  }
};

struct rank_wal_publisher_log_operator
    : public util::distributed_system::wal_log_operator<
          int64_t, PROJECT_NAMESPACE_ID::DRankEventLog, rank_wal_publisher_log_action_getter, std::less<int64_t>,
          rank_log_action_hash_t, rank_log_action_equal_t,
          atfw::memory::stl::allocator<PROJECT_NAMESPACE_ID::DRankEventLog>> {};

struct rank_wal_subscriber_type
    : public util::distributed_system::wal_subscriber<rank_wal_subscriber_private_data, uint64_t> {};

using rank_wal_publisher_type =
    util::distributed_system::wal_publisher<rank_storage_type, rank_wal_publisher_log_operator,
                                            rank_wal_publisher_context, rank*, rank_wal_subscriber_type>;

rank_wal_publisher_log_operator::strong_ptr<rank_wal_publisher_type> create_rank_publisher(rank&);