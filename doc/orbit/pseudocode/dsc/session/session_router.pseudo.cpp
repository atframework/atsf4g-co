#include "session_router.pseudo.h"

namespace atorbit {
namespace dsc {
namespace session {

namespace {

static bool same_ds_key(const ds_composite_key_t& left, const ds_composite_key_t& right) {
  return left.dsa_id == right.dsa_id && left.ds_id == right.ds_id;
}

}  // namespace

result_code_t session_router::connect(unique_id_t unique_id,
                                      connection_handle_t connection_handle,
                                      const char* controller_route_key) {
  if (!validate_connect_request(unique_id, connection_handle, controller_route_key)) {
    return -1;
  }

  if (has_duplicate_live_connection(unique_id)) {
    return -2;
  }

  auto* session = find_session(unique_id);
  if (nullptr == session) {
    session = allocate_session(unique_id);
  }

  if (nullptr == session) {
    return -3;
  }

  session->connection_handle = connection_handle;
  session->controller_route_key = controller_route_key;
  session->state = session_state_t::k_connected;
  session->occupied = true;
  return 0;
}

result_code_t session_router::reconnect(unique_id_t unique_id,
                                        connection_handle_t connection_handle,
                                        const char* controller_route_key,
                                        unsigned long long last_received_seq) {
  if (!validate_connect_request(unique_id, connection_handle, controller_route_key)) {
    return -1;
  }

  auto* session = find_session(unique_id);
  if (nullptr == session) {
    return -2;
  }

  if (session->state == session_state_t::k_connected) {
    return -3;
  }

  if (!can_resume_on_same_route(*session, controller_route_key)) {
    return -4;
  }

  session->connection_handle = connection_handle;
  session->last_received_seq = last_received_seq;
  session->state = session_state_t::k_connected;
  return 0;
}

result_code_t session_router::mark_disconnected(unique_id_t unique_id, connection_handle_t connection_handle) {
  auto* session = find_session(unique_id);
  if (nullptr == session || session->connection_handle != connection_handle) {
    return -1;
  }

  session->connection_handle = 0;
  session->state = session_state_t::k_disconnected;
  return 0;
}

result_code_t session_router::bind_ds_owner(unique_id_t unique_id, const ds_composite_key_t& ds_key) {
  auto* session = find_session(unique_id);
  if (nullptr == session || !is_valid_ds_key(ds_key)) {
    return -1;
  }

  if (has_owned_ds(*session, ds_key)) {
    return 0;
  }

  if (session->ds_count >= 32) {
    return -2;
  }

  session->ds_list[session->ds_count++] = ds_key;
  return 0;
}

result_code_t session_router::release_ds_owner(const ds_composite_key_t& ds_key) {
  if (!is_valid_ds_key(ds_key)) {
    return -1;
  }

  for (auto& session : sessions_) {
    if (!session.occupied) {
      continue;
    }

    for (unsigned long long index = 0; index < session.ds_count; ++index) {
      if (!same_ds_key(session.ds_list[index], ds_key)) {
        continue;
      }

      for (unsigned long long shift_index = index + 1; shift_index < session.ds_count; ++shift_index) {
        session.ds_list[shift_index - 1] = session.ds_list[shift_index];
      }

      --session.ds_count;
      session.ds_list[session.ds_count] = {};
      return 0;
    }
  }

  return -2;
}

bool session_router::validate_ds_owner(unique_id_t unique_id, const ds_composite_key_t& ds_key) const {
  const auto* session = find_session(unique_id);
  if (nullptr == session || !is_valid_ds_key(ds_key)) {
    return false;
  }

  return has_owned_ds(*session, ds_key);
}

unique_id_t session_router::find_owner_unique_id(const ds_composite_key_t& ds_key) const {
  if (!is_valid_ds_key(ds_key)) {
    return 0;
  }

  for (const auto& session : sessions_) {
    if (session.occupied && has_owned_ds(session, ds_key)) {
      return session.unique_id;
    }
  }

  return 0;
}

bool session_router::is_connected(unique_id_t unique_id) const {
  const auto* session = find_session(unique_id);
  if (nullptr == session) {
    return false;
  }

  return session->state == session_state_t::k_connected;
}

unsigned long long session_router::get_owned_ds_count(unique_id_t unique_id) const {
  const auto* session = find_session(unique_id);
  if (nullptr == session) {
    return 0;
  }

  return session->ds_count;
}

bool session_router::validate_connect_request(unique_id_t unique_id,
                                              connection_handle_t connection_handle,
                                              const char* controller_route_key) const {
  if (0 == unique_id || 0 == connection_handle || nullptr == controller_route_key) {
    return false;
  }

  return true;
}

bool session_router::is_valid_ds_key(const ds_composite_key_t& ds_key) const {
  if (0 == ds_key.dsa_id || 0 == ds_key.ds_id) {
    return false;
  }

  return true;
}

session_router::external_session_t* session_router::find_session(unique_id_t unique_id) {
  for (auto& session : sessions_) {
    if (session.occupied && session.unique_id == unique_id) {
      return &session;
    }
  }

  return nullptr;
}

const session_router::external_session_t* session_router::find_session(unique_id_t unique_id) const {
  for (const auto& session : sessions_) {
    if (session.occupied && session.unique_id == unique_id) {
      return &session;
    }
  }

  return nullptr;
}

session_router::external_session_t* session_router::allocate_session(unique_id_t unique_id) {
  for (auto& session : sessions_) {
    if (!session.occupied) {
      session.unique_id = unique_id;
      session.occupied = true;
      session.ds_count = 0;
      session.last_received_seq = 0;
      session.state = session_state_t::k_disconnected;
      return &session;
    }
  }

  return nullptr;
}

bool session_router::has_duplicate_live_connection(unique_id_t unique_id) const {
  const auto* session = find_session(unique_id);
  if (nullptr == session) {
    return false;
  }

  return session->state == session_state_t::k_connected;
}

bool session_router::can_resume_on_same_route(const external_session_t& session, const char* controller_route_key) const {
  if (nullptr == session.controller_route_key || nullptr == controller_route_key) {
    return false;
  }

  return session.controller_route_key == controller_route_key;
}

bool session_router::has_owned_ds(const external_session_t& session, const ds_composite_key_t& ds_key) const {
  for (unsigned long long index = 0; index < session.ds_count; ++index) {
    if (same_ds_key(session.ds_list[index], ds_key)) {
      return true;
    }
  }

  return false;
}

}  // namespace session
}  // namespace dsc
}  // namespace atorbit