// Copyright 2026 atframework

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

class task_action_orbit_room_ready
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq,
                                     PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyReq,
                                             PROJECT_NAMESPACE_ID::SSMatchingOrbitRoomReadyRsp>;
  using base_type::operator();

  GAME_SERVICE_API explicit task_action_orbit_room_ready(dispatcher_start_data_type&& param);
  GAME_SERVICE_API ~task_action_orbit_room_ready() override;
  GAME_SERVICE_API const char* name() const override;
  GAME_SERVICE_API result_type operator()() override;
  GAME_SERVICE_API int on_success() override;
  GAME_SERVICE_API int on_failed() override;
};
