#pragma once

// Phase 4.6
// 目标: 固化 Unique ID 唯一连接、sticky 重连继承和 DS owner 校验。
// 未来真实落点: src/dsc/session/session_router.cpp

namespace atorbit {
namespace dsc {
namespace session {

using result_code_t = int;
using unique_id_t = unsigned long long;
using connection_handle_t = unsigned long long;

enum class session_state_t {
  k_disconnected = 0,
  k_connected = 1,
};

struct ds_composite_key_t {
  unsigned long long dsa_id = 0;
  unsigned long long ds_id = 0;
};

class session_router {
public:
  result_code_t connect(unique_id_t unique_id, connection_handle_t connection_handle, const char* controller_route_key);
  result_code_t reconnect(unique_id_t unique_id,
                          connection_handle_t connection_handle,
                          const char* controller_route_key,
                          unsigned long long last_received_seq);
  result_code_t mark_disconnected(unique_id_t unique_id, connection_handle_t connection_handle);
  result_code_t bind_ds_owner(unique_id_t unique_id, const ds_composite_key_t& ds_key);
  result_code_t release_ds_owner(const ds_composite_key_t& ds_key);
  bool validate_ds_owner(unique_id_t unique_id, const ds_composite_key_t& ds_key) const;
  unique_id_t find_owner_unique_id(const ds_composite_key_t& ds_key) const;
  bool is_connected(unique_id_t unique_id) const;
  unsigned long long get_owned_ds_count(unique_id_t unique_id) const;

private:
  struct external_session_t {
    unique_id_t unique_id = 0;
    connection_handle_t connection_handle = 0;
    const char* controller_route_key = nullptr;
    unsigned long long last_received_seq = 0;
    ds_composite_key_t ds_list[32];
    unsigned long long ds_count = 0;
    session_state_t state = session_state_t::k_disconnected;
    bool occupied = false;
  };

private:
  bool validate_connect_request(unique_id_t unique_id,
                                connection_handle_t connection_handle,
                                const char* controller_route_key) const;
  bool is_valid_ds_key(const ds_composite_key_t& ds_key) const;
  external_session_t* find_session(unique_id_t unique_id);
  const external_session_t* find_session(unique_id_t unique_id) const;
  external_session_t* allocate_session(unique_id_t unique_id);
  bool has_duplicate_live_connection(unique_id_t unique_id) const;
  bool can_resume_on_same_route(const external_session_t& session, const char* controller_route_key) const;
  bool has_owned_ds(const external_session_t& session, const ds_composite_key_t& ds_key) const;

private:
  external_session_t sessions_[64];
};

}  // namespace session
}  // namespace dsc
}  // namespace atorbit