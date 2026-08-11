// Copyright 2026 atframework
// Created by atsf4g-co orbit_room module migration

#pragma once

#include <design_pattern/singleton.h>

#include <rpc/rpc_common_types.h>

#include <config/extern_service_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>
#include <protocol/pbdesc/orbit_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "logic/room/orbit_room.h"

#include <memory/rc_ptr.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace rpc {
class context;
}  // namespace rpc

class orbit_room_manager : public util::design_pattern::singleton<orbit_room_manager> {
 public:
  int init();
  int reload();
  int tick();
  int stop();

  inline int32_t get_room_size() const noexcept { return static_cast<int32_t>(room_index_by_client_id_.size()); }

  rpc::result_code_type create_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitCreateRoomReq& req,
                                    uint64_t match_server_id);
  int32_t init_user(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitUserInitReq& req);
  rpc::result_code_type join_room(rpc::context& ctx, const PROJECT_NAMESPACE_ID::SSOrbitUserJoinRoomReq& req);

  rpc::result_code_type on_client_start(rpc::context& ctx, const std::string& client_id, const std::string& client_addr,
                                        const std::string& payload);
  rpc::result_code_type on_client_end(rpc::context& ctx, const std::string& client_id, const std::string& payload,
                                      atfw::orbit::EnClientExitReason exit_reason, int32_t exit_code);
  rpc::result_code_type on_user_finish(
      rpc::context& ctx, const std::string& client_id,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DOrbitUserFinishResult>& results);

  static bool fill_client_start_args_from_template_id(uint32_t template_id, const std::string& client_id,
                                                      atfw::orbit::DAgentClientStartArgs& args);

 private:
  atfw::util::memory::strong_rc_ptr<orbit_room> get_room(const std::string& client_id) noexcept;

  bool is_inited_ = false;
  bool is_closing_ = false;

  std::unordered_map<std::string, atfw::util::memory::strong_rc_ptr<orbit_room>> room_index_by_client_id_;
};
