#include "task_action_register_controller.pseudo.h"

namespace atorbit {
namespace dsm {
namespace service {
namespace logic {
namespace action {

namespace {

static app::rpc_result_t write_register_controller_response(app::rpc_context_t& rpc_context,
                                                            const topology::controller_registration_t& registration) {
  rpc_context.register_controller_response_written = true;
  rpc_context.registered_controller_id = registration.controller_id;
  rpc_context.registered_region = registration.region;
  return 0;
}

}  // namespace

app::rpc_result_t run_task_action_register_controller(app::runtime_handle_t& runtime,
                                                      app::rpc_context_t& rpc_context,
                                                      const app::RegisterControllerReq& request,
                                                      topology::cluster_catalog& cluster_catalog) {
  (void)runtime;
  if (!request.has_controller() || 0 == request.controller_id() || nullptr == request.region()) {
    app::write_invalid_request_log(rpc_context);
    return app::build_rpc_error(app::ERROR_CODE_INVALID_ARGUMENT);
  }

  topology::controller_registration_t registration;
  registration.controller_id = request.controller_id();
  registration.region = request.region();
  registration.controller_addr = request.controller_addr();

  auto catalog_result = cluster_catalog.upsert_controller(registration);
  if (catalog_result != 0) {
    return app::build_rpc_error(catalog_result);
  }

  return write_register_controller_response(rpc_context, registration);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsm
}  // namespace atorbit