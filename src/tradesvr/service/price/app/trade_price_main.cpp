// Copyright 2026 atframework

#include "shared/trade_service_main.h"

#include <logic/logic_server_macro.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/trade_price.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "app/handle_ss_rpc_tradepriceservice.atfw.gen.h"

namespace {
class main_module final : public atfw::atapp::module_impl {
 public:
  const char* name() const override { return "trade_price_main_module"; }

  int init() override {
    INIT_CALL_FN(handle::trade_price::register_handles_for_tradepriceservice);
    return 0;
  }
};
}  // namespace

int main(int argc, char* argv[]) {
  return trade_service::run<main_module>(
      argc, argv, __FILE__, [](atfw::atapp::app& app, logic_config&, logic_config::server_instance_config_ptr& config) {
        auto settings = atfw::component::memory::stl::make_strong_rc<atfw::trade::config::tradesvr_price_cfg>();
        app.parse_configures_into(*settings, "tradesvr_price", "ATAPP_TRADESVR_PRICE");
        config = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(settings);
      });
}
