// Copyright 2026 atframework
//
// Regression case for the accept-time session destruction crash:
// sessions waiting for their first handshake packet all have id 0, so the
// first-idle timer table must not be keyed by session id. Two connections
// accepted while both are still pre-handshake must coexist; the earlier one
// must stay alive until it is closed or times out.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "frame/test_macros.h"

#include <uv.h>

#include "algorithm/crypto_cipher.h"
#include <time/time_utility.h>

#include <atframe/atapp.h>

#include "atgateway/protocol/v2/libatgw_protocol_sdk.h"
#include "session.h"
#include "session_manager.h"

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
  CASE_MSG_INFO() << "after client1 accepted: id=" << captured.ids[0]
                  << ", use_count=" << captured.weak[0].use_count() << std::endl;
  test_client_t *client2 = test_start_client(loop, port);
  bool client2_accepted = test_pump_until(loop, [&captured] { return captured.raw.size() >= 2; });
  CASE_EXPECT_TRUE(client2_accepted);
  CASE_EXPECT_EQ(0, client2->connect_status);

  // ============ assert: both pre-handshake sessions coexist ============
  CASE_MSG_INFO() << "after client2 accepted: ids: " << (captured.ids.size() > 0 ? captured.ids[0] : 0) << ", "
                  << (captured.ids.size() > 1 ? captured.ids[1] : 0)
                  << ", use_counts: " << captured.weak[0].use_count() << ", "
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
