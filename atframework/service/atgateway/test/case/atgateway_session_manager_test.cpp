// Copyright 2026 atframework
//
// Regression coverage for pre-handshake session lifetime, authenticated reconnects,
// and asynchronous cleanup of replaced connections.

#include <atframe/atapp.h>
#include <time/time_utility.h>
#include <uv.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "algorithm/crypto_cipher.h"
#include "atgateway/protocol/v2/libatgw_protocol_sdk.h"
#include "frame/test_macros.h"
#include "session.h"          // NOLINT(build/include_subdir)
#include "session_manager.h"  // NOLINT(build/include_subdir)

namespace {

struct openssl_test_init_wrapper {
  openssl_test_init_wrapper() { atfw::util::crypto::cipher::init_global_algorithm(); }
  ~openssl_test_init_wrapper() { atfw::util::crypto::cipher::cleanup_global_algorithm(); }
};

static std::shared_ptr<openssl_test_init_wrapper> openssl_test_inited;

static void ensure_openssl_init() {
  if (!openssl_test_inited) {
    openssl_test_inited = std::make_shared<openssl_test_init_wrapper>();
  }
}

using libatgw_protocol_sdk = ::atframework::gateway::libatgw_protocol_sdk;
using libatgw_protocol_api = ::atframework::gateway::libatgw_protocol_api;

// The protocol callbacks are referenced by protocol objects owned by sessions, so the storage must
// outlive the session manager. No handshake is driven in this case; these stubs only keep the
// protocol layer contract-valid if anything fires unexpectedly.
libatgw_protocol_api::proto_callbacks_t test_proto_callbacks;

struct test_client_t {
  uv_tcp_t handle;
  uv_connect_t connect_req;
  int connect_status;
};

void test_on_client_connect(uv_connect_t *req, int status) {
  auto *client = reinterpret_cast<test_client_t *>(req->data);
  if (nullptr != client) {
    client->connect_status = status;
  }
}

void test_on_client_closed(uv_handle_t *handle) { delete reinterpret_cast<test_client_t *>(handle); }

test_client_t *test_start_client(uv_loop_t *loop, uint16_t port) {
  auto *client = new test_client_t();
  client->connect_status = -1;
  uv_tcp_init(loop, &client->handle);

  sockaddr_in addr;
  uv_ip4_addr("127.0.0.1", static_cast<int>(port), &addr);
  client->connect_req.data = client;
  uv_tcp_connect(&client->connect_req, &client->handle, reinterpret_cast<const sockaddr *>(&addr),
                 test_on_client_connect);
  return client;
}

struct test_probe_handle_t {
  uv_tcp_t handle;
};

void test_on_probe_closed(uv_handle_t *handle) { delete reinterpret_cast<test_probe_handle_t *>(handle); }

uint16_t test_probe_free_port(uv_loop_t *loop) {
  auto *probe = new test_probe_handle_t();
  uv_tcp_init(loop, &probe->handle);

  sockaddr_in addr;
  uv_ip4_addr("127.0.0.1", 0, &addr);
  if (0 != uv_tcp_bind(&probe->handle, reinterpret_cast<const sockaddr *>(&addr), 0)) {
    uv_close(reinterpret_cast<uv_handle_t *>(&probe->handle), test_on_probe_closed);
    return 0;
  }

  int name_len = sizeof(addr);
  if (0 != uv_tcp_getsockname(&probe->handle, reinterpret_cast<sockaddr *>(&addr), &name_len)) {
    uv_close(reinterpret_cast<uv_handle_t *>(&probe->handle), test_on_probe_closed);
    return 0;
  }

  uint16_t port = ntohs(addr.sin_port);
  uv_close(reinterpret_cast<uv_handle_t *>(&probe->handle), test_on_probe_closed);
  return port;
}

struct captured_sessions_t {
  std::vector<::atframework::gateway::session *> raw;
  std::vector<std::weak_ptr<::atframework::gateway::session>> weak;
  std::vector<std::string> hosts;
  std::vector<int32_t> ports;
  std::vector<uint64_t> ids;
};

captured_sessions_t *g_test_captured_sessions = nullptr;

int test_on_create_session(::atframework::gateway::session *sess, uv_stream_t *) {
  if (nullptr == g_test_captured_sessions || nullptr == sess) {
    return 0;
  }

  g_test_captured_sessions->raw.push_back(sess);
  g_test_captured_sessions->weak.push_back(sess->shared_from_this());
  g_test_captured_sessions->hosts.push_back(sess->get_peer_host());
  g_test_captured_sessions->ports.push_back(sess->get_peer_port());
  g_test_captured_sessions->ids.push_back(sess->get_id());
  return 0;
}

// Pumps the event loop until pred holds. The iteration cap is only a hang guard: the predicate
// itself, asserted by the caller, is the pass/fail evidence.
bool test_pump_until(uv_loop_t *loop, const std::function<bool()> &pred, int max_iterations = 5000) {
  for (int i = 0; i < max_iterations; ++i) {
    if (pred()) {
      return true;
    }
    atfw::util::time::time_utility::update();
    uv_run(loop, UV_RUN_NOWAIT);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

bool test_pump_app_until(::atframework::atapp::app &app, const std::function<bool()> &pred,
                         int max_iterations = 5000) {
  for (int i = 0; i < max_iterations; ++i) {
    if (pred()) {
      return true;
    }
    atfw::util::time::time_utility::update();
    app.run_noblock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

bool test_all_sessions_released(const captured_sessions_t &captured) {
  for (const auto &weak : captured.weak) {
    if (!weak.expired()) {
      return false;
    }
  }
  return true;
}

void test_write_config_file(const char *path) {
  // Offline atapp configure, mirroring the minimal headless layout used by the repo's test
  // fixtures: no bus endpoints, etcd disabled, stderr-only log.
  std::ofstream config_file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  config_file << "atapp:\n"
              << "  id: 0x00000051\n"
              << "  id_mask: 8.8.8.8\n"
              << "  name: \"atgateway-session-mgr-test\"\n"
              << "  type_id: 1\n"
              << "  type_name: \"unit-test\"\n"
              << "  bus:\n"
              << "    listen: []\n"
              << "    proxy: \"\"\n"
              << "    backlog: 256\n"
              << "    first_idle_timeout: 10s\n"
              << "    ping_interval: 60s\n"
              << "    retry_interval: 3s\n"
              << "    fault_tolerant: 3\n"
              << "    message_size: 64KB\n"
              << "    receive_buffer_size: 1MB\n"
              << "    send_buffer_size: 1MB\n"
              << "    send_buffer_number: 0\n"
              << "  timer:\n"
              << "    tick_interval: 8ms\n"
              << "    stop_timeout: 2s\n"
              << "    stop_interval: 128ms\n"
              << "  etcd:\n"
              << "    enable: false\n"
              << "  log:\n"
              << "    level: warning\n"
              << "    category:\n"
              << "      - name: default\n"
              << "        prefix: \"[Log %L][%F %T.%f][%s:%n(%C)]: \"\n"
              << "        sink:\n"
              << "          - type: stderr\n"
              << "            level:\n"
              << "              min: fatal\n"
              << "              max: warning\n";
}

}  // namespace

namespace {

using gateway_session = ::atframework::gateway::session;
using gateway_error = ::atframework::gateway::error_code_t;
using gateway_close_reason = ::atframework::gateway::close_reason_t;

// Real protocol authentication and TCP handles, with queued protocol writes so tests can
// hold the old connection's kickoff in flight while the replacement completes its handshake.
struct reconnect_connection {
  libatgw_protocol_api::proto_callbacks_t server_callbacks;
  libatgw_protocol_api::proto_callbacks_t client_callbacks;
  gateway_session::ptr_t server;
  std::unique_ptr<libatgw_protocol_sdk> client;
  test_client_t *socket = nullptr;
  std::deque<std::vector<unsigned char>> to_server;
  std::deque<std::vector<unsigned char>> to_client;
  std::vector<std::string> received_messages;
  bool hold_server_write = false;
  int accept_status = -1;
  int reconnect_status = -1;
  int server_handshake_status = -1;
  int client_handshake_status = -1;
  int server_close_count = 0;
  int client_close_reason = 0;
};

static void deliver_protocol_frames(std::deque<std::vector<unsigned char>> &frames, libatgw_protocol_api &receiver) {
  while (!frames.empty()) {
    auto frame = std::move(frames.front());
    frames.pop_front();
    size_t offset = 0;
    while (offset < frame.size()) {
      char *buffer = nullptr;
      size_t length = 0;
      receiver.alloc_receive_buffer(frame.size() - offset, buffer, length);
      CASE_EXPECT_TRUE(buffer != nullptr && length > 0);
      if (buffer == nullptr || length == 0) {
        return;
      }
      length = (std::min)(length, frame.size() - offset);
      std::memcpy(buffer, frame.data() + offset, length);
      int error = 0;
      receiver.read(static_cast<int>(length), {reinterpret_cast<const unsigned char *>(buffer), length}, error);
      offset += length;
    }
  }
}

struct reconnect_fixture {
  uv_loop_t loop{};
  uv_tcp_t listener{};
  std::vector<std::unique_ptr<reconnect_connection>> connections;
  ::atframework::gateway::session_manager manager;
  std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> server_context;
  std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> client_context;
  uint16_t port = 0;

  reconnect_fixture() {
    ensure_openssl_init();
    atfw::util::time::time_utility::update();
    libatgw_protocol_sdk::crypto_conf_t config;
    config.set_default();
    config.supported_algorithms = {
        ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
    };
    config.client_mode = false;
    auto client_config = config;
    client_config.client_mode = true;
    server_context = libatgw_protocol_sdk::create_shared_context(config);
    client_context = libatgw_protocol_sdk::create_shared_context(client_config);
    CASE_EXPECT_TRUE(server_context != nullptr && client_context != nullptr);
    manager.get_conf().origin_conf.set_echo_server(true);
    manager.get_conf().origin_conf.mutable_client()->mutable_reconnect_timeout()->set_seconds(180);
    manager.get_conf().origin_conf.mutable_client()->mutable_shutdown_timeout()->set_seconds(5);
    CASE_EXPECT_EQ(0, uv_loop_init(&loop));
    CASE_EXPECT_EQ(0, uv_tcp_init(&loop, &listener));
    listener.data = this;
    sockaddr_in address{};
    CASE_EXPECT_EQ(0, uv_ip4_addr("127.0.0.1", 0, &address));
    CASE_EXPECT_EQ(0, uv_tcp_bind(&listener, reinterpret_cast<const sockaddr *>(&address), 0));
    CASE_EXPECT_EQ(0, uv_listen(reinterpret_cast<uv_stream_t *>(&listener), 16, [](uv_stream_t *stream, int status) {
                     auto *self = static_cast<reconnect_fixture *>(stream->data);
                     CASE_EXPECT_EQ(0, status);
                     auto &connection = *self->connections.back();
                     connection.accept_status = connection.server->accept_tcp(stream);
                   }));
    int address_length = sizeof(address);
    CASE_EXPECT_EQ(0, uv_tcp_getsockname(&listener, reinterpret_cast<sockaddr *>(&address), &address_length));
    port = ntohs(address.sin_port);
  }

  ~reconnect_fixture() {
    for (auto &connection : connections) {
      connection->hold_server_write = false;
      if (connection->server->get_protocol_handle()->check_flag(libatgw_protocol_api::flag_t::kWriting)) {
        connection->server->on_write_done(UV_ECANCELED);
      }
      connection->server->close(static_cast<int>(gateway_close_reason::kServerClosed), 0, "test cleanup");
      uv_close(reinterpret_cast<uv_handle_t *>(&connection->socket->handle), test_on_client_closed);
    }
    manager.reset();
    uv_close(reinterpret_cast<uv_handle_t *>(&listener), nullptr);
    uv_run(&loop, UV_RUN_DEFAULT);
    CASE_EXPECT_EQ(0, uv_loop_close(&loop));
    atfw::util::time::time_utility::update();
  }

  reconnect_connection &connect() {
    connections.emplace_back(std::make_unique<reconnect_connection>());
    auto &connection = *connections.back();
    connection.server_callbacks.write_fn = [&connection](libatgw_protocol_api *protocol,
                                                         gsl::span<unsigned char> buffer, bool *done) {
      connection.to_client.emplace_back(buffer.data() + protocol->get_write_header_offset(),
                                        buffer.data() + buffer.size());
      *done = !connection.hold_server_write;
      return 0;
    };
    connection.client_callbacks.write_fn = [&connection](libatgw_protocol_api *protocol,
                                                         gsl::span<unsigned char> buffer, bool *done) {
      connection.to_server.emplace_back(buffer.data() + protocol->get_write_header_offset(),
                                        buffer.data() + buffer.size());
      *done = true;
      return 0;
    };
    connection.server_callbacks.new_session_fn = [&connection](libatgw_protocol_api *, uint64_t &id) {
      int result = connection.server->init_new_session();
      id = connection.server->get_id();
      return result;
    };
    connection.server_callbacks.reconnect_fn = [this, &connection](libatgw_protocol_api *, uint64_t id) {
      connection.reconnect_status = manager.reconnect(*connection.server, id);
      return connection.reconnect_status;
    };
    connection.server_callbacks.on_handshake_done_fn = [this, &connection](libatgw_protocol_api *, int status) {
      connection.server_handshake_status = status;
      return status == 0 ? manager.active_session(connection.server) : 0;
    };
    connection.client_callbacks.on_handshake_done_fn = [&connection](libatgw_protocol_api *, int status) {
      connection.client_handshake_status = status;
      return 0;
    };
    connection.server_callbacks.close_fn = [&connection](libatgw_protocol_api *, int32_t reason, int32_t sub_reason,
                                                         atfw::util::nostd::string_view message) {
      ++connection.server_close_count;
      auto &server = *connection.server;
      if (!server.check_flag(gateway_session::flag_t::kClosing)) {
        auto *owner = server.get_manager();
        bool allow_reconnect = reason <= static_cast<int>(gateway_close_reason::kReconnectBound);
        if (owner == nullptr || owner->close(server.get_id(), reason, sub_reason, message, allow_reconnect) < 0) {
          server.close(reason, sub_reason, message);
        }
      }
      return 0;
    };
    connection.client_callbacks.close_fn = [&connection](libatgw_protocol_api *, int32_t reason, int32_t,
                                                         atfw::util::nostd::string_view) {
      connection.client_close_reason = reason;
      return 0;
    };
    connection.server_callbacks.message_fn = [&connection](libatgw_protocol_api *,
                                                           gsl::span<const unsigned char> data) {
      connection.received_messages.emplace_back(reinterpret_cast<const char *>(data.data()), data.size());
      return 0;
    };
    std::unique_ptr<libatgw_protocol_api> protocol = std::make_unique<libatgw_protocol_sdk>(server_context);
    protocol->set_callbacks(&connection.server_callbacks);
    connection.server = gateway_session::create(&manager, protocol);
    connection.client = std::make_unique<libatgw_protocol_sdk>(client_context);
    connection.client->set_callbacks(&connection.client_callbacks);
    connection.socket = test_start_client(&loop, port);
    CASE_EXPECT_TRUE(test_pump_until(&loop, [&connection] { return connection.accept_status == 0; }));
    return connection;
  }

  static void exchange(reconnect_connection &connection) {
    for (int iteration = 0; iteration < 16; ++iteration) {
      deliver_protocol_frames(connection.to_server, *connection.server->get_protocol_handle());
      if (!connection.hold_server_write) {
        deliver_protocol_frames(connection.to_client, *connection.client);
      }
      if (connection.to_server.empty() && (connection.to_client.empty() || connection.hold_server_write)) {
        return;
      }
    }
    CASE_EXPECT_TRUE(connection.to_server.empty());
    CASE_EXPECT_TRUE(connection.to_client.empty());
  }

  static bool start(reconnect_connection &connection) {
    CASE_EXPECT_EQ(0, connection.client->start_session());
    exchange(connection);
    CASE_EXPECT_EQ(0, connection.server_handshake_status);
    CASE_EXPECT_EQ(0, connection.client_handshake_status);
    return connection.server_handshake_status == 0 && connection.client_handshake_status == 0;
  }

  static void reconnect(reconnect_connection &connection, const reconnect_connection &previous,
                        bool corrupt_token = false) {
    auto token = previous.client->get_session_token();
    std::vector<unsigned char> token_copy(token.begin(), token.end());
    CASE_EXPECT_FALSE(token_copy.empty());
    if (token_copy.empty()) {
      return;
    }
    if (corrupt_token) {
      token_copy.front() = static_cast<unsigned char>(token_copy.front() ^ 0x80U);
    }
    CASE_EXPECT_EQ(
        0, connection.client->reconnect_session(previous.server->get_id(), {token_copy.data(), token_copy.size()}));
    exchange(connection);
  }
};

static void check_replace_connection(bool cached, bool cache_enabled) {
  reconnect_fixture fixture;
  auto &previous = fixture.connect();
  if (!reconnect_fixture::start(previous)) {
    return;
  }
  const auto id = previous.server->get_id();
  int private_value = 42;
  previous.server->set_private_data(&private_value);
  previous.server->set_router(0x1234, "reconnect-router");
  previous.hold_server_write = true;
  if (!cache_enabled) {
    fixture.manager.get_conf().origin_conf.mutable_client()->mutable_reconnect_timeout()->set_seconds(0);
  }
  if (cached) {
    CASE_EXPECT_EQ(0, fixture.manager.close(id, static_cast<int>(gateway_close_reason::kReset), 0, "lost", true));
  }
  auto &replacement = fixture.connect();
  reconnect_fixture::reconnect(replacement, previous);
  CASE_EXPECT_EQ(0, replacement.reconnect_status);
  CASE_EXPECT_EQ(0, replacement.client_handshake_status);
  CASE_EXPECT_EQ(replacement.server.get(), fixture.manager.find_session(id).get());
  CASE_EXPECT_EQ(id, replacement.server->get_id());
  CASE_EXPECT_EQ(&private_value, replacement.server->get_private_data());
  CASE_EXPECT_EQ(static_cast<uint64_t>(0x1234), replacement.server->get_router_id());
  CASE_EXPECT_EQ(std::string("reconnect-router"), replacement.server->get_router_name());
  CASE_EXPECT_TRUE(replacement.server->check_flag(gateway_session::flag_t::kRegistered));
  CASE_EXPECT_TRUE(previous.server->check_flag(gateway_session::flag_t::kReconnected));
  CASE_EXPECT_FALSE(previous.server->check_flag(gateway_session::flag_t::kRegistered));
  CASE_EXPECT_TRUE(previous.server->check_flag(gateway_session::flag_t::kHasFd));
  if (replacement.reconnect_status != 0) {
    return;
  }

  previous.hold_server_write = false;
  reconnect_fixture::exchange(previous);
  CASE_EXPECT_EQ(0, previous.server->on_write_done(0));
  CASE_EXPECT_EQ(1, previous.server_close_count);
  CASE_EXPECT_EQ(static_cast<int>(cached ? gateway_close_reason::kReset : gateway_close_reason::kKickoff),
                 previous.client_close_reason);
  CASE_EXPECT_TRUE(test_pump_until(
      &fixture.loop, [&previous] { return !previous.server->check_flag(gateway_session::flag_t::kHasFd); }));
  // Old completion and repeated cleanup must leave the replacement registered and usable.
  CASE_EXPECT_EQ(0, previous.server->force_close_fd());
  CASE_EXPECT_EQ(0, previous.server->close(static_cast<int>(gateway_close_reason::kLogout), 0, "late close"));
  CASE_EXPECT_EQ(replacement.server.get(), fixture.manager.find_session(id).get());
  const unsigned char payload[] = "message after replacement";
  CASE_EXPECT_EQ(0, replacement.client->send_post({payload, sizeof(payload) - 1}));
  reconnect_fixture::exchange(replacement);
  CASE_EXPECT_EQ(static_cast<size_t>(1), replacement.received_messages.size());
  if (!replacement.received_messages.empty()) {
    CASE_EXPECT_EQ(std::string("message after replacement"), replacement.received_messages.front());
  }
}

}  // namespace

CASE_TEST(atgateway_session_manager, reconnect_replaces_live_connection) { check_replace_connection(false, true); }

CASE_TEST(atgateway_session_manager, reconnect_replaces_connection_with_cache_disabled) {
  check_replace_connection(false, false);
}

CASE_TEST(atgateway_session_manager, reconnect_replaces_connection_during_shutdown) {
  check_replace_connection(true, true);
}

CASE_TEST(atgateway_session_manager, reconnect_rejects_invalid_token_without_closing_live_connection) {
  reconnect_fixture fixture;
  auto &previous = fixture.connect();
  if (!reconnect_fixture::start(previous)) {
    return;
  }
  auto &replacement = fixture.connect();
  reconnect_fixture::reconnect(replacement, previous, true);
  CASE_EXPECT_EQ(static_cast<int>(gateway_error::kRefuseReconnect), replacement.reconnect_status);
  CASE_EXPECT_EQ(previous.server.get(), fixture.manager.find_session(previous.server->get_id()).get());
  CASE_EXPECT_FALSE(previous.server->check_flag(gateway_session::flag_t::kReconnected));
  CASE_EXPECT_FALSE(previous.server->check_flag(gateway_session::flag_t::kClosing));
  CASE_EXPECT_EQ(0, previous.server_close_count);
  const unsigned char payload[] = "original connection remains authenticated";
  CASE_EXPECT_EQ(0, previous.client->send_post({payload, sizeof(payload) - 1}));
  reconnect_fixture::exchange(previous);
  CASE_EXPECT_EQ(static_cast<size_t>(1), previous.received_messages.size());
}

CASE_TEST(atgateway_session_manager, reconnect_force_closes_stalled_old_connection) {
  reconnect_fixture fixture;
  auto &previous = fixture.connect();
  if (!reconnect_fixture::start(previous)) {
    return;
  }
  previous.hold_server_write = true;
  auto &replacement = fixture.connect();
  reconnect_fixture::reconnect(replacement, previous);
  CASE_EXPECT_EQ(0, replacement.reconnect_status);
  if (replacement.reconnect_status != 0) {
    return;
  }
  auto deadline = atfw::util::time::time_utility::now() + std::chrono::seconds(6);
  atfw::util::time::time_utility::update(&deadline);
  CASE_EXPECT_EQ(0, fixture.manager.tick());
  CASE_EXPECT_TRUE(uv_is_closing(reinterpret_cast<uv_handle_t *>(previous.server->get_uv_stream())) != 0);
  CASE_EXPECT_TRUE(test_pump_until(
      &fixture.loop, [&previous] { return !previous.server->check_flag(gateway_session::flag_t::kHasFd); }));
  CASE_EXPECT_EQ(UV_ECANCELED, previous.server->on_write_done(UV_ECANCELED));
  CASE_EXPECT_EQ(replacement.server.get(), fixture.manager.find_session(previous.server->get_id()).get());
}

CASE_TEST(atgateway_session_manager, reconnect_preserves_cached_session_after_invalid_token) {
  reconnect_fixture fixture;
  auto &previous = fixture.connect();
  if (!reconnect_fixture::start(previous)) {
    return;
  }
  const auto id = previous.server->get_id();
  CASE_EXPECT_EQ(0, fixture.manager.close(id, static_cast<int>(gateway_close_reason::kReset), 0, "lost", true));
  CASE_EXPECT_TRUE(test_pump_until(
      &fixture.loop, [&previous] { return !previous.server->check_flag(gateway_session::flag_t::kHasFd); }));
  auto &unauthorized = fixture.connect();
  reconnect_fixture::reconnect(unauthorized, previous, true);
  CASE_EXPECT_EQ(static_cast<int>(gateway_error::kRefuseReconnect), unauthorized.reconnect_status);
  CASE_EXPECT_TRUE(previous.server->check_flag(gateway_session::flag_t::kWaitReconnect));
  CASE_EXPECT_FALSE(previous.server->check_flag(gateway_session::flag_t::kReconnected));
  auto &replacement = fixture.connect();
  reconnect_fixture::reconnect(replacement, previous);
  CASE_EXPECT_EQ(0, replacement.reconnect_status);
  CASE_EXPECT_EQ(0, replacement.client_handshake_status);
  CASE_EXPECT_EQ(replacement.server.get(), fixture.manager.find_session(id).get());
}

CASE_TEST(atgateway_session_manager, reconnect_twice_ignores_old_connection_completions) {
  reconnect_fixture fixture;
  auto &original = fixture.connect();
  if (!reconnect_fixture::start(original)) {
    return;
  }
  const auto id = original.server->get_id();
  original.hold_server_write = true;
  CASE_EXPECT_EQ(0, fixture.manager.close(id, static_cast<int>(gateway_close_reason::kReset), 0, "lost", true));
  auto &first = fixture.connect();
  reconnect_fixture::reconnect(first, original);
  CASE_EXPECT_EQ(0, first.reconnect_status);
  if (first.reconnect_status != 0) {
    return;
  }
  first.hold_server_write = true;
  auto &latest = fixture.connect();
  reconnect_fixture::reconnect(latest, first);
  CASE_EXPECT_EQ(0, latest.reconnect_status);
  CASE_EXPECT_EQ(0, latest.client_handshake_status);
  if (latest.reconnect_status != 0) {
    return;
  }
  for (auto *previous : {&first, &original}) {
    previous->hold_server_write = false;
    reconnect_fixture::exchange(*previous);
    CASE_EXPECT_EQ(0, previous->server->on_write_done(0));
    CASE_EXPECT_TRUE(test_pump_until(
        &fixture.loop, [previous] { return !previous->server->check_flag(gateway_session::flag_t::kHasFd); }));
    CASE_EXPECT_EQ(latest.server.get(), fixture.manager.find_session(id).get());
  }
  // The original cached connection's expiry must not disturb the current owner.
  auto deadline = atfw::util::time::time_utility::now() + std::chrono::seconds(181);
  atfw::util::time::time_utility::update(&deadline);
  CASE_EXPECT_EQ(0, fixture.manager.tick());
  CASE_EXPECT_EQ(latest.server.get(), fixture.manager.find_session(id).get());
  CASE_EXPECT_TRUE(latest.server->check_flag(gateway_session::flag_t::kRegistered));
  CASE_EXPECT_FALSE(latest.server->check_flag(gateway_session::flag_t::kClosing));
  const unsigned char payload[] = "latest connection after repeated reconnect";
  CASE_EXPECT_EQ(0, latest.client->send_post({payload, sizeof(payload) - 1}));
  reconnect_fixture::exchange(latest);
  CASE_EXPECT_EQ(static_cast<size_t>(1), latest.received_messages.size());
}

CASE_TEST(atgateway_session_manager, first_idle_accept_overlap) {
  ensure_openssl_init();

  // ============ arrange: headless atapp + session manager listening on a free loopback port ============
  const char *config_path = "atgateway_session_manager_test.yaml";
  test_write_config_file(config_path);

  ::atframework::atapp::app app;
  std::vector<std::string> args_storage = {"atgateway-session-mgr-test", "-c", config_path, "start"};
  std::vector<const char *> args;
  args.reserve(args_storage.size());
  for (const auto &arg : args_storage) {
    args.push_back(arg.c_str());
  }
  int app_init_result = app.init(nullptr, static_cast<int>(args.size()), args.data(), nullptr);
  CASE_EXPECT_EQ(0, app_init_result);
  if (0 != app_init_result) {
    CASE_MSG_INFO() << "app.init failed: " << app_init_result << std::endl;
    return;
  }

  uv_loop_t *loop = app.get_evloop();
  CASE_EXPECT_TRUE(nullptr != loop);
  if (nullptr == loop) {
    return;
  }

  libatgw_protocol_sdk::crypto_conf_t proto_conf;
  proto_conf.set_default();
  auto shared_conf = libatgw_protocol_sdk::create_shared_context(proto_conf);
  CASE_EXPECT_TRUE(!!shared_conf);
  if (!shared_conf) {
    return;
  }

  test_proto_callbacks = libatgw_protocol_api::proto_callbacks_t();

  ::atframework::gateway::session_manager mgr;
  mgr.set_on_create_session(test_on_create_session);
  int mgr_init_result = mgr.init(&app, [&shared_conf]() -> std::unique_ptr<libatgw_protocol_api> {
    auto *ret = new (std::nothrow)libatgw_protocol_sdk(shared_conf);
    if (nullptr != ret) {
      ret->set_callbacks(&test_proto_callbacks);
      ret->set_write_header_offset(sizeof(uv_write_t));
    }
    return std::unique_ptr<libatgw_protocol_api>(ret);
  });
  CASE_EXPECT_EQ(0, mgr_init_result);
  if (0 != mgr_init_result) {
    return;
  }

  uint16_t port = test_probe_free_port(loop);
  CASE_EXPECT_TRUE(0 != port);
  if (0 == port) {
    return;
  }

  // Production fills the listen options from the atgateway configure (backlog defaults to 1024
  // there); set the same value explicitly because this case does not run the configure loader.
  mgr.get_conf().origin_conf.mutable_listen()->set_backlog(1024);

  char listen_address[64] = {0};
  std::snprintf(listen_address, sizeof(listen_address) - 1, "ipv4://127.0.0.1:%u",
                static_cast<unsigned int>(port));
  int listen_result = mgr.listen(listen_address);
  CASE_EXPECT_EQ(0, listen_result);
  if (0 != listen_result) {
    return;
  }

  // ============ act: two connections, neither sends a handshake packet ============
  captured_sessions_t captured;
  g_test_captured_sessions = &captured;

  test_client_t *client1 = test_start_client(loop, port);
  bool client1_accepted = test_pump_until(loop, [&captured] { return captured.raw.size() >= 1; });
  CASE_EXPECT_TRUE(client1_accepted);
  CASE_EXPECT_EQ(0, client1->connect_status);

  // client1 is accepted and still waiting for its first handshake packet when client2 arrives.
  CASE_MSG_INFO() << "after client1 accepted: id=" << (captured.ids.size() > 0 ? captured.ids[0] : 0)
                  << ", use_count=" << (captured.weak.size() > 0 ? captured.weak[0].use_count() : 0) << std::endl;
  test_client_t *client2 = test_start_client(loop, port);
  bool client2_accepted = test_pump_until(loop, [&captured] { return captured.raw.size() >= 2; });
  CASE_EXPECT_TRUE(client2_accepted);
  CASE_EXPECT_EQ(0, client2->connect_status);

  // ============ assert: both pre-handshake sessions coexist ============
  CASE_MSG_INFO() << "after client2 accepted: ids: " << (captured.ids.size() > 0 ? captured.ids[0] : 0) << ", "
                  << (captured.ids.size() > 1 ? captured.ids[1] : 0)
                  << ", use_counts: " << (captured.weak.size() > 0 ? captured.weak[0].use_count() : 0) << ", "
                  << (captured.weak.size() > 1 ? captured.weak[1].use_count() : 0) << std::endl;
  CASE_EXPECT_EQ(static_cast<size_t>(2), captured.raw.size());
  if (captured.raw.size() >= 2) {
    CASE_EXPECT_TRUE(captured.raw[0] != captured.raw[1]);
    CASE_EXPECT_FALSE(captured.weak[0].expired());
    CASE_EXPECT_FALSE(captured.weak[1].expired());
    CASE_EXPECT_FALSE(captured.raw[0]->check_flag(::atframework::gateway::session::flag_t::kClosing));
    CASE_EXPECT_FALSE(captured.raw[1]->check_flag(::atframework::gateway::session::flag_t::kClosing));
    CASE_EXPECT_EQ(std::string("127.0.0.1"), captured.hosts[0]);
    CASE_EXPECT_EQ(std::string("127.0.0.1"), captured.hosts[1]);
    CASE_EXPECT_TRUE(0 != captured.ports[0]);
    CASE_EXPECT_TRUE(0 != captured.ports[1]);
    CASE_EXPECT_TRUE(captured.ports[0] != captured.ports[1]);
  }

  // ============ cleanup: everything must be released ============
  uv_close(reinterpret_cast<uv_handle_t *>(&client1->handle), test_on_client_closed);
  uv_close(reinterpret_cast<uv_handle_t *>(&client2->handle), test_on_client_closed);

  mgr.reset();
  bool sessions_released = test_pump_until(loop, [&captured] { return test_all_sessions_released(captured); });
  CASE_EXPECT_TRUE(sessions_released);
  if (sessions_released) {
    for (const auto &weak : captured.weak) {
      CASE_EXPECT_TRUE(weak.expired());
    }
  }
  g_test_captured_sessions = nullptr;

  app.stop();
  CASE_EXPECT_TRUE(test_pump_app_until(app, [&app] { return app.is_closed(); }));
}
