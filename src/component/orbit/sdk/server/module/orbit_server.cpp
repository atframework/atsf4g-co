#include "orbit_server.h"

#include "handle/handle_ss_rpc_controllertoserverservice.h"

#include <logic/logic_server_macro.h>

int orbit_server_module::init() {
  {
    // register all router managers
  }

  INIT_CALL_FN(handle::controllertoserverservice::register_handles_for_controllertoserverservice);
  return 0;
}

int orbit_server_module::stop() { return 0; }

int orbit_server_module::tick() {
  int ret = 0;
  return ret;
}