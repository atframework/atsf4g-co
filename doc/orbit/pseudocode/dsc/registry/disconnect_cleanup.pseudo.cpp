#include "disconnect_cleanup.pseudo.h"

namespace atorbit {
namespace dsc {
namespace registry {

ds_exit_cleanup_result_t disconnect_cleanup::cleanup_ds_exit(session::session_router& session_router,
                                                             const session::ds_composite_key_t& ds_key) {
  ds_exit_cleanup_result_t result;
  result.owner_unique_id = session_router.find_owner_unique_id(ds_key);
  if (0 == result.owner_unique_id) {
    return result;
  }

  if (session_router.release_ds_owner(ds_key) != 0) {
    return result;
  }

  result.found_owner = true;
  result.remaining_ds_count = session_router.get_owned_ds_count(result.owner_unique_id);
  return result;
}

int disconnect_cleanup::cleanup_external_disconnect(session::session_router& session_router,
                                                    unsigned long long unique_id,
                                                    unsigned long long connection_handle) {
  return session_router.mark_disconnected(unique_id, connection_handle);
}

}  // namespace registry
}  // namespace dsc
}  // namespace atorbit