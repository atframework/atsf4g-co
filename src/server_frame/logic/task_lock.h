#pragma once

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <memory>
#include <unordered_set>

#include <rpc/rpc_common_types.h>

namespace rpc {
class context;
}

class player_cache;

class task_lock : public std::enable_shared_from_this<task_lock> {
 public:
  ATFW_UTIL_FORCEINLINE task_lock() : user_id_(0) {}
  SERVER_FRAME_API void init(uint64_t user_id);
  SERVER_FRAME_API void init_task(uint64_t id);
  SERVER_FRAME_API void remove_task(uint64_t id);
  ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type wait_task(rpc::context &ctx);

 private:
  std::unordered_set<uint64_t> id_;
  uint64_t user_id_;
};

class task_lock_guard {
 public:
  SERVER_FRAME_API task_lock_guard(std::shared_ptr<player_cache> player, uint64_t task_id);
  SERVER_FRAME_API ~task_lock_guard();

 private:
  std::weak_ptr<player_cache> ptr_;
  uint64_t task_id_;
};
