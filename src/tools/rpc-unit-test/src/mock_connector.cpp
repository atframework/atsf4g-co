// Copyright 2026 atframework

#include <atframework/testing/mock_connector.h>

#include <atframe/atapp.h>
#include <atframe/atapp_conf.h>

#include <log/log_wrapper.h>

#include <detail/libatbus_error.h>

#include <atframework/testing/raw_transport.h>

#include <string>

namespace atframework {
namespace testing {

mock_connector::mock_connector(atfw::atapp::app &owner, raw_transport &transport)
    : atfw::atapp::atapp_connector_impl(owner), transport_(&transport) {
  register_protocol("mock");
}

mock_connector::~mock_connector() { cleanup(); }

gsl::string_view mock_connector::name() const noexcept { return "atfw::testing::mock_connector"; }

uint32_t mock_connector::get_address_type(const atfw::atbus::channel::channel_address_t &) const noexcept {
  uint32_t ret = 0;
  ret |= static_cast<uint32_t>(address_type_t::kDuplex);
  ret |= static_cast<uint32_t>(address_type_t::kLocalHost);
  ret |= static_cast<uint32_t>(address_type_t::kLocalProcess);
  return ret;
}

int32_t mock_connector::on_start_listen(const atfw::atbus::channel::channel_address_t &) {
  return atfw::atapp::EN_ATAPP_ERR_SUCCESS;
}

int32_t mock_connector::on_start_connect(const atfw::atapp::etcd_discovery_node &discovery,
                                         atfw::atapp::atapp_endpoint &, const atfw::atbus::channel::channel_address_t &,
                                         const atfw::atapp::atapp_connection_handle::ptr_t &handle) {
  if (!handle) {
    return atfw::atapp::EN_ATAPP_ERR_NOT_INITED;
  }

  uintptr_t key = reinterpret_cast<uintptr_t>(handle.get());
  handles_[key] = handle;
  handle->set_ready();
  handle->set_private_data_u64(static_cast<uint64_t>(key));

  if (nullptr != transport_) {
    transport_->capture_connect(discovery.get_discovery_info().id());
  }
  return atfw::atapp::EN_ATAPP_ERR_SUCCESS;
}

int32_t mock_connector::on_close_connection(atfw::atapp::atapp_connection_handle &handle) {
  uintptr_t key = reinterpret_cast<uintptr_t>(&handle);
  auto iter = handles_.find(key);
  if (iter != handles_.end()) {
    if (nullptr != transport_ && nullptr != handle.get_endpoint()) {
      transport_->capture_disconnect(handle.get_endpoint()->get_id());
    }
    handles_.erase(iter);
  }
  return EN_ATBUS_ERR_SUCCESS;
}

int32_t mock_connector::on_send_forward_request(atfw::atapp::atapp_connection_handle *handle, int32_t type,
                                                uint64_t *msg_sequence, gsl::span<const unsigned char> data,
                                                const atfw::atapp::protocol::atapp_metadata *metadata) {
  if (nullptr == handle) {
    return atfw::atapp::EN_ATAPP_ERR_NOT_INITED;
  }

  uint64_t node_id = 0;
  std::string node_name;
  if (nullptr != handle->get_endpoint()) {
    node_id = handle->get_endpoint()->get_id();
    node_name = handle->get_endpoint()->get_name();
  }

  if (nullptr == transport_ || !transport_->is_active()) {
    // Engine already torn down: drop silently and report success so app shutdown is not disturbed.
    return EN_ATBUS_ERR_SUCCESS;
  }

  return transport_->capture_outbound(node_id, node_name, type, nullptr == msg_sequence ? 0 : *msg_sequence, data,
                                      metadata);
}

void mock_connector::on_discovery_event(atfw::atapp::etcd_discovery_action_t,
                                        const atfw::atapp::etcd_discovery_node::ptr_t &) {}

}  // namespace testing
}  // namespace atframework
