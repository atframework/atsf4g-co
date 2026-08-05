// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <atframe/atapp_config.h>
#include <atframe/connectors/atapp_connector_impl.h>

#include <cstdint>
#include <unordered_map>

#include <atframework/testing/runtime.h>

namespace atframework {
namespace testing {

class raw_transport;

// Mock connector of the "mock://" protocol. Outbound bytes are captured into the bound raw_transport
// engine and never touch a real socket. Modeled after atapp_connector_loopback.
class RPC_UNIT_TEST_API mock_connector : public atfw::atapp::atapp_connector_impl {
 public:
  mock_connector(atfw::atapp::app &owner, raw_transport &transport);
  ~mock_connector();

  mock_connector(const mock_connector &) = delete;
  mock_connector &operator=(const mock_connector &) = delete;

  gsl::string_view name() const noexcept override;
  uint32_t get_address_type(const atfw::atbus::channel::channel_address_t &addr) const noexcept override;

  int32_t on_start_listen(const atfw::atbus::channel::channel_address_t &addr) override;
  int32_t on_start_connect(const atfw::atapp::etcd_discovery_node &discovery, atfw::atapp::atapp_endpoint &endpoint,
                           const atfw::atbus::channel::channel_address_t &addr,
                           const atfw::atapp::atapp_connection_handle::ptr_t &handle) override;
  int32_t on_close_connection(atfw::atapp::atapp_connection_handle &handle) override;
  int32_t on_send_forward_request(atfw::atapp::atapp_connection_handle *handle, int32_t type, uint64_t *msg_sequence,
                                  gsl::span<const unsigned char> data,
                                  const atfw::atapp::protocol::atapp_metadata *metadata) override;
  void on_discovery_event(atfw::atapp::etcd_discovery_action_t,
                          const atfw::atapp::etcd_discovery_node::ptr_t &) override;

 private:
  raw_transport *transport_;
  std::unordered_map<uintptr_t, atfw::atapp::atapp_connection_handle::ptr_t> handles_;
};

}  // namespace testing
}  // namespace atframework
