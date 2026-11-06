# Orbit Guide

Use for Orbit room/user lifecycle, Match/Lobby handoff, DTMQ room events, DS callbacks, and the example AutoClient.
For controller/agent process management or SDK structure, start with `doc/docs/components/orbit.md` and the owning
component sources.

## Authoritative sources

- `src/orbitsvr/protocol/protocol/pbdesc/orbit_service.proto`: Match-facing create/join/end-join protocols.
- `src/orbitsvr/service/logic/room/orbit_room_manager.h/.cpp` and `orbit_room.h/.cpp`: room state, user init/finish,
  timeouts, notifications, and event publication.
- `src/lobbysvr/service/logic/orbit/user_orbit_manager.cpp`: Lobby room data and result handling.
- `src/matchsvr/service/logic/action/task_action_orbit_room_end_join.cpp` and
  `task_action_orbit_room_user_quit.cpp`: Match-side callback implementation status.
- `src/component/GameSharedComponent/Orbit/`: DS runtime, SDK, tests, and AutoClient.

## Review checks

- Trace `orbit_room_create -> DS start/room_ready -> orbit_user_join -> user_init -> finish/end_join` and record room
  ownership, `is_last_one`, absolute deadlines, and late/duplicate callback behavior at each await boundary.
- `room_ready` returns connection data, player tokens, and timing to Match. Orbit room events use DTMQ channel 101;
  Lobby exposes room key/address/token/init data through `user_get_info(need_user_orbit_room)`.
- Orbit chat type 102 is only a configuration/protocol reservation until actual room subscription, send, and CS sync
  paths exist. Do not conflate it with the room-event channel.
- Match EndJoin/UserQuit actions currently return success as placeholders, and Lobby result handling retains a TODO.
  Do not claim completed Match state closure or DS settlement. Add tests for the real behavior when implementing them.
- AutoClient is C++14-compatible and uses `atfw::util::file_system` for paths. Do not reintroduce `std::filesystem` or
  a target-level C++17 requirement; third-party dependencies have separate toolchain requirements.
- Verify both payload and actual delivery for room/user notifications, including duplicate and late events. Keep
  matching-team, Orbit, and chat documentation synchronized when changing these contracts.
