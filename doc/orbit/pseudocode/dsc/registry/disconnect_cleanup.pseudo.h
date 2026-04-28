#pragma once

#include "../session/session_router.pseudo.h"

// Phase 3.5 / 3.12
// 目标: 固化 DS 退出、外部断线和 agent 断线时的会话清理入口。
// 未来真实落点: src/dsc/registry/disconnect_cleanup.cpp

namespace atorbit {
namespace dsc {
namespace registry {

struct ds_exit_cleanup_result_t {
  bool found_owner = false;
  unsigned long long owner_unique_id = 0;
  unsigned long long remaining_ds_count = 0;
};

class disconnect_cleanup {
public:
  ds_exit_cleanup_result_t cleanup_ds_exit(session::session_router& session_router,
                                           const session::ds_composite_key_t& ds_key);
  int cleanup_external_disconnect(session::session_router& session_router,
                                  unsigned long long unique_id,
                                  unsigned long long connection_handle);
};

}  // namespace registry
}  // namespace dsc
}  // namespace atorbit