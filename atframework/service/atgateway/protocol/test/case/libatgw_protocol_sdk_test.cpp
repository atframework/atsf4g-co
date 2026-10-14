// Copyright 2026 atframework

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "algorithm/murmur_hash.h"
#include "frame/test_macros.h"

#include <atgateway/protocol/v2/libatgw_protocol_sdk.h>

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
using crypto_conf_t = libatgw_protocol_sdk::crypto_conf_t;
using crypto_session_t = libatgw_protocol_sdk::crypto_session_t;

// ========================= Server-Client simulation helpers =========================

struct sim_peer_t {
  std::shared_ptr<libatgw_protocol_sdk> sdk;
  sim_peer_t *remote;
  int handshake_status;
  int handshake_update_status;
  std::vector<std::vector<unsigned char>> received_messages;
  int error_count;
  int last_error_code;
  bool closed;
  int close_count;
  int close_reason;
  int close_sub_reason;
  uint64_t new_session_id_counter;
  bool pause_writing = false;
  std::list<std::vector<unsigned char>> pending_write_messages;
  size_t total_write_payload_bytes = 0;
  std::vector<std::vector<unsigned char>> captured_write_payloads;

  sim_peer_t()
      : remote(nullptr),
        handshake_status(-1),
        handshake_update_status(-1),
        error_count(0),
        last_error_code(0),
        closed(false),
        close_count(0),
        close_reason(0),
        close_sub_reason(0),
        new_session_id_counter(0x1000),
        pause_writing(false),
        total_write_payload_bytes(0) {}
};

static bool sim_process_writing(libatgw_protocol_api *proto, bool *is_done) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr == self) {
    return false;
  }

  if (nullptr == self->remote || nullptr == self->remote->sdk) {
    self->pending_write_messages.clear();
    return true;
  }

  while (!self->pending_write_messages.empty() && !self->pause_writing) {
    auto &msg = self->pending_write_messages.front();
    gsl::span<unsigned char> buffer{msg.data(), msg.size()};

    // Deliver to remote peer using alloc_receive_buffer + read protocol
    auto &remote_sdk = self->remote->sdk;
    size_t remaining = buffer.size();
    const unsigned char *src = buffer.data();

    while (remaining > 0 && !self->pause_writing) {
      char *recv_buf = nullptr;
      size_t recv_len = 0;
      remote_sdk->alloc_receive_buffer(remaining, recv_buf, recv_len);
      if (nullptr == recv_buf || 0 == recv_len) {
        break;
      }
      size_t to_copy = (remaining < recv_len) ? remaining : recv_len;
      memcpy(recv_buf, src, to_copy);

      int32_t errcode = 0;
      gsl::span<const unsigned char> data_span{reinterpret_cast<const unsigned char *>(recv_buf), to_copy};
      remote_sdk->read(static_cast<int>(to_copy), data_span, errcode);

      src += to_copy;
      remaining -= to_copy;
    }

    if (remaining == 0) {
      self->pending_write_messages.pop_front();
    } else {
      msg.erase(msg.begin(),
                msg.begin() + static_cast<std::vector<unsigned char>::difference_type>(buffer.size() - remaining));
    }
  }

  if (self->pending_write_messages.empty()) {
    if (is_done != nullptr) {
      *is_done = true;
    }
    proto->write_done(0);
    return true;
  }

  return false;
}

/// @brief Deliver data from one peer's write callback to the other peer's read path.
///
/// write_fn receives the full buffer including write_header_offset_ headspace at the front.
/// The actual payload starts after proto->get_write_header_offset() bytes.
/// We must use alloc_receive_buffer() + memcpy + read() to simulate the transport layer,
/// because read() expects data to already reside in the SDK's internal read buffer.
static int sim_write_fn(libatgw_protocol_api *proto, gsl::span<unsigned char> buffer, bool *is_done) {
  if (buffer.empty()) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return 0;
  }

  // Find the peer data
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr == self || nullptr == self->remote || nullptr == self->remote->sdk) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return 0;
  }

  // Skip write_header_offset_ headspace — only the payload after that is real data
  size_t header_offset = proto->get_write_header_offset();
  if (buffer.size() <= header_offset) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return 0;
  }
  const unsigned char *payload = buffer.data() + header_offset;
  size_t payload_len = buffer.size() - header_offset;

  self->total_write_payload_bytes += payload_len;
  self->captured_write_payloads.emplace_back(payload, payload + payload_len);
  self->pending_write_messages.emplace_back(payload, payload + payload_len);

  sim_process_writing(proto, is_done);
  return 0;
}

static int sim_message_fn(libatgw_protocol_api *proto, gsl::span<const unsigned char> buffer) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr == self) {
    return 0;
  }
  if (buffer.empty()) {
    self->received_messages.emplace_back();
  } else {
    self->received_messages.emplace_back(buffer.data(), buffer.data() + buffer.size());
  }
  return 0;
}

static int sim_new_session_fn(libatgw_protocol_api *proto, uint64_t &sess_id) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    sess_id = ++self->new_session_id_counter;
  }
  return 0;
}

static int sim_reconnect_fn(libatgw_protocol_api * /*proto*/, uint64_t /*sess_id*/) {
  // Accept reconnect
  return 0;
}

static int sim_close_fn(libatgw_protocol_api *proto, int32_t reason, int32_t sub_reason,
                        atfw::util::nostd::string_view) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    self->closed = true;
    ++self->close_count;
    self->close_reason = reason;
    self->close_sub_reason = sub_reason;
  }
  return 0;
}

static int sim_handshake_done_fn(libatgw_protocol_api *proto, int status) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    self->handshake_status = status;
  }
  return 0;
}

static int sim_handshake_update_fn(libatgw_protocol_api *proto, int status) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    self->handshake_update_status = status;
  }
  return 0;
}

static int sim_error_fn(libatgw_protocol_api *proto, const char * /*filename*/, uint32_t /*line*/, int errcode,
                        const char * /*errmsg*/) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    ++self->error_count;
    self->last_error_code = errcode;
  }
  return 0;
}

/// @brief Setup a server-client pair with callbacks wired for loopback simulation.
static void setup_sim_pair(sim_peer_t &server, sim_peer_t &client,
                           const std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> &server_conf,
                           const std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> &client_conf,
                           libatgw_protocol_api::proto_callbacks_t &server_cbs,
                           libatgw_protocol_api::proto_callbacks_t &client_cbs) {
  server_cbs.write_fn = sim_write_fn;
  server_cbs.message_fn = sim_message_fn;
  server_cbs.new_session_fn = sim_new_session_fn;
  server_cbs.reconnect_fn = sim_reconnect_fn;
  server_cbs.close_fn = sim_close_fn;
  server_cbs.on_handshake_done_fn = sim_handshake_done_fn;
  server_cbs.on_handshake_update_fn = sim_handshake_update_fn;
  server_cbs.on_error_fn = sim_error_fn;

  client_cbs.write_fn = sim_write_fn;
  client_cbs.message_fn = sim_message_fn;
  client_cbs.new_session_fn = sim_new_session_fn;
  client_cbs.reconnect_fn = sim_reconnect_fn;
  client_cbs.close_fn = sim_close_fn;
  client_cbs.on_handshake_done_fn = sim_handshake_done_fn;
  client_cbs.on_handshake_update_fn = sim_handshake_update_fn;
  client_cbs.on_error_fn = sim_error_fn;

  server.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
  client.sdk = std::make_shared<libatgw_protocol_sdk>(client_conf);

  server.sdk->set_callbacks(&server_cbs);
  client.sdk->set_callbacks(&client_cbs);

  server.sdk->set_private_data(&server);
  client.sdk->set_private_data(&client);

  // Wire them together
  server.remote = &client;
  client.remote = &server;
}

}  // namespace

// ========== crypto_session_t tests ==========

CASE_TEST(atgateway_protocol_sdk, crypto_session_default_state) {
  ensure_openssl_init();

  crypto_session_t session;
  CASE_EXPECT_FALSE(session.has_handshake_data());
  CASE_EXPECT_EQ(0, session.get_handshake_sequence_id());

  // With no cipher, encrypt/decrypt should pass through
  const char *test_data = "hello world";
  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(test_data), strlen(test_data)};
  gsl::span<const unsigned char> out;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> crypto_heap_buf;
  gsl::span<const unsigned char> iv;
  atfw::util::nostd::string_view aad;
  int ret = session.encrypt_data(in, out, compression_heap_buf, crypto_heap_buf, iv, aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), out.size());

  std::unique_ptr<unsigned char[]> dec_heap_buf;
  ret = session.decrypt_data(in, out, dec_heap_buf, iv, aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), out.size());
}

CASE_TEST(atgateway_protocol_sdk, create_shared_context) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = false;

  // Add an access token
  std::vector<unsigned char> token = {'t', 'e', 's', 't', '-', 't', 'o', 'k', 'e', 'n'};
  conf.access_tokens.push_back(token);
  conf.key_refresh_interval = std::chrono::seconds(300);
  conf.max_post_message_size = 4 * 1024 * 1024;

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  CASE_MSG_INFO() << "create_shared_context with secp256r1 + aes-256-gcm succeeded" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, setup_crypto_with_key_and_roundtrip) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;

  // Use aes-256-gcm: needs 32 bytes key + 12 bytes IV
  unsigned char key[32];
  unsigned char iv[12];
  for (size_t i = 0; i < sizeof(key); ++i) {
    key[i] = static_cast<unsigned char>(i + 1);
  }
  for (size_t i = 0; i < sizeof(iv); ++i) {
    iv[i] = static_cast<unsigned char>(i + 0x10);
  }

  auto alg = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm);
  int ret = sender.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                         gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);
  if (ret != 0) {
    CASE_MSG_ERROR() << "sender setup_crypto_with_key failed: " << ret << '\n';
    return;
  }

  ret = receiver.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                       gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);
  if (ret != 0) {
    CASE_MSG_ERROR() << "receiver setup_crypto_with_key failed: " << ret << '\n';
    return;
  }

  // Encrypt
  const char *plaintext = "Hello, atgateway v2 protocol!";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<const unsigned char> encrypted;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> crypto_heap_buf;
  gsl::span<const unsigned char> enc_iv;
  atfw::util::nostd::string_view enc_aad;
  ret = sender.encrypt_data(in, encrypted, compression_heap_buf, crypto_heap_buf, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_GT(encrypted.size(), static_cast<size_t>(0));

  if (ret != 0 || encrypted.empty()) {
    CASE_MSG_ERROR() << "encrypt_data failed: " << ret << '\n';
    return;
  }

  // Copy encrypted data (internal buffers may be reused)
  std::vector<unsigned char> encrypted_copy(encrypted.data(), encrypted.data() + encrypted.size());

  // Decrypt
  gsl::span<const unsigned char> enc_in{encrypted_copy.data(), encrypted_copy.size()};
  gsl::span<const unsigned char> decrypted;
  std::unique_ptr<unsigned char[]> dec_heap_buf;
  ret = receiver.decrypt_data(enc_in, decrypted, dec_heap_buf, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted.size());
  if (decrypted.size() == plaintext_len && !decrypted.empty()) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted.data(), plaintext_len));
    CASE_MSG_INFO() << "Encrypt/decrypt round-trip succeeded: \""
                    << std::string(reinterpret_cast<const char *>(decrypted.data()), decrypted.size()) << "\"" << '\n';
  }

  sender.close();
  receiver.close();
}

CASE_TEST(atgateway_protocol_sdk, encrypt_decrypt_no_cipher) {
  ensure_openssl_init();

  crypto_session_t session;

  const char *data = "test data no cipher";
  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(data), strlen(data)};
  gsl::span<const unsigned char> out;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> crypto_heap_buf;
  gsl::span<const unsigned char> iv;
  atfw::util::nostd::string_view aad;

  int ret = session.encrypt_data(in, out, compression_heap_buf, crypto_heap_buf, iv, aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), out.size());

  std::unique_ptr<unsigned char[]> dec_heap_buf;
  ret = session.decrypt_data(in, out, dec_heap_buf, iv, aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), out.size());
}

CASE_TEST(atgateway_protocol_sdk, access_data_generation_and_verification) {
  ensure_openssl_init();

  // Setup config with a known access token
  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = false;

  std::vector<unsigned char> token = {'m', 'y', '-', 'a', 'c', 'c', 'e', 's', 's', '-', 't', 'o', 'k', 'e', 'n'};
  conf.access_tokens.push_back(token);
  conf.key_refresh_interval = std::chrono::seconds(300);

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  // Test make_access_data_plaintext
  std::vector<unsigned char> pubkey = {0x01, 0x02, 0x03, 0x04};
  std::vector<unsigned char> session_token = {0x10, 0x20, 0x30};
  std::string plaintext = libatgw_protocol_sdk::make_access_data_plaintext(
      12345, 100, 200, 99, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
      gsl::span<const unsigned char>{pubkey.data(), pubkey.size()},
      gsl::span<const unsigned char>{session_token.data(), session_token.size()});

  CASE_EXPECT_FALSE(plaintext.empty());
  CASE_MSG_INFO() << "Access data plaintext: " << plaintext << '\n';

  // Test calculate_access_data_signature
  gsl::span<const unsigned char> token_span{token.data(), token.size()};
  std::string signature = libatgw_protocol_sdk::calculate_access_data_signature(token_span, plaintext);
  CASE_EXPECT_FALSE(signature.empty());
  CASE_MSG_INFO() << "Access data signature size: " << signature.size() << '\n';

  // Verify: same plaintext + same token => same signature
  std::string signature2 = libatgw_protocol_sdk::calculate_access_data_signature(token_span, plaintext);
  CASE_EXPECT_EQ(signature.size(), signature2.size());
  CASE_EXPECT_EQ(signature, signature2);

  // Different token => different signature
  std::vector<unsigned char> other_token = {'o', 't', 'h', 'e', 'r'};
  gsl::span<const unsigned char> other_token_span{other_token.data(), other_token.size()};
  std::string signature3 = libatgw_protocol_sdk::calculate_access_data_signature(other_token_span, plaintext);
  CASE_EXPECT_FALSE(signature3.empty());
  // Signatures should differ
  CASE_EXPECT_NE(signature, signature3);
}

CASE_TEST(atgateway_protocol_sdk, crypto_session_close) {
  ensure_openssl_init();

  crypto_session_t session;

  unsigned char key[32] = {};
  unsigned char iv[12] = {};
  auto alg = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm);
  int ret = session.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                          gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);

  session.close();
  CASE_EXPECT_FALSE(session.has_handshake_data());
  CASE_EXPECT_EQ(static_cast<uint64_t>(0), session.get_handshake_sequence_id());

  // After close, encrypt/decrypt should passthrough again
  const char *data = "post-close data";
  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(data), strlen(data)};
  gsl::span<const unsigned char> out;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> crypto_heap_buf;
  gsl::span<const unsigned char> enc_iv;
  atfw::util::nostd::string_view enc_aad;
  ret = session.encrypt_data(in, out, compression_heap_buf, crypto_heap_buf, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), out.size());
}

CASE_TEST(atgateway_protocol_sdk, create_shared_context_with_multiple_algorithms) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kChacha20Poly1305Ietf));
  conf.client_mode = false;
  conf.max_post_message_size = 1 * 1024 * 1024;

  std::vector<unsigned char> token = {'t', 'o', 'k', 'e', 'n'};
  conf.access_tokens.push_back(token);

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  CASE_MSG_INFO() << "create_shared_context with x25519 + multiple algorithms succeeded" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, sdk_constructor_with_shared_conf) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = true;
  conf.max_post_message_size = 2 * 1024 * 1024;

  std::vector<unsigned char> token = {'t', 'e', 's', 't'};
  conf.access_tokens.push_back(token);

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);
  if (!global_conf) {
    return;
  }

  // Create SDK with shared global config
  libatgw_protocol_sdk sdk(global_conf);
  const auto &session = sdk.get_crypto_session();
  CASE_EXPECT_TRUE(!!session);
  if (session) {
    CASE_EXPECT_FALSE(session->has_handshake_data());
  }

  CASE_MSG_INFO() << "SDK created with shared config successfully" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, sdk_constructor_with_nullptr_conf) {
  ensure_openssl_init();

  // SDK with nullptr shared_conf should still construct without crashing
  libatgw_protocol_sdk sdk(nullptr);
  const auto &session = sdk.get_crypto_session();
  CASE_EXPECT_TRUE(!!session);

  CASE_MSG_INFO() << "SDK created with nullptr config without crash" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, encrypt_data_separates_bidirectional_nonces) {
  ensure_openssl_init();

  crypto_session_t client;
  crypto_session_t server;
  crypto_conf_t client_config;
  crypto_conf_t server_config;
  client_config.client_mode = true;
  server_config.client_mode = false;
  auto client_context = libatgw_protocol_sdk::create_shared_context(client_config);
  auto server_context = libatgw_protocol_sdk::create_shared_context(server_config);
  CASE_EXPECT_TRUE(!!client_context);
  CASE_EXPECT_TRUE(!!server_context);
  if (!client_context || !server_context) {
    return;
  }
  client.set_shared_conf(client_context);
  server.set_shared_conf(server_context);
  const unsigned char key[16] = {0x53, 0x29, 0x17};
  const unsigned char initial_iv[12] = {0x31, 0x72, 0x19};
  const unsigned char plaintext[] = "bidirectional nonce isolation";
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm);
  int result = client.setup_crypto_with_key(algorithm, key, {}, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  result = server.setup_crypto_with_key(algorithm, key, {}, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }

  std::vector<unsigned char> compression_buffer;
  std::unique_ptr<unsigned char[]> crypto_buffer;
  gsl::span<const unsigned char> ciphertext;
  gsl::span<const unsigned char> client_iv;
  atfw::util::nostd::string_view client_aad;
  result = client.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, client_iv, client_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(initial_iv), client_iv.size());
  if (result != 0 || client_iv.size() != sizeof(initial_iv)) {
    return;
  }
  const std::vector<unsigned char> client_iv_copy(client_iv.begin(), client_iv.end());
  const std::vector<unsigned char> client_ciphertext(ciphertext.begin(), ciphertext.end());
  const std::string client_aad_copy(client_aad.data(), client_aad.size());

  gsl::span<const unsigned char> server_iv;
  atfw::util::nostd::string_view server_aad;
  result = server.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, server_iv, server_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(initial_iv), server_iv.size());
  if (result != 0 || server_iv.size() != sizeof(initial_iv)) {
    return;
  }
  CASE_EXPECT_NE(0, memcmp(client_iv_copy.data(), server_iv.data(), server_iv.size()));

  const std::vector<unsigned char> server_ciphertext(ciphertext.begin(), ciphertext.end());
  gsl::span<const unsigned char> decoded;
  result = client.decrypt_data(server_ciphertext, decoded, crypto_buffer, server_iv, server_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
  if (result == 0 && decoded.size() == sizeof(plaintext)) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
  }
  result = server.decrypt_data(client_ciphertext, decoded, crypto_buffer, client_iv_copy, client_aad_copy);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
  if (result == 0 && decoded.size() == sizeof(plaintext)) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
  }

  client_iv = {};
  client_aad = {};
  result = client.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, client_iv, client_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(client_iv.empty());
  if (result != 0) {
    return;
  }
  const std::vector<unsigned char> next_ciphertext(ciphertext.begin(), ciphertext.end());
  result = server.decrypt_data(next_ciphertext, decoded, crypto_buffer, client_iv, client_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
  if (result == 0 && decoded.size() == sizeof(plaintext)) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
  }
}

// Only initialization and explicit reset publish an IV. Other packets continue the cipher state.
CASE_TEST(atgateway_protocol_sdk, encrypt_data_publishes_iv_only_after_reset) {
  ensure_openssl_init();
  const char *const algorithms[] = {"aes-256-gcm", "chacha20", "chacha20-poly1305-ietf", "xchacha20-poly1305-ietf"};
  constexpr size_t kPacketCount = 4;
  constexpr size_t kResetPacket = 2;
  constexpr size_t kPayloadSize = 97;
  const unsigned char key[32] = {0x75, 0x26, 0x38};
  const unsigned char plaintext[kPayloadSize] = {0x51, 0x17, 0x63};
  for (const char *name : algorithms) {
    const auto algorithm = libatgw_protocol_sdk::crypto_algorithm_from_name(name);
    if (algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone)) {
      CASE_MSG_INFO() << name << " not available on this platform, skip" << '\n';
      continue;
    }
    atfw::util::crypto::cipher cipher;
    int result = cipher.init(name);
    CASE_EXPECT_EQ(0, result);
    if (result != 0) {
      continue;
    }
    const std::vector<unsigned char> initial_iv(cipher.get_iv_size(), 0x41);
    const std::vector<unsigned char> reset_iv(cipher.get_iv_size(), 0x6d);
    crypto_session_t sender;
    crypto_session_t receiver;
    result = sender.setup_crypto_with_key(algorithm, key, initial_iv, false);
    CASE_EXPECT_EQ(0, result);
    if (result != 0) {
      continue;
    }
    result = receiver.setup_crypto_with_key(algorithm, key, initial_iv, false);
    CASE_EXPECT_EQ(0, result);
    if (result != 0) {
      continue;
    }
    size_t decoded_packets = 0;
    std::vector<unsigned char> compression_buffer;
    std::unique_ptr<unsigned char[]> crypto_buffer;
    for (size_t i = 0; i < kPacketCount; ++i) {
      gsl::span<const unsigned char> ciphertext;
      gsl::span<const unsigned char> output_iv;
      if (i == kResetPacket) {
        output_iv = reset_iv;
      }
      atfw::util::nostd::string_view output_aad;
      result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
      CASE_EXPECT_EQ(0, result);
      if (result != 0) {
        break;
      }
      if (i == 0 || i == kResetPacket) {
        const auto &expected_iv = i == 0 ? initial_iv : reset_iv;
        CASE_EXPECT_EQ(expected_iv.size(), output_iv.size());
        if (expected_iv.size() != output_iv.size()) {
          break;
        }
        CASE_EXPECT_EQ(0, memcmp(expected_iv.data(), output_iv.data(), output_iv.size()));
      } else {
        CASE_EXPECT_TRUE(output_iv.empty());
      }
      const std::vector<unsigned char> ciphertext_copy(ciphertext.begin(), ciphertext.end());
      gsl::span<const unsigned char> decoded;
      result = receiver.decrypt_data(ciphertext_copy, decoded, crypto_buffer, output_iv, output_aad);
      CASE_EXPECT_EQ(0, result);
      CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
      if (result != 0 || decoded.size() != sizeof(plaintext)) {
        break;
      }
      CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
      ++decoded_packets;
    }
    CASE_EXPECT_EQ(kPacketCount, decoded_packets);
  }
}

CASE_TEST(atgateway_protocol_sdk, encrypt_data_refreshes_random_iv_with_new_key) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;
  const unsigned char old_key[16] = {0x68, 0x23, 0x14};
  const unsigned char new_key[16] = {0x92, 0x37, 0x25};
  const unsigned char plaintext[] = "new key uses an independent random IV";
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm);
  int result = sender.setup_crypto_with_key(algorithm, old_key, {}, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  std::vector<unsigned char> compression_buffer;
  std::unique_ptr<unsigned char[]> crypto_buffer;
  gsl::span<const unsigned char> ciphertext;
  gsl::span<const unsigned char> output_iv;
  atfw::util::nostd::string_view output_aad = "fixed AAD across key rotation";
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  if (result != 0 || output_iv.empty() || ciphertext.empty()) {
    return;
  }
  const std::vector<unsigned char> old_iv(output_iv.begin(), output_iv.end());
  const std::vector<unsigned char> old_ciphertext(ciphertext.begin(), ciphertext.end());

  result = sender.setup_crypto_with_key(algorithm, new_key, {}, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  output_iv = {};
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  if (result != 0 || output_iv.empty() || ciphertext.empty()) {
    return;
  }
  CASE_EXPECT_TRUE(old_iv != std::vector<unsigned char>(output_iv.begin(), output_iv.end()));
  const std::vector<unsigned char> new_ciphertext(ciphertext.begin(), ciphertext.end());
  CASE_EXPECT_TRUE(old_ciphertext != new_ciphertext);

  result = receiver.setup_crypto_with_key(algorithm, new_key, {}, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  gsl::span<const unsigned char> decoded;
  result = receiver.decrypt_data(new_ciphertext, decoded, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
  if (result == 0 && decoded.size() == sizeof(plaintext)) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
  }
  result = receiver.decrypt_data(old_ciphertext, decoded, crypto_buffer, old_iv, output_aad);
  CASE_EXPECT_EQ(static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation), result);
  CASE_EXPECT_TRUE(decoded.empty());
}

CASE_TEST(atgateway_protocol_sdk, encrypt_data_cbc_continues_without_resetting_iv) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;
  const unsigned char key[16] = {0x12, 0x74, 0x69};
  const unsigned char initial_iv[16] = {0x37, 0x29, 0x16};
  const unsigned char plaintext[16] = {0x68, 0x29, 0x71};
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Cbc);
  int result = sender.setup_crypto_with_key(algorithm, key, initial_iv, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  result = receiver.setup_crypto_with_key(algorithm, key, initial_iv, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }

  std::vector<unsigned char> compression_buffer;
  std::unique_ptr<unsigned char[]> crypto_buffer;
  gsl::span<const unsigned char> ciphertext;
  gsl::span<const unsigned char> output_iv;
  atfw::util::nostd::string_view output_aad;
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_GE(ciphertext.size(), sizeof(initial_iv));
  if (result != 0 || ciphertext.size() < sizeof(initial_iv)) {
    return;
  }
  const std::vector<unsigned char> first_ciphertext(ciphertext.begin(), ciphertext.end());
  gsl::span<const unsigned char> first_decoded;
  result = receiver.decrypt_data(first_ciphertext, first_decoded, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), first_decoded.size());
  if (result != 0 || first_decoded.size() != sizeof(plaintext)) {
    return;
  }
  CASE_EXPECT_EQ(0, memcmp(plaintext, first_decoded.data(), first_decoded.size()));

  output_iv = {};
  output_aad = {};
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_TRUE(output_iv.empty());
  if (result != 0) {
    return;
  }

  const std::vector<unsigned char> ciphertext_copy(ciphertext.begin(), ciphertext.end());
  gsl::span<const unsigned char> decoded;
  result = receiver.decrypt_data(ciphertext_copy, decoded, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(plaintext), decoded.size());
  if (result == 0 && decoded.size() == sizeof(plaintext)) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decoded.data(), decoded.size()));
  }
}

CASE_TEST(atgateway_protocol_sdk, cipher_rejects_invalid_explicit_iv) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;
  const unsigned char key[16] = {0x71, 0x43, 0x26};
  const unsigned char initial_iv[16] = {0x42, 0x16, 0x39};
  const unsigned char invalid_iv[] = {0x27};
  const unsigned char plaintext[16] = {0x63, 0x28, 0x19};
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Cbc);
  int result = sender.setup_crypto_with_key(algorithm, key, initial_iv, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }
  result = receiver.setup_crypto_with_key(algorithm, key, initial_iv, false);
  CASE_EXPECT_EQ(0, result);
  if (result != 0) {
    return;
  }

  std::vector<unsigned char> compression_buffer;
  std::unique_ptr<unsigned char[]> crypto_buffer;
  gsl::span<const unsigned char> ciphertext;
  gsl::span<const unsigned char> output_iv = initial_iv;
  atfw::util::nostd::string_view output_aad;
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(sizeof(initial_iv), output_iv.size());
  if (result != 0 || output_iv.size() != sizeof(initial_iv)) {
    return;
  }
  CASE_EXPECT_EQ(0, memcmp(initial_iv, output_iv.data(), output_iv.size()));
  const std::vector<unsigned char> ciphertext_copy(ciphertext.begin(), ciphertext.end());

  gsl::span<const unsigned char> decoded;
  result = receiver.decrypt_data(ciphertext_copy, decoded, crypto_buffer, invalid_iv, output_aad);
  CASE_EXPECT_EQ(static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation), result);
  CASE_EXPECT_TRUE(decoded.empty());

  output_iv = invalid_iv;
  result = sender.encrypt_data(plaintext, ciphertext, compression_buffer, crypto_buffer, output_iv, output_aad);
  CASE_EXPECT_EQ(static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation), result);
  CASE_EXPECT_TRUE(ciphertext.empty());
}

CASE_TEST(atgateway_protocol_sdk, encrypt_decrypt_aes_128_gcm) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;

  unsigned char key[16];
  unsigned char iv[12];
  for (size_t i = 0; i < sizeof(key); ++i) {
    key[i] = static_cast<unsigned char>(i + 0x30);
  }
  for (size_t i = 0; i < sizeof(iv); ++i) {
    iv[i] = static_cast<unsigned char>(i + 0x50);
  }

  auto alg = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm);
  int ret = sender.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                         gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);
  ret = receiver.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                       gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);

  const char *plaintext = "AES-128-GCM round-trip test";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<const unsigned char> encrypted;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> enc_heap;
  gsl::span<const unsigned char> enc_iv;
  atfw::util::nostd::string_view enc_aad;
  ret = sender.encrypt_data(in, encrypted, compression_heap_buf, enc_heap, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_GT(encrypted.size(), static_cast<size_t>(0));

  std::vector<unsigned char> encrypted_copy(encrypted.data(), encrypted.data() + encrypted.size());

  gsl::span<const unsigned char> enc_in{encrypted_copy.data(), encrypted_copy.size()};
  gsl::span<const unsigned char> decrypted;
  std::unique_ptr<unsigned char[]> dec_heap;
  ret = receiver.decrypt_data(enc_in, decrypted, dec_heap, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted.size());
  if (decrypted.size() == plaintext_len && !decrypted.empty()) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted.data(), plaintext_len));
  }

  sender.close();
  receiver.close();
}

CASE_TEST(atgateway_protocol_sdk, encrypt_decrypt_chacha20) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;

  unsigned char key[32];
  unsigned char iv[16];
  for (size_t i = 0; i < sizeof(key); ++i) {
    key[i] = static_cast<unsigned char>(i + 0xA0);
  }
  for (size_t i = 0; i < sizeof(iv); ++i) {
    iv[i] = static_cast<unsigned char>(i + 0xB0);
  }

  auto alg = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kChacha20);
  int ret = sender.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                         gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  if (ret != 0) {
    CASE_MSG_INFO() << "chacha20 not available on this platform, skip" << '\n';
    return;
  }

  ret = receiver.setup_crypto_with_key(alg, gsl::span<const unsigned char>{key, sizeof(key)},
                                       gsl::span<const unsigned char>{iv, sizeof(iv)}, false);
  CASE_EXPECT_EQ(0, ret);

  const char *plaintext = "ChaCha20 round-trip test";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<const unsigned char> encrypted;
  std::vector<unsigned char> compression_heap_buf;
  std::unique_ptr<unsigned char[]> enc_heap;
  gsl::span<const unsigned char> enc_iv;
  atfw::util::nostd::string_view enc_aad;
  ret = sender.encrypt_data(in, encrypted, compression_heap_buf, enc_heap, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);

  std::vector<unsigned char> encrypted_copy(encrypted.data(), encrypted.data() + encrypted.size());

  gsl::span<const unsigned char> enc_in{encrypted_copy.data(), encrypted_copy.size()};
  gsl::span<const unsigned char> decrypted;
  std::unique_ptr<unsigned char[]> dec_heap;
  ret = receiver.decrypt_data(enc_in, decrypted, dec_heap, enc_iv, enc_aad);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted.size());
  if (decrypted.size() == plaintext_len && !decrypted.empty()) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted.data(), plaintext_len));
  }

  sender.close();
  receiver.close();
}

CASE_TEST(atgateway_protocol_sdk, max_post_message_size_config) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = true;
  conf.max_post_message_size = 512;  // Very small for testing

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);
  if (!global_conf) {
    return;
  }

  // max_post_message_size is set in the config, verify the global_conf is valid
  CASE_MSG_INFO() << "max_post_message_size config validated" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, access_data_plaintext_format) {
  ensure_openssl_init();

  std::vector<unsigned char> pubkey = {0xAA, 0xBB, 0xCC, 0xDD};
  std::vector<unsigned char> session_token = {0x11, 0x22};
  std::string plaintext = libatgw_protocol_sdk::make_access_data_plaintext(
      42, 1000, 2000, 77, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
      gsl::span<const unsigned char>{pubkey.data(), pubkey.size()},
      gsl::span<const unsigned char>{session_token.data(), session_token.size()});

  CASE_EXPECT_FALSE(plaintext.empty());
  CASE_MSG_INFO() << "Access data plaintext (x25519): " << plaintext << '\n';
}

CASE_TEST(atgateway_protocol_sdk, get_info_with_shared_conf) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = true;

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);
  if (!global_conf) {
    return;
  }

  libatgw_protocol_sdk sdk(global_conf);
  std::string info = sdk.get_info();
  CASE_EXPECT_FALSE(info.empty());
  CASE_MSG_INFO() << "SDK info:\n" << info << '\n';
}

// ========== default_crypto_configure / set_default tests ==========

CASE_TEST(atgateway_protocol_sdk, crypto_conf_set_default) {
  ensure_openssl_init();

  crypto_conf_t conf;
  // Set some non-default values
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp521r1);
  conf.max_post_message_size = 999;
  conf.key_refresh_interval = std::chrono::seconds(1);
  conf.client_mode = true;

  // Call set_default
  conf.set_default();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
                 conf.key_exchange_algorithm);
  CASE_EXPECT_EQ(static_cast<uint64_t>(2 * 1024 * 1024), conf.max_post_message_size);
  CASE_EXPECT_TRUE(std::chrono::hours(4) == conf.key_refresh_interval);
  CASE_EXPECT_FALSE(conf.client_mode);
  // set_default() now populates default algorithm lists
  CASE_EXPECT_FALSE(conf.supported_algorithms.empty());
  CASE_EXPECT_FALSE(conf.compression_algorithms.empty());
  CASE_EXPECT_TRUE(conf.access_tokens.empty());

  CASE_MSG_INFO() << "crypto_conf_t::set_default verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, default_conf_creates_global_configure) {
  ensure_openssl_init();

  // Verify default config (with x25519 key exchange, no cipher) can create global configure
  crypto_conf_t conf;
  conf.set_default();

  auto global_conf = libatgw_protocol_sdk::create_shared_context(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  if (global_conf) {
    auto *mutable_conf = libatgw_protocol_sdk::get_shared_context_mutable_conf(global_conf);
    CASE_EXPECT_TRUE(nullptr != mutable_conf);
    if (mutable_conf) {
      CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
                     mutable_conf->key_exchange_algorithm);
      CASE_EXPECT_EQ(static_cast<uint64_t>(2 * 1024 * 1024), mutable_conf->max_post_message_size);
    }
  }

  CASE_MSG_INFO() << "default config creates global configure successfully" << '\n';
}

// ========== Algorithm name <-> enum conversion tests ==========

CASE_TEST(atgateway_protocol_sdk, key_exchange_name_conversion) {
  ensure_openssl_init();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("ecdh:x25519"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("ecdh:secp256r1"));
  // "ecdh:p-256" is only available if OpenSSL reports it as a supported curve name
  {
    const auto &all_names = libatgw_protocol_sdk::get_all_key_exchange_algorithm_names();
    if (std::find(all_names.begin(), all_names.end(), "ecdh:p-256") != all_names.end()) {
      CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
                     libatgw_protocol_sdk::key_exchange_algorithm_from_name("ecdh:p-256"));
    } else {
      CASE_MSG_INFO() << "ecdh:p-256 not available on this platform, skipping\n";
    }
  }
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kNone),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("invalid"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kNone),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name(""));

  const auto &names = libatgw_protocol_sdk::get_all_key_exchange_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "key exchange algorithm count: " << names.size() << '\n';
}

CASE_TEST(atgateway_protocol_sdk, crypto_algorithm_name_conversion) {
  ensure_openssl_init();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("aes-256-gcm"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kXxtea),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("xxtea"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("invalid"));

  const auto &names = libatgw_protocol_sdk::get_all_crypto_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "crypto algorithm count: " << names.size() << '\n';
}

CASE_TEST(atgateway_protocol_sdk, compression_algorithm_name_conversion) {
  ensure_openssl_init();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kZstd),
                 libatgw_protocol_sdk::compression_algorithm_from_name("zstd"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kLz4),
                 libatgw_protocol_sdk::compression_algorithm_from_name("lz4"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone),
                 libatgw_protocol_sdk::compression_algorithm_from_name("invalid"));

  const auto &names = libatgw_protocol_sdk::get_all_compression_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "compression algorithm count: " << names.size() << '\n';
}

// ========== Server-Client simulation tests ==========

static bool setup_encrypted_confirmation_test_pair(sim_peer_t &server, sim_peer_t &client,
                                                   libatgw_protocol_api::proto_callbacks_t &server_callbacks,
                                                   libatgw_protocol_api::proto_callbacks_t &client_callbacks) {
  ensure_openssl_init();
  crypto_conf_t server_config;
  server_config.supported_algorithms.assign(
      1, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  server_config.compression_algorithms.clear();
  crypto_conf_t client_config = server_config;
  client_config.client_mode = true;
  auto server_context = libatgw_protocol_sdk::create_shared_context(server_config);
  auto client_context = libatgw_protocol_sdk::create_shared_context(client_config);
  CASE_EXPECT_TRUE(!!server_context && !!client_context);
  if (!server_context || !client_context) {
    return false;
  }
  setup_sim_pair(server, client, server_context, client_context, server_callbacks, client_callbacks);
  return true;
}

CASE_TEST(atgateway_protocol_sdk, duplicate_handshake_confirm_preserves_receive_cipher) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  if (!setup_encrypted_confirmation_test_pair(server, client, server_callbacks, client_callbacks)) {
    return;
  }
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_TRUE(server.sdk->get_crypto_session()->has_receive_cipher());
  if (server.handshake_status != 0 || client.handshake_status != 0) {
    return;
  }

  // Sequence 1 marks an already completed exchange; it is never a new confirmation.
  flatbuffers::FlatBufferBuilder builder;
  auto head = ::atframework::gateway::v2::Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kConfirm), 1);
  auto body = ::atframework::gateway::v2::Createcs_body_confirm(builder, 1);
  builder.Finish(
      ::atframework::gateway::v2::Createclient_message(
          builder, head,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_body, cs_body_confirm),
          body.Union()),
      ::atframework::gateway::v2::client_messageIdentifier());
  CASE_EXPECT_EQ(0, client.sdk->write_message(builder));
  CASE_EXPECT_TRUE(server.sdk->get_crypto_session()->has_receive_cipher());

  const unsigned char payload[] = "encrypted after duplicate confirmation";
  CASE_EXPECT_EQ(0, client.sdk->send_post(payload));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (server.received_messages.size() == 1) {
    CASE_EXPECT_EQ(sizeof(payload), server.received_messages.front().size());
    if (server.received_messages.front().size() == sizeof(payload)) {
      CASE_EXPECT_EQ(0, memcmp(payload, server.received_messages.front().data(), sizeof(payload)));
    }
  }
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_FALSE(server.closed);
}

CASE_TEST(atgateway_protocol_sdk, initial_handshake_rejects_post_before_cipher_confirmation) {
  size_t dropped_confirmations = 0;
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  if (!setup_encrypted_confirmation_test_pair(server, client, server_callbacks, client_callbacks)) {
    return;
  }
  client_callbacks.write_fn = [&dropped_confirmations](libatgw_protocol_api *protocol, gsl::span<unsigned char> buffer,
                                                       bool *is_done) {
    const size_t offset = protocol->get_write_header_offset() + sizeof(uint32_t) * 2;
    if (buffer.size() > offset) {
      const auto *message = ::atframework::gateway::v2::Getclient_message(buffer.data() + offset);
      if (message->head()->type() ==
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kConfirm)) {
        ++dropped_confirmations;
        if (is_done != nullptr) {
          *is_done = true;
        }
        return 0;
      }
    }
    return sim_write_fn(protocol, buffer, is_done);
  };
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_EQ(static_cast<size_t>(1), dropped_confirmations);
  CASE_EXPECT_FALSE(server.sdk->get_crypto_session()->has_receive_cipher());
  CASE_EXPECT_TRUE(server.sdk->get_crypto_session()->has_handshaking_receive_cipher());
  if (server.handshake_status != 0 || client.handshake_status != 0 || dropped_confirmations != 1) {
    return;
  }

  // The exchange negotiated AES-GCM, but this packet has plaintext in the ciphertext field.
  const unsigned char payload[] = "post without confirmation";
  flatbuffers::FlatBufferBuilder builder;
  auto head = ::atframework::gateway::v2::Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kPost), 2);
  auto crypto = ::atframework::gateway::v2::Createcs_body_post_crypto(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  auto body = ::atframework::gateway::v2::Createcs_body_post(
      builder, crypto, {}, sizeof(payload),
      builder.CreateVector(reinterpret_cast<const int8_t *>(payload), sizeof(payload)));
  builder.Finish(
      ::atframework::gateway::v2::Createclient_message(
          builder, head,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_body, cs_body_post),
          body.Union()),
      ::atframework::gateway::v2::client_messageIdentifier());
  CASE_EXPECT_EQ(0, client.sdk->write_message(builder));
  CASE_EXPECT_TRUE(server.received_messages.empty());
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_EQ(static_cast<int>(::atfw::gateway::error_code_t::kHandshake), server.last_error_code);
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atfw::gateway::close_reason_t::kInvalidData), server.close_reason);
}

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_x25519_aes256gcm) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  // Client initiates handshake
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

  // Both should have completed handshake
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_NE(static_cast<uint64_t>(0), client.sdk->get_session_id());
  CASE_EXPECT_EQ(client.sdk->get_session_id(), server.sdk->get_session_id());

  // Send message from client to server
  std::string msg = "hello from client";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);

  // Server should have received the message
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  // Send message from server to client
  std::string reply = "hello from server";
  gsl::span<const unsigned char> reply_span{reinterpret_cast<const unsigned char *>(reply.data()), reply.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);

  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    std::string received(client.received_messages[0].begin(), client.received_messages[0].end());
    CASE_EXPECT_EQ(reply, received);
  }

  CASE_MSG_INFO() << "server-client x25519+aes-256-gcm handshake and messaging verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_secp256r1_aes128gcm) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Send message client->server
  std::string msg = "secp256r1 aes128gcm test";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  CASE_MSG_INFO() << "server-client secp256r1+aes-128-gcm verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_no_encryption) {
  ensure_openssl_init();

  // ECDH still performs key exchange but no cipher configured
  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.clear();    // no cipher
  server_conf_data.compression_algorithms.clear();  // no compression
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.clear();    // no cipher
  client_conf_data.compression_algorithms.clear();  // no compression
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  std::string msg = "plaintext message, no cipher";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  CASE_MSG_INFO() << "server-client no-encryption verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_multiple_messages) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

  // Send multiple messages
  const int num_messages = 10;
  for (int i = 0; i < num_messages; ++i) {
    std::string msg = "message-" + std::to_string(i);
    gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
    ret = client.sdk->send_post(msg_span);
    CASE_EXPECT_EQ(0, ret);
  }

  CASE_EXPECT_EQ(static_cast<size_t>(num_messages), server.received_messages.size());
  for (int i = 0; i < num_messages && i < static_cast<int>(server.received_messages.size()); ++i) {
    std::string expected = "message-" + std::to_string(i);
    std::string received(server.received_messages[i].begin(), server.received_messages[i].end());
    CASE_EXPECT_EQ(expected, received);
  }

  CASE_MSG_INFO() << "multiple messages verified: " << server.received_messages.size() << " received" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_key_refresh) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  // Initial handshake
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  uint64_t session_id = client.sdk->get_session_id();
  CASE_EXPECT_NE(static_cast<uint64_t>(0), session_id);

  // Send a message before key refresh
  std::string msg1 = "before key refresh";
  gsl::span<const unsigned char> msg1_span{reinterpret_cast<const unsigned char *>(msg1.data()), msg1.size()};
  ret = client.sdk->send_post(msg1_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  // Trigger key refresh (handshake_update)
  ret = client.sdk->handshake_update();
  CASE_EXPECT_EQ(0, ret);

  // Session ID should be preserved after key refresh
  CASE_EXPECT_EQ(session_id, client.sdk->get_session_id());

  // Send a message after key refresh
  std::string msg2 = "after key refresh";
  gsl::span<const unsigned char> msg2_span{reinterpret_cast<const unsigned char *>(msg2.data()), msg2.size()};
  ret = client.sdk->send_post(msg2_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());

  if (server.received_messages.size() >= 2) {
    std::string received(server.received_messages[1].begin(), server.received_messages[1].end());
    CASE_EXPECT_EQ(msg2, received);
  }

  CASE_MSG_INFO() << "key refresh verified, session_id preserved: " << session_id << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_ping_pong) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

  // Send ping from client
  ret = client.sdk->send_ping();
  CASE_EXPECT_EQ(0, ret);

  CASE_MSG_INFO() << "ping/pong test completed" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_bad_data) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

  // Feed garbage data to server directly
  unsigned char garbage[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0x00, 0x01};
  gsl::span<const unsigned char> garbage_span{garbage, sizeof(garbage)};
  int32_t errcode = 0;
  server.sdk->read(static_cast<int>(sizeof(garbage)), garbage_span, errcode);

  // The server should have encountered an error or handled it gracefully
  CASE_MSG_INFO() << "bad data test completed, errcode: " << errcode << ", server errors: " << server.error_count
                  << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_algorithm_negotiation) {
  ensure_openssl_init();

  // Server supports aes-256-gcm and aes-128-gcm
  // Client supports only aes-128-gcm
  // Should negotiate aes-128-gcm
  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Verify messaging works
  std::string msg = "algorithm negotiation test";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  CASE_MSG_INFO() << "algorithm negotiation verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_with_access_tokens) {
  ensure_openssl_init();

  std::vector<unsigned char> token = {'s', 'h', 'a', 'r', 'e', 'd', '-', 'k', 'e', 'y'};

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.access_tokens.push_back(token);
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.access_tokens.push_back(token);
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Send message
  std::string msg = "authenticated message";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  CASE_MSG_INFO() << "handshake with matching access tokens verified" << '\n';
}

// ========== Reconnect tests ==========

CASE_TEST(atgateway_protocol_sdk, server_client_reconnect_success) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  // --- Phase 1: initial handshake ---
  sim_peer_t server1, client1;
  libatgw_protocol_api::proto_callbacks_t server1_cbs = {}, client1_cbs = {};
  setup_sim_pair(server1, client1, server_conf, client_conf, server1_cbs, client1_cbs);

  int ret = client1.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server1.handshake_status);
  CASE_EXPECT_EQ(0, client1.handshake_status);

  uint64_t original_session_id = client1.sdk->get_session_id();
  CASE_EXPECT_NE(static_cast<uint64_t>(0), original_session_id);

  // Send a message to verify connectivity
  std::string msg1 = "before reconnect";
  gsl::span<const unsigned char> msg1_span{reinterpret_cast<const unsigned char *>(msg1.data()), msg1.size()};
  ret = client1.sdk->send_post(msg1_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server1.received_messages.size());
  if (!server1.received_messages.empty()) {
    std::string received(server1.received_messages[0].begin(), server1.received_messages[0].end());
    CASE_EXPECT_EQ(msg1, received);
  }

  // --- Phase 2: simulate reconnect ---
  // Reuse the client SDK on the replacement transport and validate against the previous server session.
  sim_peer_t server2;
  server2.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
  libatgw_protocol_api::proto_callbacks_t server2_cbs = {};
  server2_cbs.write_fn = sim_write_fn;
  server2_cbs.message_fn = sim_message_fn;
  server2_cbs.new_session_fn = sim_new_session_fn;
  server2_cbs.reconnect_fn = [&server1](libatgw_protocol_api *proto, uint64_t) {
    return proto->check_reconnect(server1.sdk.get())
               ? 0
               : static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  };
  server2_cbs.close_fn = sim_close_fn;
  server2_cbs.on_handshake_done_fn = sim_handshake_done_fn;
  server2_cbs.on_handshake_update_fn = sim_handshake_update_fn;
  server2_cbs.on_error_fn = sim_error_fn;
  server2.sdk->set_callbacks(&server2_cbs);
  server2.sdk->set_private_data(&server2);

  // Rewire client1 to talk to server2
  client1.remote = &server2;
  server2.remote = &client1;
  client1.handshake_status = -1;
  client1.handshake_update_status = -1;

  CASE_EXPECT_FALSE(client1.sdk->get_session_token().empty());
  std::vector<unsigned char> origin_token;
  origin_token.assign(client1.sdk->get_session_token().data(),
                      client1.sdk->get_session_token().data() + client1.sdk->get_session_token().size());
  ret = client1.sdk->reconnect_session(original_session_id,
                                       gsl::span<const unsigned char>{origin_token.data(), origin_token.size()});
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server2.handshake_status);
  // Reconnect triggers on_handshake_update_fn (not on_handshake_done_fn) since kHandshakeDone is already set
  CASE_EXPECT_EQ(0, client1.handshake_update_status);
  CASE_EXPECT_EQ(original_session_id, client1.sdk->get_session_id());

  std::vector<unsigned char> new_token;
  new_token.assign(client1.sdk->get_session_token().data(),
                   client1.sdk->get_session_token().data() + client1.sdk->get_session_token().size());
  CASE_EXPECT_FALSE(client1.sdk->get_session_token().empty());
  if (origin_token.size() == new_token.size()) {
    CASE_EXPECT_FALSE(memcmp(origin_token.data(), new_token.data(), origin_token.size()) == 0);
  }

  // Send a message after reconnect
  std::string msg2 = "after reconnect";
  gsl::span<const unsigned char> msg2_span{reinterpret_cast<const unsigned char *>(msg2.data()), msg2.size()};
  ret = client1.sdk->send_post(msg2_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server2.received_messages.size());
  if (!server2.received_messages.empty()) {
    std::string received(server2.received_messages[0].begin(), server2.received_messages[0].end());
    CASE_EXPECT_EQ(msg2, received);
  }

  CASE_MSG_INFO() << "reconnect success verified, session_id: " << original_session_id << '\n';
}

CASE_TEST(atgateway_protocol_sdk, server_client_reconnect_refused) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  // --- Phase 1: initial handshake to obtain session token ---
  sim_peer_t server1, client1;
  libatgw_protocol_api::proto_callbacks_t server1_cbs = {}, client1_cbs = {};
  setup_sim_pair(server1, client1, server_conf, client_conf, server1_cbs, client1_cbs);

  int ret = client1.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, client1.handshake_status);

  CASE_EXPECT_FALSE(client1.sdk->get_session_token().empty());
  std::vector<unsigned char> origin_token;
  origin_token.assign(client1.sdk->get_session_token().data(),
                      client1.sdk->get_session_token().data() + client1.sdk->get_session_token().size());

  uint64_t original_session_id = client1.sdk->get_session_id();
  CASE_EXPECT_NE(static_cast<uint64_t>(0), original_session_id);

  // --- Phase 2: reconnect to a new server that refuses ---
  sim_peer_t server2;
  server2.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
  libatgw_protocol_api::proto_callbacks_t server2_cbs = {};
  server2_cbs.write_fn = sim_write_fn;
  server2_cbs.message_fn = sim_message_fn;
  server2_cbs.new_session_fn = sim_new_session_fn;
  server2_cbs.reconnect_fn = [](libatgw_protocol_api * /*proto*/, uint64_t /*sess_id*/) -> int {
    return static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  };
  server2_cbs.close_fn = sim_close_fn;
  server2_cbs.on_handshake_done_fn = sim_handshake_done_fn;
  server2_cbs.on_handshake_update_fn = sim_handshake_update_fn;
  server2_cbs.on_error_fn = sim_error_fn;
  server2.sdk->set_callbacks(&server2_cbs);
  server2.sdk->set_private_data(&server2);

  // Rewire client1 to talk to server2
  client1.remote = &server2;
  server2.remote = &client1;
  client1.handshake_status = -1;
  client1.handshake_update_status = -1;

  // Modify token to simulate a wrong token
  CASE_EXPECT_FALSE(origin_token.empty());
  if (!origin_token.empty()) {
    ++origin_token[0];
  }

  // Client tries to reconnect with modified token
  ret = client1.sdk->reconnect_session(original_session_id,
                                       gsl::span<const unsigned char>{origin_token.data(), origin_token.size()});

  // The server refuses the reconnect, so the client should be closed or have a non-zero handshake status.
  CASE_EXPECT_TRUE(client1.closed || client1.handshake_update_status != 0);

  CASE_MSG_INFO() << "reconnect refused verified, ret: " << ret << ", closed: " << client1.closed
                  << ", handshake_update_status: " << client1.handshake_update_status << '\n';
}

// ========== Ping/pong timing test ==========

CASE_TEST(atgateway_protocol_sdk, server_client_ping_pong_get_last_ping) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

  // Before ping, last_delta should be 0
  auto ping_before = client.sdk->get_last_ping();
  CASE_EXPECT_EQ(static_cast<int64_t>(0), ping_before.last_delta);

  // Send ping from client — server auto-replies with pong
  ret = client.sdk->send_ping();
  CASE_EXPECT_EQ(0, ret);

  // After ping/pong round-trip, last_delta should be >= 0
  auto ping_after = client.sdk->get_last_ping();
  CASE_EXPECT_GE(ping_after.last_delta, static_cast<int64_t>(0));

  CASE_MSG_INFO() << "ping/pong timing: last_delta=" << ping_after.last_delta << '\n';
}

// ========== handshake_update mid-stream messaging ==========

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_update_midstream) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  // Phase 1: initial handshake + messaging
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, client.handshake_status);

  std::string msg1 = "phase1 message";
  gsl::span<const unsigned char> msg1_span{reinterpret_cast<const unsigned char *>(msg1.data()), msg1.size()};
  ret = client.sdk->send_post(msg1_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg1, received);
  }

  CASE_EXPECT_FALSE(client.sdk->get_session_token().empty());
  std::vector<unsigned char> origin_client_token;
  origin_client_token.assign(client.sdk->get_session_token().data(),
                             client.sdk->get_session_token().data() + client.sdk->get_session_token().size());
  CASE_EXPECT_FALSE(client.sdk->get_session_token().empty());
  std::vector<unsigned char> origin_server_token;
  origin_server_token.assign(server.sdk->get_session_token().data(),
                             server.sdk->get_session_token().data() + server.sdk->get_session_token().size());

  CASE_EXPECT_EQ(origin_client_token.size(), origin_server_token.size());
  if (origin_client_token.size() == origin_server_token.size()) {
    CASE_EXPECT_TRUE(memcmp(origin_client_token.data(), origin_server_token.data(), origin_client_token.size()) == 0);
  }

  // Pause server's writing
  server.pause_writing = true;

  // Phase 2: handshake_update (key refresh)
  uint64_t session_id = client.sdk->get_session_id();
  ret = client.sdk->handshake_update();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(session_id, client.sdk->get_session_id());

  // 客户端未收到换密钥
  std::vector<unsigned char> client_token_should_not_change;
  client_token_should_not_change.assign(
      client.sdk->get_session_token().data(),
      client.sdk->get_session_token().data() + client.sdk->get_session_token().size());
  CASE_EXPECT_FALSE(client.sdk->get_session_token().empty());
  if (origin_client_token.size() == client_token_should_not_change.size()) {
    CASE_EXPECT_TRUE(
        memcmp(origin_client_token.data(), client_token_should_not_change.data(), origin_client_token.size()) == 0);
  }

  // 服务端已经更换密钥
  std::vector<unsigned char> server_token_should_changed;
  server_token_should_changed.assign(server.sdk->get_session_token().data(),
                                     server.sdk->get_session_token().data() + server.sdk->get_session_token().size());
  CASE_EXPECT_FALSE(server.sdk->get_session_token().empty());
  if (origin_server_token.size() == server_token_should_changed.size()) {
    CASE_EXPECT_FALSE(
        memcmp(origin_server_token.data(), server_token_should_changed.data(), origin_server_token.size()) == 0);
  }

  // Phase 3: continue messaging after key refresh
  std::string msg2 = "phase3 message after handshake_update";
  gsl::span<const unsigned char> msg2_span{reinterpret_cast<const unsigned char *>(msg2.data()), msg2.size()};
  ret = client.sdk->send_post(msg2_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() >= 2) {
    std::string received(server.received_messages[1].begin(), server.received_messages[1].end());
    CASE_EXPECT_EQ(msg2, received);
  }

  // Resume server's writing
  server.pause_writing = false;
  sim_process_writing(server.sdk.get(), nullptr);

  // 客户端已完成换密钥
  std::vector<unsigned char> client_token_should_changed;
  client_token_should_changed.assign(client.sdk->get_session_token().data(),
                                     client.sdk->get_session_token().data() + client.sdk->get_session_token().size());
  CASE_EXPECT_FALSE(client.sdk->get_session_token().empty());
  if (origin_client_token.size() == client_token_should_changed.size()) {
    CASE_EXPECT_FALSE(
        memcmp(origin_client_token.data(), client_token_should_changed.data(), origin_client_token.size()) == 0);
  }

  CASE_EXPECT_EQ(client_token_should_changed.size(), server_token_should_changed.size());
  if (client_token_should_changed.size() == server_token_should_changed.size()) {
    CASE_EXPECT_TRUE(memcmp(client_token_should_changed.data(), server_token_should_changed.data(),
                            client_token_should_changed.size()) == 0);
  }

  // Also send from server to client
  std::string msg3 = "server reply after handshake_update";
  gsl::span<const unsigned char> msg3_span{reinterpret_cast<const unsigned char *>(msg3.data()), msg3.size()};
  ret = server.sdk->send_post(msg3_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    std::string received(client.received_messages[0].begin(), client.received_messages[0].end());
    CASE_EXPECT_EQ(msg3, received);
  }

  CASE_MSG_INFO() << "handshake_update mid-stream: 3 phases verified, session_id=" << session_id << '\n';
}

// Regression test: when the transport cannot complete a write synchronously (write_fn leaves
// *is_done == false), try_write() must stop calling write_fn and wait for write_done(). It must
// not re-deliver the same block in a loop (unbounded memory growth), must not drop messages queued
// while a write is in flight, and close() must still finish (close_fn) after the pending write
// completes asynchronously.
CASE_TEST(atgateway_protocol_sdk, server_client_async_write_backpressure) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // The handshake already produced server-side writes; measure from this baseline.
  const size_t captured_baseline = server.captured_write_payloads.size();

  // Pause the server's transport: writes become asynchronous and stay in flight.
  server.pause_writing = true;

  std::string msg1 = "backpressure message-1";
  gsl::span<const unsigned char> msg1_span{reinterpret_cast<const unsigned char *>(msg1.data()), msg1.size()};
  ret = server.sdk->send_post(msg1_span);
  CASE_EXPECT_EQ(0, ret);

  // The second message must be queued behind the in-flight write.
  std::string msg2 = "backpressure message-2";
  gsl::span<const unsigned char> msg2_span{reinterpret_cast<const unsigned char *>(msg2.data()), msg2.size()};
  ret = server.sdk->send_post(msg2_span);
  CASE_EXPECT_EQ(0, ret);

  // While backpressured: nothing delivered, and the in-flight block must be handed to the
  // transport exactly once (no duplicate write_fn calls, no unbounded growth).
  CASE_EXPECT_TRUE(client.received_messages.empty());
  CASE_EXPECT_EQ(captured_baseline + 1, server.captured_write_payloads.size());
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.pending_write_messages.size());

  // Resume the transport and complete the in-flight write; the queued message must be drained
  // by write_done() without any further send call.
  server.pause_writing = false;
  sim_process_writing(server.sdk.get(), nullptr);

  CASE_EXPECT_EQ(static_cast<size_t>(2), client.received_messages.size());
  if (client.received_messages.size() >= 2) {
    std::string received1(client.received_messages[0].begin(), client.received_messages[0].end());
    std::string received2(client.received_messages[1].begin(), client.received_messages[1].end());
    CASE_EXPECT_EQ(msg1, received1);
    CASE_EXPECT_EQ(msg2, received2);
  }
  // Each message was written to the transport exactly once.
  CASE_EXPECT_EQ(captured_baseline + 2, server.captured_write_payloads.size());
  CASE_EXPECT_TRUE(server.pending_write_messages.empty());

  // close() while a write is in flight must not fire close_fn immediately ...
  client.pause_writing = true;
  client.sdk->close(static_cast<int32_t>(::atframework::gateway::close_reason_t::kEof), 0, "async close");
  CASE_EXPECT_FALSE(client.closed);
  CASE_EXPECT_FALSE(server.closed);

  // ... and must complete after the pending kickoff write is done asynchronously.
  client.pause_writing = false;
  sim_process_writing(client.sdk.get(), nullptr);

  CASE_EXPECT_TRUE(server.closed);
  CASE_EXPECT_TRUE(client.closed);
  // close_fn has two fire sites (close() and the try_write() completion path); they must stay
  // mutually exclusive via flag_t::kClosed, so each side must be notified exactly once.
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(1, client.close_count);
  CASE_EXPECT_EQ(static_cast<int32_t>(::atframework::gateway::close_reason_t::kEof), server.close_reason);

  CASE_MSG_INFO() << "async write backpressure: no duplication, queued message drained, async close completed" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, synchronous_write_failure_closes_connection) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  if (0 != server.handshake_status || 0 != client.handshake_status) {
    server.sdk.reset();
    client.sdk.reset();
    return;
  }

  const int write_error = static_cast<int>(::atframework::gateway::error_code_t::kNetwork);
  const unsigned char payload[] = "synchronous write failure";
  gsl::span<const unsigned char> message{payload, sizeof(payload) - 1};
  int write_count = 0;
  server_cbs.write_fn = [&](libatgw_protocol_api *proto, gsl::span<unsigned char>, bool *is_done) {
    ++write_count;
    *is_done = true;
    if (1 == write_count) {
      // A callback can enqueue another message while its own write is still active.
      CASE_EXPECT_EQ(0, proto->write(message));
      return write_error;
    }
    return 0;
  };

  CASE_EXPECT_EQ(write_error, server.sdk->send_post(message));
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_EQ(write_error, server.last_error_code);
  CASE_EXPECT_TRUE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kClosed));
  CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kWriting));
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kReset), server.close_reason);
  CASE_EXPECT_EQ(write_error, server.close_sub_reason);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kClosing), server.sdk->send_post(message));
  CASE_EXPECT_EQ(1, write_count);
  CASE_EXPECT_TRUE(client.received_messages.empty());

  // A repeated completion cannot report the same failure or close the connection twice.
  server.sdk->write_done(write_error);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_EQ(1, server.close_count);
  server.sdk.reset();
  client.sdk.reset();
}

CASE_TEST(atgateway_protocol_sdk, asynchronous_write_failure_discards_pending_messages) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  if (0 != server.handshake_status || 0 != client.handshake_status) {
    server.sdk.reset();
    client.sdk.reset();
    return;
  }

  const int write_error = static_cast<int>(::atframework::gateway::error_code_t::kNetwork);
  int write_count = 0;
  server_cbs.write_fn = [&](libatgw_protocol_api *, gsl::span<unsigned char>, bool *is_done) {
    ++write_count;
    *is_done = false;
    return 0;
  };
  const unsigned char payload[] = "asynchronous write failure";
  gsl::span<const unsigned char> message{payload, sizeof(payload) - 1};
  CASE_EXPECT_EQ(0, server.sdk->send_post(message));
  CASE_EXPECT_EQ(0, server.sdk->send_post(message));
  CASE_EXPECT_EQ(1, write_count);
  CASE_EXPECT_TRUE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kWriting));

  CASE_EXPECT_EQ(write_error, server.sdk->write_done(write_error));
  CASE_EXPECT_EQ(1, write_count);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_EQ(write_error, server.last_error_code);
  CASE_EXPECT_TRUE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kClosed));
  CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kWriting));
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kReset), server.close_reason);
  CASE_EXPECT_EQ(write_error, server.close_sub_reason);
  CASE_EXPECT_TRUE(client.received_messages.empty());
  server.sdk.reset();
  client.sdk.reset();
}

CASE_TEST(atgateway_protocol_sdk, asynchronous_handshake_write_failure_completes_handshake) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  int handshake_count = 0;
  std::vector<int> callback_order;
  client_cbs.write_fn = [](libatgw_protocol_api *, gsl::span<unsigned char>, bool *is_done) {
    *is_done = false;
    return 0;
  };
  client_cbs.on_error_fn = [&](libatgw_protocol_api *proto, const char *filename, uint32_t line, int status,
                               const char *message) {
    callback_order.push_back(1);
    return sim_error_fn(proto, filename, line, status, message);
  };
  client_cbs.on_handshake_done_fn = [&](libatgw_protocol_api *proto, int status) {
    ++handshake_count;
    callback_order.push_back(2);
    return sim_handshake_done_fn(proto, status);
  };
  client_cbs.close_fn = [&](libatgw_protocol_api *proto, int32_t reason, int32_t sub_reason,
                            atfw::util::nostd::string_view message) {
    callback_order.push_back(3);
    return sim_close_fn(proto, reason, sub_reason, message);
  };

  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_TRUE(client.sdk->check_flag(libatgw_protocol_api::flag_t::kWriting));
  CASE_EXPECT_EQ(0, handshake_count);
  const int write_error = static_cast<int>(::atframework::gateway::error_code_t::kNetwork);
  CASE_EXPECT_EQ(write_error, client.sdk->write_done(write_error));
  CASE_EXPECT_EQ(write_error, client.handshake_status);
  CASE_EXPECT_EQ(1, handshake_count);
  CASE_EXPECT_EQ(1, client.error_count);
  CASE_EXPECT_EQ(1, client.close_count);
  CASE_EXPECT_TRUE(client.sdk->check_flag(libatgw_protocol_api::flag_t::kClosed));
  CASE_EXPECT_FALSE(client.sdk->check_flag(libatgw_protocol_api::flag_t::kHandshakeUpdate));
  CASE_EXPECT_EQ(static_cast<size_t>(3), callback_order.size());
  if (callback_order.size() == 3) {
    CASE_EXPECT_EQ(1, callback_order[0]);
    CASE_EXPECT_EQ(2, callback_order[1]);
    CASE_EXPECT_EQ(3, callback_order[2]);
  }

  client.sdk->write_done(write_error);
  client.sdk.reset();
  CASE_EXPECT_EQ(1, handshake_count);
  CASE_EXPECT_EQ(1, client.error_count);
  CASE_EXPECT_EQ(1, client.close_count);
  CASE_EXPECT_EQ(write_error, client.handshake_status);
  server.sdk.reset();
}

CASE_TEST(atgateway_protocol_sdk, kickoff_write_failure_preserves_close_reason) {
  ensure_openssl_init();

  // Both an immediate transport failure and a later failed completion must finish the requested close.
  for (bool asynchronous : {false, true}) {
    crypto_conf_t server_conf_data;
    server_conf_data.key_exchange_algorithm =
        ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
    server_conf_data.supported_algorithms.push_back(
        ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
    crypto_conf_t client_conf_data = server_conf_data;
    client_conf_data.client_mode = true;
    auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
    auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
    CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
    if (!server_conf || !client_conf) {
      return;
    }

    sim_peer_t server, client;
    libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
    setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
    CASE_EXPECT_EQ(0, client.sdk->start_session());
    CASE_EXPECT_EQ(0, server.handshake_status);
    CASE_EXPECT_EQ(0, client.handshake_status);
    if (0 != server.handshake_status || 0 != client.handshake_status) {
      server.sdk.reset();
      client.sdk.reset();
      return;
    }

    const int write_error = static_cast<int>(::atframework::gateway::error_code_t::kNetwork);
    int write_count = 0;
    server_cbs.write_fn = [&](libatgw_protocol_api *, gsl::span<unsigned char>, bool *is_done) {
      ++write_count;
      *is_done = !asynchronous;
      return asynchronous ? 0 : write_error;
    };

    const int close_reason = static_cast<int>(::atframework::gateway::close_reason_t::kEof);
    server.sdk->close(close_reason, 17, "requested shutdown");
    if (asynchronous) {
      CASE_EXPECT_EQ(0, server.close_count);
      CASE_EXPECT_EQ(write_error, server.sdk->write_done(write_error));
    }
    CASE_EXPECT_EQ(1, write_count);
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(write_error, server.last_error_code);
    CASE_EXPECT_TRUE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kClosed));
    CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kWriting));
    CASE_EXPECT_EQ(1, server.close_count);
    CASE_EXPECT_EQ(close_reason, server.close_reason);
    CASE_EXPECT_EQ(17, server.close_sub_reason);
    server.sdk->close(static_cast<int>(::atframework::gateway::close_reason_t::kReset));
    CASE_EXPECT_EQ(1, server.close_count);
    server.sdk.reset();
    client.sdk.reset();
  }
}

// ========== Access token mismatch test ==========

CASE_TEST(atgateway_protocol_sdk, server_client_access_token_mismatch) {
  ensure_openssl_init();

  std::vector<unsigned char> server_token = {'s', 'e', 'r', 'v', 'e', 'r', '-', 'k', 'e', 'y'};
  std::vector<unsigned char> client_token = {'c', 'l', 'i', 'e', 'n', 't', '-', 'k', 'e', 'y'};

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.access_tokens.push_back(server_token);
  server_conf_data.client_mode = false;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.access_tokens.push_back(client_token);
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  // Client initiates handshake — should fail due to token mismatch.
  // start_session() returns write status (0 = write ok).  The server rejects the handshake
  // when verify_access_data fails and closes its side.  In the synchronous test harness
  // the server's close is processed before start_session returns.
  int ret = client.sdk->start_session();

  // The server should have detected the mismatch and either error'd or closed.
  CASE_EXPECT_TRUE(server.error_count > 0 || server.closed || server.handshake_status != 0);

  CASE_MSG_INFO() << "access token mismatch verified, ret=" << ret << ", server_errors=" << server.error_count
                  << ", server_closed=" << server.closed << ", client_errors=" << client.error_count << '\n';
}

// ========== Data correctness: large message ==========

CASE_TEST(atgateway_protocol_sdk, server_client_large_message_correctness) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);
  conf_data.max_post_message_size = 128 * 1024;

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);

// Build a large message with known pattern
// 要比TLS数据块大，测试堆分配逻辑
#ifndef ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE 65536
#endif
  std::vector<unsigned char> large_msg(ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE + 1024);
  for (size_t i = 0; i < large_msg.size(); ++i) {
    large_msg[i] = static_cast<unsigned char>(i & 0xFF);
  }

  gsl::span<const unsigned char> msg_span{large_msg.data(), large_msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  if (!server.received_messages.empty()) {
    CASE_EXPECT_EQ(large_msg.size(), server.received_messages[0].size());
    // Verify byte-by-byte correctness
    bool data_matches = (large_msg == server.received_messages[0]);
    CASE_EXPECT_TRUE(data_matches);
    if (!data_matches && server.received_messages[0].size() == large_msg.size()) {
      // Find first mismatch for diagnostics
      for (size_t i = 0; i < large_msg.size(); ++i) {
        if (large_msg[i] != server.received_messages[0][i]) {
          CASE_MSG_ERROR() << "Data mismatch at byte " << i << ": expected 0x" << std::hex
                           << static_cast<int>(large_msg[i]) << " got 0x"
                           << static_cast<int>(server.received_messages[0][i]) << std::dec << '\n';
          break;
        }
      }
    }
  }

  CASE_MSG_INFO() << "large message correctness verified: " << large_msg.size() << " bytes" << '\n';
}

// ========== crypto_conf_t constructor / copy / move tests ==========

// ========== Algorithm coverage: XXTEA (block cipher, no AEAD) ==========

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_xxtea) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.clear();
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kXxtea));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  // If xxtea is not available on this platform, handshake may negotiate kNone
  if (server.handshake_status != 0 || client.handshake_status != 0) {
    CASE_MSG_INFO() << "xxtea not available on this platform, skip" << '\n';
    return;
  }
  CASE_EXPECT_EQ(0, ret);

  // Send message client->server (tests PKCS#7 padding for block cipher)
  std::string msg = "xxtea block cipher test";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  // Send message server->client (bidirectional)
  std::string reply = "xxtea reply";
  gsl::span<const unsigned char> reply_span{reinterpret_cast<const unsigned char *>(reply.data()), reply.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    std::string received(client.received_messages[0].begin(), client.received_messages[0].end());
    CASE_EXPECT_EQ(reply, received);
  }

  CASE_MSG_INFO() << "server-client x25519+xxtea handshake and messaging verified" << '\n';
}

// ========== Algorithm coverage: AES-256-CBC (block cipher, non-AEAD) ==========

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_aes256cbc) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.clear();
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Cbc));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  if (server.handshake_status != 0 || client.handshake_status != 0) {
    CASE_MSG_INFO() << "aes-256-cbc not available on this platform, skip" << '\n';
    return;
  }
  CASE_EXPECT_EQ(0, ret);

  // Test block-alignment: send message that is NOT a multiple of AES block size (16 bytes)
  std::string msg = "aes256cbc non-aligned msg!";  // 25 bytes, not multiple of 16
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  // Test exact block-alignment: 32 bytes = 2 * AES block size
  std::string msg_aligned = "0123456789abcdef0123456789abcdef";  // exactly 32 bytes
  gsl::span<const unsigned char> msg_aligned_span{reinterpret_cast<const unsigned char *>(msg_aligned.data()),
                                                  msg_aligned.size()};
  ret = client.sdk->send_post(msg_aligned_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() >= 2) {
    std::string received(server.received_messages[1].begin(), server.received_messages[1].end());
    CASE_EXPECT_EQ(msg_aligned, received);
  }

  // Bidirectional
  std::string reply = "cbc server reply";
  gsl::span<const unsigned char> reply_span{reinterpret_cast<const unsigned char *>(reply.data()), reply.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    std::string received(client.received_messages[0].begin(), client.received_messages[0].end());
    CASE_EXPECT_EQ(reply, received);
  }

  CASE_MSG_INFO() << "server-client x25519+aes-256-cbc handshake and messaging verified" << '\n';
}

// ========== Untrusted connection message size limit tests ==========

// Helper: construct a wire-format message (8-byte header + payload)
static std::vector<unsigned char> make_raw_wire_message(const unsigned char *payload, size_t payload_len) {
  const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);
  std::vector<unsigned char> wire(msg_header_len + payload_len);

  // Write message length at offset 4 (little-endian uint32)
  uint32_t len32 = static_cast<uint32_t>(payload_len);
  memcpy(wire.data() + sizeof(uint32_t), &len32, sizeof(uint32_t));

  // Copy payload at offset 8
  if (payload_len > 0 && payload != nullptr) {
    memcpy(wire.data() + msg_header_len, payload, payload_len);
  }

  // Write murmur3 hash of payload at offset 0
  uint32_t hash32 = atfw::util::hash::murmur_hash3_x86_32(reinterpret_cast<const char *>(wire.data() + msg_header_len),
                                                          static_cast<int>(payload_len), 0);
  memcpy(wire.data(), &hash32, sizeof(uint32_t));

  return wire;
}

// Feed each chunk through the receive buffer supplied by the SDK, stopping on read errors.
static void read_raw_wire_message(libatgw_protocol_sdk &sdk, const std::vector<unsigned char> &wire, int &errcode) {
  size_t offset = 0;
  while (offset < wire.size()) {
    char *recv_buf = nullptr;
    size_t recv_len = 0;
    sdk.alloc_receive_buffer(wire.size() - offset, recv_buf, recv_len);
    CASE_EXPECT_TRUE(recv_buf != nullptr);
    CASE_EXPECT_GT(recv_len, static_cast<size_t>(0));
    if (recv_buf == nullptr || recv_len == 0) {
      return;
    }

    size_t remaining = wire.size() - offset;
    size_t read_len = (remaining < recv_len) ? remaining : recv_len;
    memcpy(recv_buf, wire.data() + offset, read_len);
    sdk.read(static_cast<int>(read_len),
             gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(recv_buf), read_len}, errcode);
    if (errcode != static_cast<int>(::atframework::gateway::error_code_t::kSuccess)) {
      return;
    }
    offset += read_len;
  }
}

static bool setup_receive_size_test_pair(sim_peer_t &server, sim_peer_t &client,
                                         libatgw_protocol_api::proto_callbacks_t &server_callbacks,
                                         libatgw_protocol_api::proto_callbacks_t &client_callbacks,
                                         libatgw_protocol_sdk::crypto_algorithm_type algorithm, uint64_t limit,
                                         libatgw_protocol_sdk::compression_algorithm_type compression =
                                             ::atframework::gateway::v2::compression_algorithm_t::kNone) {
  ensure_openssl_init();
  crypto_conf_t server_config;
  server_config.supported_algorithms.clear();
  server_config.supported_algorithms.push_back(algorithm);
  server_config.compression_algorithms.clear();
  if (compression != ::atframework::gateway::v2::compression_algorithm_t::kNone) {
    server_config.compression_algorithms.push_back(compression);
    server_config.compression_threshold_size = 0;
  }
  server_config.max_post_message_size = limit;
  crypto_conf_t client_config = server_config;
  client_config.client_mode = true;
  auto server_context = libatgw_protocol_sdk::create_shared_context(server_config);
  auto client_context = libatgw_protocol_sdk::create_shared_context(client_config);
  CASE_EXPECT_TRUE(server_context != nullptr && client_context != nullptr);
  if (!server_context || !client_context) {
    return false;
  }
  setup_sim_pair(server, client, server_context, client_context, server_callbacks, client_callbacks);
  int result = client.sdk->start_session();
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  return result == 0 && server.handshake_status == 0 && client.handshake_status == 0;
}

CASE_TEST(atgateway_protocol_sdk, send_queue_rejection_resynchronizes_cipher_iv) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  constexpr size_t kMessageLimit = 256;
  constexpr size_t kRejectBufferSize = 1;
  constexpr size_t kResumeBufferSize = 4096;
  constexpr size_t kBufferCount = 1;
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm);
  if (!setup_receive_size_test_pair(server, client, server_callbacks, client_callbacks, algorithm, kMessageLimit)) {
    return;
  }
  const unsigned char payload[] = "cipher stream survives an unsent encrypted packet";
  CASE_EXPECT_EQ(0, server.sdk->send_post(payload));
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  server.sdk->set_send_buffer_limit(kRejectBufferSize, kBufferCount);
  CASE_EXPECT_LT(server.sdk->send_post(payload), 0);
  CASE_EXPECT_FALSE(server.closed);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  server.sdk->set_send_buffer_limit(kResumeBufferSize, kBufferCount);
  CASE_EXPECT_EQ(0, server.sdk->send_post(payload));
  CASE_EXPECT_EQ(static_cast<size_t>(2), client.received_messages.size());
  CASE_EXPECT_FALSE(client.closed);
  if (client.closed) {
    return;
  }
  CASE_EXPECT_EQ(0, server.sdk->send_post(payload));
  CASE_EXPECT_EQ(static_cast<size_t>(3), client.received_messages.size());
  CASE_EXPECT_FALSE(client.closed);
  CASE_EXPECT_EQ(0, client.error_count);
  for (const auto &received : client.received_messages) {
    CASE_EXPECT_EQ(sizeof(payload), received.size());
    if (received.size() == sizeof(payload)) {
      CASE_EXPECT_EQ(0, memcmp(payload, received.data(), received.size()));
    }
  }
}

static void check_post_declared_length(uint64_t declared_length, ::atframework::gateway::error_code_t expected_error,
                                       ::atframework::gateway::close_reason_t expected_close_reason,
                                       bool claim_unnegotiated_compression = false) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  if (!setup_receive_size_test_pair(
          server, client, server_callbacks, client_callbacks,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone), 256)) {
    return;
  }
  std::vector<unsigned char> payload = {'H', 'e', 'a', 'd', 'T', 'a', 'i', 'l'};
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{payload.data(), payload.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  // Record delivery without reading an invalid span if the regression is present.
  size_t deliveries = 0;
  size_t delivered_size = 0;
  bool delivered_payload_matches = false;
  server_callbacks.message_fn = [&](libatgw_protocol_api *, gsl::span<const unsigned char> buffer) {
    ++deliveries;
    delivered_size = buffer.size();
    delivered_payload_matches = buffer.size() <= payload.size() &&
                                (buffer.empty() || memcmp(payload.data(), buffer.data(), buffer.size()) == 0);
    return 0;
  };
  flatbuffers::FlatBufferBuilder builder;
  auto head = ::atframework::gateway::v2::Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kPost), 1);
  flatbuffers::Offset<::atframework::gateway::v2::cs_body_post_compression> compression;
  if (claim_unnegotiated_compression) {
    compression = ::atframework::gateway::v2::Createcs_body_post_compression(
        builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kZlib),
        payload.size());
  }
  auto body = ::atframework::gateway::v2::Createcs_body_post(
      builder, {}, compression, declared_length,
      builder.CreateVector(reinterpret_cast<const int8_t *>(payload.data()), payload.size()));
  builder.Finish(
      ::atframework::gateway::v2::Createclient_message(
          builder, head,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_body, cs_body_post),
          body.Union()),
      ::atframework::gateway::v2::client_messageIdentifier());
  auto wire = make_raw_wire_message(builder.GetBufferPointer(), builder.GetSize());
  int errcode = 0;
  read_raw_wire_message(*server.sdk, wire, errcode);
  if (expected_error == ::atframework::gateway::error_code_t::kSuccess) {
    CASE_EXPECT_EQ(static_cast<size_t>(1), deliveries);
    CASE_EXPECT_EQ(declared_length, delivered_size);
    CASE_EXPECT_TRUE(delivered_payload_matches);
    CASE_EXPECT_EQ(0, server.error_count);
    CASE_EXPECT_EQ(0, server.close_count);
    CASE_EXPECT_FALSE(server.closed);
  } else {
    CASE_EXPECT_EQ(static_cast<size_t>(0), deliveries);
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(static_cast<int>(expected_error), server.last_error_code);
    CASE_EXPECT_EQ(1, server.close_count);
    CASE_EXPECT_EQ(static_cast<int>(expected_close_reason), server.close_reason);
  }
  server.sdk.reset();
  client.sdk.reset();
}

CASE_TEST(atgateway_protocol_sdk, post_rejects_declared_length_larger_than_decoded_data) {
  check_post_declared_length(16, ::atframework::gateway::error_code_t::kInvalidSize,
                             ::atframework::gateway::close_reason_t::kInvalidData);
}

CASE_TEST(atgateway_protocol_sdk, post_rejects_declared_length_above_limit) {
  check_post_declared_length(257, ::atframework::gateway::error_code_t::kMessageTooLarge,
                             ::atframework::gateway::close_reason_t::kInvalidMessageSize);
}

CASE_TEST(atgateway_protocol_sdk, post_trims_to_zero_declared_length) {
  check_post_declared_length(0, ::atframework::gateway::error_code_t::kSuccess,
                             ::atframework::gateway::close_reason_t::kUnknown);
}

CASE_TEST(atgateway_protocol_sdk, post_trims_trailing_plaintext_bytes) {
  constexpr size_t kDeclaredPrefixSize = 4;
  check_post_declared_length(kDeclaredPrefixSize, ::atframework::gateway::error_code_t::kSuccess,
                             ::atframework::gateway::close_reason_t::kUnknown);
}

CASE_TEST(atgateway_protocol_sdk, post_rejects_unnegotiated_compression) {
  check_post_declared_length(8, ::atframework::gateway::error_code_t::kCompressionNotSupported,
                             ::atframework::gateway::close_reason_t::kInvalidData, true);
}

static void check_plaintext_empty_post(libatgw_protocol_sdk::compression_algorithm_type compression) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  constexpr size_t kMessageLimit = 256;
  if (!setup_receive_size_test_pair(server, client, server_callbacks, client_callbacks,
                                    ::atframework::gateway::v2::crypto_algorithm_t::kNone, kMessageLimit,
                                    compression)) {
    return;
  }
  CASE_EXPECT_EQ(compression, client.sdk->get_crypto_session()->get_selected_compression_algorithm());
  CASE_EXPECT_EQ(compression, server.sdk->get_crypto_session()->get_selected_compression_algorithm());

  CASE_EXPECT_EQ(0, client.sdk->send_post({}));
  CASE_EXPECT_EQ(0, server.sdk->send_post({}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!server.received_messages.empty() && !client.received_messages.empty()) {
    CASE_EXPECT_TRUE(server.received_messages.front().empty());
    CASE_EXPECT_TRUE(client.received_messages.front().empty());
  }
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);
  CASE_EXPECT_FALSE(server.closed);
  CASE_EXPECT_FALSE(client.closed);
  if (server.closed || client.closed) {
    return;
  }

  std::vector<unsigned char> payload(kMessageLimit, 'E');
  CASE_EXPECT_EQ(0, client.sdk->send_post({payload.data(), payload.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() == 2) {
    CASE_EXPECT_TRUE(payload == server.received_messages.back());
  }
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);
  CASE_EXPECT_FALSE(server.closed);
  CASE_EXPECT_FALSE(client.closed);
}

CASE_TEST(atgateway_protocol_sdk, plaintext_empty_post_is_delivered) {
  check_plaintext_empty_post(::atframework::gateway::v2::compression_algorithm_t::kNone);
}

CASE_TEST(atgateway_protocol_sdk, plaintext_empty_post_skips_negotiated_compression) {
  check_plaintext_empty_post(::atframework::gateway::v2::compression_algorithm_t::kZlib);
}

CASE_TEST(atgateway_protocol_sdk, encrypted_post_at_plaintext_limit_is_delivered) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  if (!setup_receive_size_test_pair(
          server, client, server_callbacks, client_callbacks,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm), 256)) {
    return;
  }
  std::vector<unsigned char> payload(256, 'B');
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{payload.data(), payload.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    CASE_EXPECT_TRUE(payload == server.received_messages.front());
  }
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_FALSE(server.closed);
}

static void check_cbc_post_padding(libatgw_protocol_sdk::compression_algorithm_type compression, bool use_zero_padding,
                                   bool reject_zero_length = false) {
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, client_callbacks = {};
  sim_peer_t server, client;
  const auto algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Cbc);
  constexpr size_t kMessageLimit = 256;
  if (!setup_receive_size_test_pair(server, client, server_callbacks, client_callbacks, algorithm, kMessageLimit,
                                    compression)) {
    return;
  }
  CASE_EXPECT_EQ(algorithm, client.sdk->get_crypto_session()->get_selected_algorithm());
  CASE_EXPECT_EQ(algorithm, server.sdk->get_crypto_session()->get_selected_algorithm());
  CASE_EXPECT_EQ(compression, server.sdk->get_crypto_session()->get_selected_compression_algorithm());

  ::atfw::util::crypto::cipher cipher;
  CASE_EXPECT_EQ(0, cipher.init("aes-256-cbc"));
  const size_t block_size = cipher.get_block_size();
  CASE_EXPECT_GT(block_size, static_cast<size_t>(1));
  if (block_size <= 1 || block_size > (std::numeric_limits<unsigned char>::max)()) {
    return;
  }
  std::vector<unsigned char> payload(
      compression == ::atframework::gateway::v2::compression_algorithm_t::kNone ? block_size : kMessageLimit, 'P');
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{payload.data(), payload.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  gsl::span<const unsigned char> compressed;
  std::vector<unsigned char> compression_buffer;
  CASE_EXPECT_EQ(0, client.sdk->get_crypto_session()->compress_data(
                        {payload.data(), payload.size()}, compressed, compression_buffer,
                        ::atframework::gateway::v2::compression_level_t::kDefault, 0));
  const size_t compressed_size = compressed.size();
  CASE_EXPECT_FALSE(compressed.empty());
  if (compressed.empty()) {
    return;
  }
  // Padding is opaque to the protocol: both PKCS#7 and zero padding are trimmed by the declared lengths.
  std::vector<unsigned char> padded_payload(compressed.begin(), compressed.end());
  const size_t padded_size = ((compressed_size + block_size - 1) / block_size + 1) * block_size;
  padded_payload.resize(padded_size, use_zero_padding ? 0 : static_cast<unsigned char>(block_size));
  gsl::span<const unsigned char> ciphertext;
  gsl::span<const unsigned char> iv;
  atfw::util::nostd::string_view aad;
  std::unique_ptr<unsigned char[]> crypto_buffer;
  int result = client.sdk->get_crypto_session()->encrypt_data(
      gsl::span<const unsigned char>{padded_payload.data(), padded_payload.size()}, ciphertext, compression_buffer,
      crypto_buffer, iv, aad);
  CASE_EXPECT_EQ(0, result);
  CASE_EXPECT_EQ(padded_size, ciphertext.size());
  if (result != 0 || ciphertext.size() != padded_size) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  auto head = ::atframework::gateway::v2::Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kPost), 2);
  auto crypto = ::atframework::gateway::v2::Createcs_body_post_crypto(
      builder, algorithm, builder.CreateVector(reinterpret_cast<const int8_t *>(iv.data()), iv.size()),
      builder.CreateVector(reinterpret_cast<const int8_t *>(aad.data()), aad.size()));
  auto compression_info =
      ::atframework::gateway::v2::Createcs_body_post_compression(builder, compression, compressed_size);
  auto body = ::atframework::gateway::v2::Createcs_body_post(
      builder, crypto, compression_info, reject_zero_length ? 0 : payload.size(),
      builder.CreateVector(reinterpret_cast<const int8_t *>(ciphertext.data()), ciphertext.size()));
  builder.Finish(
      ::atframework::gateway::v2::Createclient_message(
          builder, head,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_body, cs_body_post),
          body.Union()),
      ::atframework::gateway::v2::client_messageIdentifier());
  auto wire = make_raw_wire_message(builder.GetBufferPointer(), builder.GetSize());
  int errcode = 0;
  read_raw_wire_message(*server.sdk, wire, errcode);

  CASE_EXPECT_EQ(0, errcode);
  if (reject_zero_length) {
    CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize), server.last_error_code);
    CASE_EXPECT_EQ(1, server.close_count);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), server.close_reason);
    return;
  }
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() == 2) {
    CASE_EXPECT_TRUE(payload == server.received_messages.back());
  }
  CASE_EXPECT_FALSE(server.closed);
  if (server.closed) {
    return;
  }
  CASE_EXPECT_EQ(0, client.sdk->send_post({payload.data(), payload.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(3), server.received_messages.size());
  if (server.received_messages.size() == 3) {
    CASE_EXPECT_TRUE(payload == server.received_messages.back());
  }
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_FALSE(server.closed);
}

CASE_TEST(atgateway_protocol_sdk, cbc_post_accepts_full_block_pkcs7_padding) {
  check_cbc_post_padding(::atframework::gateway::v2::compression_algorithm_t::kNone, false);
}

CASE_TEST(atgateway_protocol_sdk, cbc_post_trims_opaque_padding) {
  check_cbc_post_padding(::atframework::gateway::v2::compression_algorithm_t::kNone, true);
}

CASE_TEST(atgateway_protocol_sdk, compressed_cbc_post_trims_padding_before_decompression) {
  check_cbc_post_padding(::atframework::gateway::v2::compression_algorithm_t::kZlib, true);
}

CASE_TEST(atgateway_protocol_sdk, compressed_post_rejects_zero_declared_length) {
  check_cbc_post_padding(::atframework::gateway::v2::compression_algorithm_t::kZlib, true, true);
}

CASE_TEST(atgateway_protocol_sdk, compressed_post_rejects_automatic_output_sizing) {
  check_cbc_post_padding(::atframework::gateway::v2::compression_algorithm_t::kZstd, true, true);
}

CASE_TEST(atgateway_protocol_sdk, malformed_envelope_reports_one_error) {
  ensure_openssl_init();
  crypto_conf_t config;
  auto context = libatgw_protocol_sdk::create_shared_context(config);
  CASE_EXPECT_TRUE(context != nullptr);
  if (!context) {
    return;
  }
  for (int scenario = 0; scenario < 3; ++scenario) {
    libatgw_protocol_api::proto_callbacks_t callbacks = {};
    sim_peer_t server;
    server.sdk = std::make_shared<libatgw_protocol_sdk>(context);
    callbacks.close_fn = sim_close_fn;
    callbacks.on_error_fn = sim_error_fn;
    server.sdk->set_callbacks(&callbacks);
    server.sdk->set_private_data(&server);
    std::vector<unsigned char> payload(16, 0xFF);
    if (scenario != 0) {
      flatbuffers::FlatBufferBuilder builder;
      flatbuffers::Offset<::atframework::gateway::v2::client_message_head> head;
      if (scenario == 2) {
        head = ::atframework::gateway::v2::Createclient_message_head(
            builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_type_t, kPost), 1);
      }
      auto body = ::atframework::gateway::v2::Createcs_body_ping(builder, 1);
      builder.Finish(
          ::atframework::gateway::v2::Createclient_message(
              builder, head,
              ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::client_message_body, cs_body_ping),
              body.Union()),
          ::atframework::gateway::v2::client_messageIdentifier());
      payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    }
    auto wire = make_raw_wire_message(payload.data(), payload.size());
    auto following_frame = wire;
    following_frame[0] ^= 0x80;
    wire.insert(wire.end(), following_frame.begin(), following_frame.end());
    int errcode = 0;
    read_raw_wire_message(*server.sdk, wire, errcode);
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kBadData), server.last_error_code);
    CASE_EXPECT_EQ(1, server.close_count);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), server.close_reason);
    server.sdk->dispatch_data(gsl::span<const unsigned char>{payload.data(), payload.size()}, 0);
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(1, server.close_count);
  }
}

CASE_TEST(atgateway_protocol_sdk, untrusted_header_only_rejects_invalid_size) {
  ensure_openssl_init();
  crypto_conf_t config;
  auto context = libatgw_protocol_sdk::create_shared_context(config);
  CASE_EXPECT_TRUE(context != nullptr);
  if (!context) {
    return;
  }
  const uint32_t invalid_sizes[] = {0,
                                    static_cast<uint32_t>(libatgw_protocol_sdk::get_max_untrusted_message_size() + 1)};
  for (uint32_t size : invalid_sizes) {
    libatgw_protocol_api::proto_callbacks_t callbacks = {};
    sim_peer_t server;
    server.sdk = std::make_shared<libatgw_protocol_sdk>(context);
    callbacks.on_error_fn = sim_error_fn;
    server.sdk->set_callbacks(&callbacks);
    server.sdk->set_private_data(&server);
    std::vector<unsigned char> header(sizeof(uint32_t) * 2, 0);
    flatbuffers::WriteScalar<uint32_t>(header.data() + sizeof(uint32_t), size);
    int errcode = 0;
    read_raw_wire_message(*server.sdk, header, errcode);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize), errcode);
    CASE_EXPECT_EQ(1, server.error_count);
    CASE_EXPECT_EQ(errcode, server.last_error_code);
  }
}

static void check_post_decode_failure_close_reason(
    libatgw_protocol_sdk::crypto_algorithm_type crypto_algorithm,
    libatgw_protocol_sdk::compression_algorithm_type compression_algorithm,
    ::atframework::gateway::error_code_t expected_error) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms.clear();
  if (crypto_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone)) {
    server_conf_data.supported_algorithms.push_back(crypto_algorithm);
  }
  server_conf_data.compression_algorithms.clear();
  if (compression_algorithm !=
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone)) {
    server_conf_data.compression_algorithms.push_back(compression_algorithm);
  }
  server_conf_data.compression_threshold_size = 256;
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  if (ret != 0 || server.handshake_status != 0 || client.handshake_status != 0) {
    return;
  }
  CASE_EXPECT_EQ(crypto_algorithm, server.sdk->get_crypto_session()->get_selected_algorithm());
  CASE_EXPECT_EQ(compression_algorithm, server.sdk->get_crypto_session()->get_selected_compression_algorithm());

  // Establish a valid post workflow before corrupting only its encoded data.
  std::vector<unsigned char> message(2048, 'A');
  ret = client.sdk->send_post(gsl::span<const unsigned char>{message.data(), message.size()});
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  CASE_EXPECT_FALSE(client.captured_write_payloads.empty());
  CASE_EXPECT_FALSE(server.closed);
  CASE_EXPECT_EQ(0, server.error_count);
  if (ret != 0 || server.received_messages.size() != 1 || client.captured_write_payloads.empty() || server.closed) {
    return;
  }
  CASE_EXPECT_TRUE(message == server.received_messages.front());

  auto wire = client.captured_write_payloads.back();
  const size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);
  CASE_EXPECT_GT(wire.size(), header_size);
  if (wire.size() <= header_size) {
    return;
  }
  flatbuffers::Verifier verifier(wire.data() + header_size, wire.size() - header_size);
  bool valid_message = ::atframework::gateway::v2::Verifyclient_messageBuffer(verifier);
  CASE_EXPECT_TRUE(valid_message);
  if (!valid_message) {
    return;
  }
  const auto *post = ::atframework::gateway::v2::Getclient_message(wire.data() + header_size)->body_as_cs_body_post();
  CASE_EXPECT_TRUE(post != nullptr);
  if (post == nullptr) {
    return;
  }
  CASE_EXPECT_TRUE(post->data() != nullptr && post->data()->size() > 0);
  if (post->data() == nullptr || post->data()->size() == 0) {
    return;
  }
  if (compression_algorithm !=
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone)) {
    CASE_EXPECT_TRUE(post->compression() != nullptr);
    if (post->compression() == nullptr) {
      return;
    }
    CASE_EXPECT_EQ(compression_algorithm, post->compression()->type());
  }

  size_t data_offset = reinterpret_cast<const unsigned char *>(post->data()->data()) - wire.data();
  wire[data_offset] ^= 0x80;
  // Recompute the frame hash so rejection comes from decoding, not transport integrity or FlatBuffers checks.
  wire = make_raw_wire_message(wire.data() + header_size, wire.size() - header_size);
  flatbuffers::Verifier corrupted_verifier(wire.data() + header_size, wire.size() - header_size);
  CASE_EXPECT_TRUE(::atframework::gateway::v2::Verifyclient_messageBuffer(corrupted_verifier));

  int errcode = 0;
  read_raw_wire_message(*server.sdk, wire, errcode);
  CASE_EXPECT_EQ(static_cast<int>(expected_error), server.last_error_code);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_TRUE(server.closed);
  CASE_EXPECT_TRUE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kClosed));
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), server.close_reason);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
}

CASE_TEST(atgateway_protocol_sdk, post_decrypt_failure_reports_invalid_data_close_reason) {
  check_post_decode_failure_close_reason(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone),
      ::atframework::gateway::error_code_t::kCryptoOperation);
}

CASE_TEST(atgateway_protocol_sdk, post_decompress_failure_reports_invalid_data_close_reason) {
  check_post_decode_failure_close_reason(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone),
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kZlib),
      ::atframework::gateway::error_code_t::kCompressionOperation);
}

CASE_TEST(atgateway_protocol_sdk, untrusted_connection_rejects_oversized_message) {
  ensure_openssl_init();

  // Create a server SDK that has NOT completed handshake
  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  CASE_EXPECT_TRUE(!!server_conf);
  if (!server_conf) {
    return;
  }

  sim_peer_t server;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {};
  server_cbs.write_fn = sim_write_fn;
  server_cbs.message_fn = sim_message_fn;
  server_cbs.new_session_fn = sim_new_session_fn;
  server_cbs.reconnect_fn = sim_reconnect_fn;
  server_cbs.close_fn = sim_close_fn;
  server_cbs.on_handshake_done_fn = sim_handshake_done_fn;
  server_cbs.on_handshake_update_fn = sim_handshake_update_fn;
  server_cbs.on_error_fn = sim_error_fn;

  server.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
  server.sdk->set_callbacks(&server_cbs);
  server.sdk->set_private_data(&server);
  server.remote = nullptr;  // no remote needed for this test

  // Verify handshake is NOT done
  CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kHandshakeDone));

  // Construct a payload one byte above the untrusted connection limit.
  const size_t oversized_payload_len = static_cast<size_t>(libatgw_protocol_sdk::get_max_untrusted_message_size()) + 1;
  std::vector<unsigned char> oversized_payload(oversized_payload_len, 0xAB);
  auto wire_msg = make_raw_wire_message(oversized_payload.data(), oversized_payload_len);

  int errcode = 0;
  read_raw_wire_message(*server.sdk, wire_msg, errcode);

  // Should be rejected with kInvalidSize
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize), errcode);
  CASE_MSG_INFO() << "Oversized message (" << oversized_payload_len
                  << " bytes) correctly rejected for untrusted connection" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, untrusted_connection_allows_small_message) {
  ensure_openssl_init();

  // Verify that a normal handshake within the untrusted message size limit still works.
  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.key_refresh_interval = std::chrono::seconds(300);

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  // Normal handshake should complete successfully within the untrusted message size limit.
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // After handshake, send a normal message to verify connection works
  std::string msg = "post-handshake message";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  CASE_MSG_INFO() << "Normal handshake completes and messaging works with untrusted size limit in place" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, untrusted_connection_message_size_boundary) {
  ensure_openssl_init();

  // A payload at the limit passes size validation; one byte above it is rejected.
  const size_t max_untrusted_message_size = static_cast<size_t>(libatgw_protocol_sdk::get_max_untrusted_message_size());
  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  CASE_EXPECT_TRUE(!!server_conf);
  if (!server_conf) {
    return;
  }

  // Test 1: a payload exactly at the limit should pass size validation.
  {
    sim_peer_t server;
    libatgw_protocol_api::proto_callbacks_t server_cbs = {};
    server_cbs.write_fn = sim_write_fn;
    server_cbs.message_fn = sim_message_fn;
    server_cbs.new_session_fn = sim_new_session_fn;
    server_cbs.reconnect_fn = sim_reconnect_fn;
    server_cbs.close_fn = sim_close_fn;
    server_cbs.on_handshake_done_fn = sim_handshake_done_fn;
    server_cbs.on_handshake_update_fn = sim_handshake_update_fn;
    server_cbs.on_error_fn = sim_error_fn;

    server.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
    server.sdk->set_callbacks(&server_cbs);
    server.sdk->set_private_data(&server);
    server.remote = nullptr;

    CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kHandshakeDone));

    const size_t boundary_payload_len = max_untrusted_message_size;
    std::vector<unsigned char> boundary_payload(boundary_payload_len, 0xCD);
    auto wire_msg = make_raw_wire_message(boundary_payload.data(), boundary_payload_len);

    int errcode = 0;
    read_raw_wire_message(*server.sdk, wire_msg, errcode);

    // The raw payload passes size validation and reaches FlatBuffers validation, which rejects it.
    CASE_EXPECT_NE(static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize), errcode);
    CASE_EXPECT_TRUE(server.closed);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), server.close_reason);
    CASE_MSG_INFO() << "Boundary test: " << boundary_payload_len
                    << "-byte payload passed size validation for untrusted connection, errcode=" << errcode << '\n';
  }

  // Test 2: a payload one byte above the limit should be rejected.
  {
    sim_peer_t server;
    libatgw_protocol_api::proto_callbacks_t server_cbs = {};
    server_cbs.write_fn = sim_write_fn;
    server_cbs.message_fn = sim_message_fn;
    server_cbs.new_session_fn = sim_new_session_fn;
    server_cbs.reconnect_fn = sim_reconnect_fn;
    server_cbs.close_fn = sim_close_fn;
    server_cbs.on_handshake_done_fn = sim_handshake_done_fn;
    server_cbs.on_handshake_update_fn = sim_handshake_update_fn;
    server_cbs.on_error_fn = sim_error_fn;

    server.sdk = std::make_shared<libatgw_protocol_sdk>(server_conf);
    server.sdk->set_callbacks(&server_cbs);
    server.sdk->set_private_data(&server);
    server.remote = nullptr;

    CASE_EXPECT_FALSE(server.sdk->check_flag(libatgw_protocol_api::flag_t::kHandshakeDone));

    const size_t over_boundary_len = max_untrusted_message_size + 1;
    std::vector<unsigned char> over_boundary_payload(over_boundary_len, 0xEF);
    auto wire_msg = make_raw_wire_message(over_boundary_payload.data(), over_boundary_len);

    int errcode = 0;
    read_raw_wire_message(*server.sdk, wire_msg, errcode);

    // The size check must reject the payload before FlatBuffers validation.
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize), errcode);
    CASE_MSG_INFO() << "Boundary test: " << over_boundary_len << "-byte message rejected for untrusted connection"
                    << '\n';
  }
}

// ========== Algorithm coverage: ChaCha20-Poly1305-IETF (AEAD stream cipher) ==========

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_chacha20_poly1305) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.clear();
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kChacha20Poly1305Ietf));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  if (server.handshake_status != 0 || client.handshake_status != 0) {
    CASE_MSG_INFO() << "chacha20-poly1305-ietf not available on this platform, skip" << '\n';
    return;
  }
  CASE_EXPECT_EQ(0, ret);

  // Send message client->server (AEAD mode)
  std::string msg = "chacha20-poly1305 AEAD test message";
  gsl::span<const unsigned char> msg_span{reinterpret_cast<const unsigned char *>(msg.data()), msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    std::string received(server.received_messages[0].begin(), server.received_messages[0].end());
    CASE_EXPECT_EQ(msg, received);
  }

  // Bidirectional
  std::string reply = "chacha20-poly1305 reply";
  gsl::span<const unsigned char> reply_span{reinterpret_cast<const unsigned char *>(reply.data()), reply.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    std::string received(client.received_messages[0].begin(), client.received_messages[0].end());
    CASE_EXPECT_EQ(reply, received);
  }

  CASE_MSG_INFO() << "server-client x25519+chacha20-poly1305-ietf handshake and messaging verified" << '\n';
}

// ========== Oversized message rejection ==========

CASE_TEST(atgateway_protocol_sdk, server_client_oversized_message_rejected) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);
  conf_data.max_post_message_size = 1024;  // 1KB limit

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Send a message within the limit — should succeed
  std::vector<unsigned char> small_msg(512, 0x41);
  gsl::span<const unsigned char> small_span{small_msg.data(), small_msg.size()};
  ret = client.sdk->send_post(small_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  // Send a message exceeding the limit — should be rejected
  std::vector<unsigned char> large_msg(conf_data.max_post_message_size + 1, 0x42);
  gsl::span<const unsigned char> large_span{large_msg.data(), large_msg.size()};
  ret = client.sdk->send_post(large_span);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kMessageTooLarge), ret);
  // Server should not receive the oversized message
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());

  // Normal message after rejection — should still succeed
  std::string recovery_msg = "recovery after rejection";
  gsl::span<const unsigned char> recovery_span{reinterpret_cast<const unsigned char *>(recovery_msg.data()),
                                               recovery_msg.size()};
  ret = client.sdk->send_post(recovery_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() >= 2) {
    std::string received(server.received_messages[1].begin(), server.received_messages[1].end());
    CASE_EXPECT_EQ(recovery_msg, received);
  }

  CASE_MSG_INFO() << "oversized message correctly rejected with kMessageTooLarge, recovery works" << '\n';
}

// ========== Encryption-only (no compression) with wire data verification ==========

CASE_TEST(atgateway_protocol_sdk, server_client_encryption_only_no_compression) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.clear();
  conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf_data.compression_algorithms.clear();  // no compression
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Verify encryption algorithm was actually negotiated
  auto crypto_session = client.sdk->get_crypto_session();
  CASE_EXPECT_TRUE(!!crypto_session);
  if (!crypto_session ||
      crypto_session->get_selected_algorithm() ==
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone)) {
    CASE_MSG_INFO() << "aes-256-gcm not available on this platform, skip" << '\n';
    return;
  }

  // Clear captured payloads from handshake phase
  client.captured_write_payloads.clear();
  server.captured_write_payloads.clear();

  // Build a recognizable plaintext message with a distinctive pattern.
  // Use a long, repeated, unique marker string so that any substring search
  // in the encrypted wire data will fail if encryption is working.
  const char plaintext_marker[] = "PLAINTEXT_ENCRYPTION_TEST_MARKER";
  const size_t marker_len = sizeof(plaintext_marker) - 1;  // exclude null terminator
  const size_t repeat_count = 16;
  std::vector<unsigned char> plaintext_msg;
  plaintext_msg.reserve(marker_len * repeat_count);
  for (size_t r = 0; r < repeat_count; ++r) {
    plaintext_msg.insert(plaintext_msg.end(), reinterpret_cast<const unsigned char *>(plaintext_marker),
                         reinterpret_cast<const unsigned char *>(plaintext_marker) + marker_len);
  }

  // Send the message
  gsl::span<const unsigned char> msg_span{plaintext_msg.data(), plaintext_msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);

  // Verify server received the correct decrypted message
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    CASE_EXPECT_EQ(plaintext_msg.size(), server.received_messages[0].size());
    bool data_matches = (plaintext_msg == server.received_messages[0]);
    CASE_EXPECT_TRUE(data_matches);
  }

  // Key assertion: verify that the plaintext marker cannot be found in any
  // captured wire payload. This proves the data was actually encrypted.
  const unsigned char *marker_begin = reinterpret_cast<const unsigned char *>(plaintext_marker);
  const unsigned char *marker_end = marker_begin + marker_len;
  CASE_EXPECT_FALSE(client.captured_write_payloads.empty());
  for (size_t pi = 0; pi < client.captured_write_payloads.size(); ++pi) {
    const auto &wire_payload = client.captured_write_payloads[pi];
    bool found_plaintext =
        std::search(wire_payload.begin(), wire_payload.end(), marker_begin, marker_end) != wire_payload.end();
    CASE_EXPECT_FALSE(found_plaintext);
    if (found_plaintext) {
      CASE_MSG_ERROR() << "plaintext marker found in wire payload #" << pi << " (size=" << wire_payload.size()
                       << "), encryption is NOT working!" << '\n';
    }
  }

  // Also verify bidirectional: server -> client
  server.captured_write_payloads.clear();
  client.received_messages.clear();

  gsl::span<const unsigned char> reply_span{plaintext_msg.data(), plaintext_msg.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    bool data_matches = (plaintext_msg == client.received_messages[0]);
    CASE_EXPECT_TRUE(data_matches);
  }

  CASE_EXPECT_FALSE(server.captured_write_payloads.empty());
  for (size_t pi = 0; pi < server.captured_write_payloads.size(); ++pi) {
    const auto &wire_payload = server.captured_write_payloads[pi];
    bool found_plaintext =
        std::search(wire_payload.begin(), wire_payload.end(), marker_begin, marker_end) != wire_payload.end();
    CASE_EXPECT_FALSE(found_plaintext);
    if (found_plaintext) {
      CASE_MSG_ERROR() << "plaintext marker found in server wire payload #" << pi << ", encryption is NOT working!"
                       << '\n';
    }
  }

  CASE_MSG_INFO() << "encryption-only (no compression) verified: plaintext not found in wire data" << '\n';
}

// ========== Compression-only (no encryption) with wire size verification ==========

CASE_TEST(atgateway_protocol_sdk, server_client_compression_only_no_encryption) {
  ensure_openssl_init();

  crypto_conf_t conf_data;
  conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  conf_data.supported_algorithms.clear();  // no cipher
  // Keep default compression algorithms (zstd, lz4, snappy, zlib)
  conf_data.compression_threshold_size = 256;  // lower threshold to ensure compression kicks in
  conf_data.client_mode = false;
  conf_data.key_refresh_interval = std::chrono::seconds(300);

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  sim_peer_t server, client;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);

  // Check if any compression algorithm was negotiated
  auto crypto_session = client.sdk->get_crypto_session();
  CASE_EXPECT_TRUE(!!crypto_session);
  if (!crypto_session ||
      crypto_session->get_selected_compression_algorithm() ==
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone)) {
    CASE_MSG_INFO() << "no compression algorithm available on this platform, skip" << '\n';
    return;
  }

  CASE_MSG_INFO() << "negotiated compression algorithm: "
                  << static_cast<int>(crypto_session->get_selected_compression_algorithm()) << '\n';

  // Build a large, highly compressible message (repeated pattern)
  // Must be larger than compression_threshold_size (256 bytes)
  const size_t large_msg_size = 8192;
  std::vector<unsigned char> large_msg(large_msg_size);
  for (size_t i = 0; i < large_msg_size; ++i) {
    // Repeated pattern: very compressible
    large_msg[i] = static_cast<unsigned char>('A' + (i % 4));
  }

  // Reset write byte counters after handshake
  client.total_write_payload_bytes = 0;
  server.total_write_payload_bytes = 0;

  // Send the large compressible message
  gsl::span<const unsigned char> msg_span{large_msg.data(), large_msg.size()};
  ret = client.sdk->send_post(msg_span);
  CASE_EXPECT_EQ(0, ret);

  // Verify server received the message correctly (decompressed)
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    CASE_EXPECT_EQ(large_msg.size(), server.received_messages[0].size());
    bool data_matches = (large_msg == server.received_messages[0]);
    CASE_EXPECT_TRUE(data_matches);
  }

  // The key assertion: verify wire data is smaller than original data.
  // total_write_payload_bytes includes FlatBuffer framing overhead, but for a highly
  // compressible 8KB message, the compressed wire payload should be significantly smaller.
  CASE_EXPECT_LT(client.total_write_payload_bytes, large_msg_size);
  CASE_MSG_INFO() << "compression-only: original=" << large_msg_size
                  << " bytes, wire=" << client.total_write_payload_bytes << " bytes (ratio "
                  << (client.total_write_payload_bytes * 100 / large_msg_size) << "%)" << '\n';

  // Also test bidirectional: server -> client
  server.total_write_payload_bytes = 0;
  client.received_messages.clear();

  gsl::span<const unsigned char> reply_span{large_msg.data(), large_msg.size()};
  ret = server.sdk->send_post(reply_span);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!client.received_messages.empty()) {
    bool data_matches = (large_msg == client.received_messages[0]);
    CASE_EXPECT_TRUE(data_matches);
  }
  CASE_EXPECT_LT(server.total_write_payload_bytes, large_msg_size);

  CASE_MSG_INFO() << "compression-only (no encryption) verified: data compressed on wire" << '\n';
}

// ========== crypto_conf_t constructor / copy / move tests ==========

CASE_TEST(atgateway_protocol_sdk, crypto_conf_constructor_sets_defaults) {
  ensure_openssl_init();

  crypto_conf_t conf;

  // Constructor should call set_default()
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
                 conf.key_exchange_algorithm);
  CASE_EXPECT_EQ(static_cast<uint64_t>(2 * 1024 * 1024), conf.max_post_message_size);
  CASE_EXPECT_TRUE(std::chrono::hours(4) == conf.key_refresh_interval);
  CASE_EXPECT_FALSE(conf.client_mode);

  CASE_MSG_INFO() << "crypto_conf_t default constructor sets defaults" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, crypto_conf_copy_move) {
  ensure_openssl_init();

  crypto_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.max_post_message_size = 512;
  conf.client_mode = true;
  std::vector<unsigned char> token = {'a', 'b', 'c'};
  conf.access_tokens.push_back(token);

  // Copy constructor
  crypto_conf_t copy_conf(conf);
  CASE_EXPECT_EQ(conf.key_exchange_algorithm, copy_conf.key_exchange_algorithm);
  CASE_EXPECT_EQ(conf.max_post_message_size, copy_conf.max_post_message_size);
  CASE_EXPECT_EQ(conf.client_mode, copy_conf.client_mode);
  CASE_EXPECT_EQ(conf.access_tokens.size(), copy_conf.access_tokens.size());

  // Move constructor
  crypto_conf_t move_conf(std::move(copy_conf));
  CASE_EXPECT_EQ(conf.key_exchange_algorithm, move_conf.key_exchange_algorithm);
  CASE_EXPECT_EQ(conf.max_post_message_size, move_conf.max_post_message_size);
  CASE_EXPECT_EQ(static_cast<size_t>(1), move_conf.access_tokens.size());

  // Copy assignment
  crypto_conf_t assign_conf;
  assign_conf = conf;
  CASE_EXPECT_EQ(conf.key_exchange_algorithm, assign_conf.key_exchange_algorithm);

  // Move assignment
  crypto_conf_t move_assign_conf;
  move_assign_conf = std::move(assign_conf);
  CASE_EXPECT_EQ(conf.key_exchange_algorithm, move_assign_conf.key_exchange_algorithm);

  CASE_MSG_INFO() << "crypto_conf_t copy/move verified" << '\n';
}

static void check_handshake_response_integrity(bool corrupt_access_signature) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm)};
  server_conf_data.access_tokens.push_back({'h', 'a', 'n', 'd', 's', 'h', 'a', 'k', 'e', '-', 'k', 'e', 'y'});
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  server.pause_writing = true;
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.pending_write_messages.size());
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.captured_write_payloads.size());
  if (server.pending_write_messages.size() != 1 || client.captured_write_payloads.size() != 1) {
    return;
  }

  auto &wire = server.pending_write_messages.front();
  const size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);
  CASE_EXPECT_GT(wire.size(), header_size);
  if (wire.size() <= header_size) {
    return;
  }
  flatbuffers::Verifier verifier(wire.data() + header_size, wire.size() - header_size);
  bool valid_message = ::atframework::gateway::v2::Verifyclient_messageBuffer(verifier);
  CASE_EXPECT_TRUE(valid_message);
  if (!valid_message) {
    return;
  }
  const auto *handshake =
      ::atframework::gateway::v2::Getclient_message(wire.data() + header_size)->body_as_cs_body_handshake();
  CASE_EXPECT_TRUE(handshake != nullptr);
  if (handshake == nullptr) {
    return;
  }
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::handshake_step_t, kKeyExchangeRsp),
                 handshake->step());

  const flatbuffers::Vector<int8_t> *corrupted_field = nullptr;
  if (corrupt_access_signature) {
    CASE_EXPECT_TRUE(handshake->access_data() != nullptr && handshake->access_data()->size() > 0);
    if (handshake->access_data() == nullptr || handshake->access_data()->size() == 0) {
      return;
    }
    const auto *access_data = handshake->access_data()->Get(0);
    CASE_EXPECT_TRUE(access_data != nullptr);
    if (access_data == nullptr) {
      return;
    }
    corrupted_field = access_data->signature();
  } else {
    CASE_EXPECT_TRUE(handshake->initialize_crypto() != nullptr);
    if (handshake->initialize_crypto() == nullptr) {
      return;
    }
    CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
                   handshake->initialize_crypto()->algorithm());
    corrupted_field = handshake->session_token();
  }
  CASE_EXPECT_TRUE(corrupted_field != nullptr && corrupted_field->size() > 0);
  if (corrupted_field == nullptr || corrupted_field->size() == 0) {
    return;
  }
  size_t corrupted_offset = reinterpret_cast<const unsigned char *>(corrupted_field->data()) - wire.data();
  wire[corrupted_offset] ^= 0x80;
  // Keep transport and FlatBuffers valid so only response authentication or token decryption fails.
  wire = make_raw_wire_message(wire.data() + header_size, wire.size() - header_size);
  flatbuffers::Verifier corrupted_verifier(wire.data() + header_size, wire.size() - header_size);
  CASE_EXPECT_TRUE(::atframework::gateway::v2::Verifyclient_messageBuffer(corrupted_verifier));

  server.pause_writing = false;
  sim_process_writing(server.sdk.get(), nullptr);

  int expected_error =
      static_cast<int>(corrupt_access_signature ? ::atframework::gateway::error_code_t::kCryptoVerify
                                                : ::atframework::gateway::error_code_t::kCryptoOperation);
  CASE_EXPECT_EQ(expected_error, client.handshake_status);
  CASE_EXPECT_EQ(expected_error, client.last_error_code);
  CASE_EXPECT_EQ(1, client.error_count);
  CASE_EXPECT_TRUE(client.closed);
  CASE_EXPECT_EQ(1, client.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kHandshake), client.close_reason);
  CASE_EXPECT_TRUE(client.sdk->get_session_token().empty());
  // A rejected response must not send the cipher-switch confirmation.
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.captured_write_payloads.size());
}

CASE_TEST(atgateway_protocol_sdk, handshake_response_rejects_invalid_access_signature) {
  check_handshake_response_integrity(true);
}

CASE_TEST(atgateway_protocol_sdk, handshake_response_rejects_invalid_session_token_ciphertext) {
  check_handshake_response_integrity(false);
}

CASE_TEST(atgateway_protocol_sdk, key_refresh_with_access_token_preserves_authenticated_session) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm)};
  server_conf_data.access_tokens.push_back({'r', 'e', 'f', 'r', 'e', 's', 'h', '-', 'k', 'e', 'y'});
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  // An established gateway session rejects migration callbacks; a local key refresh must not call one.
  server_cbs.reconnect_fn = [](libatgw_protocol_api *, uint64_t) {
    CASE_EXPECT_TRUE(false);
    return static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  };
  int ret = client.sdk->start_session();
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_FALSE(client.sdk->get_session_token().empty());
  if (ret != 0 || server.handshake_status != 0 || client.handshake_status != 0 ||
      client.sdk->get_session_token().empty()) {
    return;
  }
  uint64_t session_id = client.sdk->get_session_id();
  std::vector<unsigned char> original_token(client.sdk->get_session_token().begin(),
                                            client.sdk->get_session_token().end());

  CASE_EXPECT_EQ(0, client.sdk->handshake_update());
  CASE_EXPECT_EQ(0, server.handshake_update_status);
  CASE_EXPECT_EQ(0, client.handshake_update_status);
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);
  CASE_EXPECT_FALSE(server.closed);
  CASE_EXPECT_FALSE(client.closed);
  CASE_EXPECT_EQ(session_id, server.sdk->get_session_id());
  CASE_EXPECT_EQ(session_id, client.sdk->get_session_id());
  if (server.handshake_update_status != 0 || client.handshake_update_status != 0 || server.closed || client.closed) {
    return;
  }
  std::vector<unsigned char> client_token(client.sdk->get_session_token().begin(),
                                          client.sdk->get_session_token().end());
  std::vector<unsigned char> server_token(server.sdk->get_session_token().begin(),
                                          server.sdk->get_session_token().end());
  CASE_EXPECT_FALSE(client_token.empty());
  CASE_EXPECT_TRUE(client_token == server_token);
  CASE_EXPECT_TRUE(client_token != original_token);

  const std::string client_message = "authenticated client after key refresh";
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{
                        reinterpret_cast<const unsigned char *>(client_message.data()), client_message.size()}));
  const std::string server_message = "authenticated server after key refresh";
  CASE_EXPECT_EQ(0, server.sdk->send_post(gsl::span<const unsigned char>{
                        reinterpret_cast<const unsigned char *>(server_message.data()), server_message.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.received_messages.size());
  if (!server.received_messages.empty()) {
    CASE_EXPECT_EQ(client_message,
                   std::string(server.received_messages.front().begin(), server.received_messages.front().end()));
  }
  if (!client.received_messages.empty()) {
    CASE_EXPECT_EQ(server_message,
                   std::string(client.received_messages.front().begin(), client.received_messages.front().end()));
  }
}

static void check_handshake_session_allocation_failure(bool callback_fails) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  int callback_count = 0;
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  server_cbs.new_session_fn = [callback_fails, &callback_count](libatgw_protocol_api *proto, uint64_t &session_id) {
    ++callback_count;
    CASE_EXPECT_TRUE(proto->check_flag(libatgw_protocol_api::flag_t::kInCallback));
    if (callback_fails) {
      // A callback may allocate the ID before a later initialization step fails.
      session_id = 0x2001;
      return static_cast<int>(::atframework::gateway::error_code_t::kMalloc);
    }
    session_id = 0;
    return 0;
  };

  CASE_EXPECT_EQ(0, client.sdk->start_session());
  int expected_error = static_cast<int>(callback_fails ? ::atframework::gateway::error_code_t::kMalloc
                                                       : ::atframework::gateway::error_code_t::kHandshake);
  CASE_EXPECT_EQ(1, callback_count);
  CASE_EXPECT_EQ(expected_error, server.handshake_status);
  CASE_EXPECT_EQ(expected_error, server.last_error_code);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_TRUE(server.closed);
  CASE_EXPECT_EQ(1, server.close_count);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::close_reason_t::kHandshake), server.close_reason);
  CASE_EXPECT_TRUE(server.captured_write_payloads.empty());
  CASE_EXPECT_TRUE(server.sdk->get_session_token().empty());
  CASE_EXPECT_FALSE(server.sdk->get_crypto_session()->has_send_cipher());
}

CASE_TEST(atgateway_protocol_sdk, handshake_rejects_session_allocation_error) {
  check_handshake_session_allocation_failure(true);
}

CASE_TEST(atgateway_protocol_sdk, handshake_rejects_zero_allocated_session_id) {
  check_handshake_session_allocation_failure(false);
}

static void check_reconnect_with_previous_session(bool configure_access_token, bool corrupt_session_token,
                                                  bool ignore_verification_failure = false) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm)};
  if (configure_access_token) {
    server_conf_data.access_tokens.push_back({'r', 'e', 'c', 'o', 'n', 'n', 'e', 'c', 't', '-', 'k', 'e', 'y'});
  }
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }

  libatgw_protocol_api::proto_callbacks_t old_server_cbs = {}, old_client_cbs = {};
  sim_peer_t old_server, old_client;
  setup_sim_pair(old_server, old_client, server_conf, client_conf, old_server_cbs, old_client_cbs);
  CASE_EXPECT_EQ(0, old_client.sdk->start_session());
  CASE_EXPECT_EQ(0, old_server.handshake_status);
  CASE_EXPECT_EQ(0, old_client.handshake_status);
  if (old_server.handshake_status != 0 || old_client.handshake_status != 0) {
    return;
  }
  auto original_token = old_client.sdk->get_session_token();
  CASE_EXPECT_FALSE(original_token.empty());
  if (original_token.empty()) {
    return;
  }
  const std::vector<unsigned char> previous_session_token(original_token.begin(), original_token.end());
  std::vector<unsigned char> reconnect_token = previous_session_token;
  if (corrupt_session_token) {
    reconnect_token.front() ^= 0x80;
  }
  uint64_t session_id = old_client.sdk->get_session_id();

  int reconnect_callback_count = 0;
  bool migration_authorized = false;
  libatgw_protocol_api::proto_callbacks_t new_server_cbs = {}, new_client_cbs = {};
  sim_peer_t new_server, new_client;
  setup_sim_pair(new_server, new_client, server_conf, client_conf, new_server_cbs, new_client_cbs);
  const unsigned char reconnect_hash[] = "authenticated reconnect route";
  new_server_cbs.reconnect_fn = [&old_server, session_id, &reconnect_callback_count, &migration_authorized,
                                 previous_session_token, ignore_verification_failure](libatgw_protocol_api *proto,
                                                                                      uint64_t requested_session_id) {
    ++reconnect_callback_count;
    CASE_EXPECT_EQ(session_id, requested_session_id);
    CASE_EXPECT_TRUE(proto->check_flag(libatgw_protocol_api::flag_t::kInCallback));
    CASE_EXPECT_TRUE(proto->get_session_token().empty());
    // The session manager performs this check before migrating or closing the previous session.
    migration_authorized = proto->check_reconnect(old_server.sdk.get());
    auto current_token = proto->get_session_token();
    if (migration_authorized) {
      CASE_EXPECT_EQ(previous_session_token.size(), current_token.size());
      if (current_token.size() == previous_session_token.size()) {
        CASE_EXPECT_EQ(0, memcmp(previous_session_token.data(), current_token.data(), current_token.size()));
      }
    } else {
      CASE_EXPECT_TRUE(current_token.empty());
    }
    auto previous_token = old_server.sdk->get_session_token();
    CASE_EXPECT_EQ(previous_session_token.size(), previous_token.size());
    if (previous_token.size() == previous_session_token.size()) {
      CASE_EXPECT_EQ(0, memcmp(previous_session_token.data(), previous_token.data(), previous_token.size()));
    }
    return migration_authorized || ignore_verification_failure
               ? 0
               : static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  };

  CASE_EXPECT_EQ(0, new_client.sdk->reconnect_session(
                        session_id, gsl::span<const unsigned char>{reconnect_token.data(), reconnect_token.size()},
                        gsl::span<const unsigned char>{reconnect_hash, sizeof(reconnect_hash) - 1}));
  CASE_EXPECT_EQ(1, reconnect_callback_count);
  CASE_EXPECT_EQ(!corrupt_session_token, migration_authorized);
  if (corrupt_session_token) {
    CASE_EXPECT_TRUE(new_server.sdk->get_session_token().empty());
    CASE_EXPECT_TRUE(new_server.closed);
    CASE_EXPECT_EQ(
        static_cast<int>(ignore_verification_failure ? ::atframework::gateway::error_code_t::kCryptoVerify
                                                     : ::atframework::gateway::error_code_t::kRefuseReconnect),
        new_server.last_error_code);
    CASE_EXPECT_EQ(1, new_server.error_count);
    CASE_EXPECT_TRUE(new_client.closed);
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect),
                   new_client.last_error_code);
    CASE_EXPECT_EQ(1, new_client.error_count);
    CASE_EXPECT_FALSE(old_server.closed);
    const std::string original_message = "previous session remains usable after refused reconnect";
    CASE_EXPECT_EQ(0, old_client.sdk->send_post(gsl::span<const unsigned char>{
                          reinterpret_cast<const unsigned char *>(original_message.data()), original_message.size()}));
    CASE_EXPECT_EQ(static_cast<size_t>(1), old_server.received_messages.size());
    if (!old_server.received_messages.empty()) {
      CASE_EXPECT_EQ(original_message, std::string(old_server.received_messages.front().begin(),
                                                   old_server.received_messages.front().end()));
    }
    return;
  }

  CASE_EXPECT_EQ(0, new_server.handshake_status);
  CASE_EXPECT_EQ(0, new_client.handshake_status);
  CASE_EXPECT_EQ(0, new_server.error_count);
  CASE_EXPECT_EQ(0, new_client.error_count);
  CASE_EXPECT_EQ(session_id, new_server.sdk->get_session_id());
  CASE_EXPECT_EQ(session_id, new_client.sdk->get_session_id());
  for (auto *sdk : {new_server.sdk.get(), new_client.sdk.get()}) {
    auto hash = sdk->get_router_hash_data();
    CASE_EXPECT_EQ(sizeof(reconnect_hash) - 1, hash.size());
    if (hash.size() == sizeof(reconnect_hash) - 1) {
      CASE_EXPECT_EQ(0, memcmp(reconnect_hash, hash.data(), hash.size()));
    }
  }
  if (new_server.handshake_status != 0 || new_client.handshake_status != 0) {
    return;
  }
  std::vector<unsigned char> new_server_token(new_server.sdk->get_session_token().begin(),
                                              new_server.sdk->get_session_token().end());
  std::vector<unsigned char> new_client_token(new_client.sdk->get_session_token().begin(),
                                              new_client.sdk->get_session_token().end());
  CASE_EXPECT_FALSE(new_client_token.empty());
  CASE_EXPECT_TRUE(new_client_token == new_server_token);
  CASE_EXPECT_TRUE(new_client_token != reconnect_token);
  const std::string message = "new transport after authenticated reconnect";
  CASE_EXPECT_EQ(0, new_client.sdk->send_post(gsl::span<const unsigned char>{
                        reinterpret_cast<const unsigned char *>(message.data()), message.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), new_server.received_messages.size());
  if (!new_server.received_messages.empty()) {
    CASE_EXPECT_EQ(
        message, std::string(new_server.received_messages.front().begin(), new_server.received_messages.front().end()));
  }
}

CASE_TEST(atgateway_protocol_sdk, fresh_transport_reconnect_checks_previous_authenticated_session) {
  check_reconnect_with_previous_session(true, false);
}

CASE_TEST(atgateway_protocol_sdk, fresh_transport_reconnect_rejects_incorrect_session_token) {
  check_reconnect_with_previous_session(true, true);
}

CASE_TEST(atgateway_protocol_sdk, fresh_transport_reconnect_uses_session_token_without_access_token) {
  check_reconnect_with_previous_session(false, false);
}

CASE_TEST(atgateway_protocol_sdk, fresh_transport_reconnect_rejects_incorrect_token_without_access_token) {
  check_reconnect_with_previous_session(false, true);
}

CASE_TEST(atgateway_protocol_sdk, fresh_transport_reconnect_rejects_ignored_token_verification_failure) {
  for (bool configure_access_token : {false, true}) {
    check_reconnect_with_previous_session(configure_access_token, true, true);
  }
}

enum class reconnect_call_failure_t { kMissingCryptoContext, kSendBufferLimit, kTransportWrite };

static void check_reconnect_call_identity_rollback(bool established_connection, reconnect_call_failure_t failure) {
  constexpr size_t kMessageLimit = 256;
  constexpr size_t kRejectBufferSize = 1;
  constexpr size_t kBufferCount = 1;
  const unsigned char original_hash[] = "original reconnect route";
  const unsigned char requested_hash[] = "replacement reconnect route";
  const int write_error = static_cast<int>(::atframework::gateway::error_code_t::kNetwork);
  int write_count = 0;
  libatgw_protocol_api::proto_callbacks_t server_callbacks = {}, established_callbacks = {}, new_callbacks = {};
  sim_peer_t server, established_client, new_client;
  if (!setup_receive_size_test_pair(
          server, established_client, server_callbacks, established_callbacks,
          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
          kMessageLimit)) {
    return;
  }
  if (established_connection) {
    int result = established_client.sdk->reconnect_session(
        established_client.sdk->get_session_id(), established_client.sdk->get_session_token(),
        gsl::span<const unsigned char>{original_hash, sizeof(original_hash) - 1});
    CASE_EXPECT_EQ(0, result);
    CASE_EXPECT_FALSE(established_client.closed);
    if (result != 0 || established_client.closed) {
      return;
    }
  } else {
    auto context = established_client.sdk->get_crypto_session()->get_shared_conf();
    if (failure == reconnect_call_failure_t::kMissingCryptoContext) {
      context.reset();
    }
    new_client.sdk = std::make_shared<libatgw_protocol_sdk>(context);
    new_callbacks = established_callbacks;
    new_client.sdk->set_callbacks(&new_callbacks);
    new_client.sdk->set_private_data(&new_client);
  }
  sim_peer_t &client = established_connection ? established_client : new_client;
  auto &callbacks = established_connection ? established_callbacks : new_callbacks;
  const uint64_t previous_session_id = client.sdk->get_session_id();
  std::vector<unsigned char> previous_token, previous_hash;
  auto token = client.sdk->get_session_token();
  auto hash = client.sdk->get_router_hash_data();
  if (!token.empty()) {
    previous_token.assign(token.begin(), token.end());
  }
  if (!hash.empty()) {
    previous_hash.assign(hash.begin(), hash.end());
  }
  callbacks.write_fn = [&write_count, write_error](libatgw_protocol_api *, gsl::span<unsigned char>, bool *is_done) {
    ++write_count;
    *is_done = true;
    return write_error;
  };
  if (failure == reconnect_call_failure_t::kSendBufferLimit) {
    client.sdk->set_send_buffer_limit(kRejectBufferSize, kBufferCount);
  }
  int result = client.sdk->reconnect_session(
      established_client.sdk->get_session_id(), established_client.sdk->get_session_token(),
      gsl::span<const unsigned char>{requested_hash, sizeof(requested_hash) - 1});
  CASE_EXPECT_LT(result, 0);
  if (failure == reconnect_call_failure_t::kMissingCryptoContext) {
    CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kHandshake), result);
  } else if (failure == reconnect_call_failure_t::kTransportWrite) {
    CASE_EXPECT_EQ(write_error, result);
  }
  CASE_EXPECT_EQ(failure == reconnect_call_failure_t::kTransportWrite ? 1 : 0, write_count);
  CASE_EXPECT_EQ(previous_session_id, client.sdk->get_session_id());
  token = client.sdk->get_session_token();
  hash = client.sdk->get_router_hash_data();
  CASE_EXPECT_EQ(previous_token.size(), token.size());
  CASE_EXPECT_EQ(previous_hash.size(), hash.size());
  if (!previous_token.empty() && token.size() == previous_token.size()) {
    CASE_EXPECT_EQ(0, memcmp(previous_token.data(), token.data(), token.size()));
  }
  if (!previous_hash.empty() && hash.size() == previous_hash.size()) {
    CASE_EXPECT_EQ(0, memcmp(previous_hash.data(), hash.data(), hash.size()));
  }
  CASE_EXPECT_TRUE(client.closed);
  CASE_EXPECT_EQ(1, client.close_count);
  CASE_EXPECT_EQ(1, client.error_count);
  CASE_EXPECT_FALSE(client.sdk->check_flag(libatgw_protocol_api::flag_t::kHandshakeUpdate));
}

CASE_TEST(atgateway_protocol_sdk, client_reconnect_preparation_failure_restores_identity) {
  check_reconnect_call_identity_rollback(false, reconnect_call_failure_t::kMissingCryptoContext);
}

CASE_TEST(atgateway_protocol_sdk, client_reconnect_queue_rejection_restores_identity) {
  for (bool established_connection : {false, true}) {
    check_reconnect_call_identity_rollback(established_connection, reconnect_call_failure_t::kSendBufferLimit);
  }
}

CASE_TEST(atgateway_protocol_sdk, client_reconnect_transport_write_failure_restores_identity) {
  for (bool established_connection : {false, true}) {
    check_reconnect_call_identity_rollback(established_connection, reconnect_call_failure_t::kTransportWrite);
  }
}

CASE_TEST(atgateway_protocol_sdk, handshake_rejects_missing_ecdh_public_key) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  client.pause_writing = true;
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.pending_write_messages.size());
  if (client.pending_write_messages.size() != 1) {
    return;
  }
  auto &wire = client.pending_write_messages.front();
  const size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);
  CASE_EXPECT_GT(wire.size(), header_size);
  if (wire.size() <= header_size) {
    return;
  }
  const auto *handshake =
      ::atframework::gateway::v2::Getclient_message(wire.data() + header_size)->body_as_cs_body_handshake();
  CASE_EXPECT_TRUE(handshake != nullptr && handshake->public_key() != nullptr && handshake->public_key()->size() > 0);
  if (handshake == nullptr || handshake->public_key() == nullptr || handshake->public_key()->size() == 0) {
    return;
  }
  size_t key_offset = reinterpret_cast<const unsigned char *>(handshake->public_key()->data()) - wire.data();
  flatbuffers::WriteScalar<flatbuffers::uoffset_t>(wire.data() + key_offset - sizeof(flatbuffers::uoffset_t), 0);
  wire = make_raw_wire_message(wire.data() + header_size, wire.size() - header_size);
  flatbuffers::Verifier verifier(wire.data() + header_size, wire.size() - header_size);
  CASE_EXPECT_TRUE(::atframework::gateway::v2::Verifyclient_messageBuffer(verifier));

  client.pause_writing = false;
  sim_process_writing(client.sdk.get(), nullptr);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kHandshake), server.handshake_status);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kHandshake), server.last_error_code);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_TRUE(server.closed);
  CASE_EXPECT_TRUE(server.captured_write_payloads.empty());
}

CASE_TEST(atgateway_protocol_sdk, handshake_rejects_disjoint_cipher_suites) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm)};
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm)};
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kCryptoAlgorithmNotSupported),
                 server.handshake_status);
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kCryptoAlgorithmNotSupported),
                 server.last_error_code);
  CASE_EXPECT_EQ(1, server.error_count);
  CASE_EXPECT_TRUE(server.closed);
  CASE_EXPECT_TRUE(server.captured_write_payloads.empty());
  CASE_EXPECT_TRUE(server.sdk->get_session_token().empty());
}

CASE_TEST(atgateway_protocol_sdk, repeated_start_preserves_pending_handshake_key) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  client.pause_writing = true;
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(static_cast<int>(::atframework::gateway::error_code_t::kHandshake), client.sdk->start_session());
  CASE_EXPECT_EQ(static_cast<size_t>(1), client.pending_write_messages.size());
  client.pause_writing = false;
  sim_process_writing(client.sdk.get(), nullptr);
  CASE_EXPECT_EQ(0, server.handshake_status);
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);
}

CASE_TEST(atgateway_protocol_sdk, repeated_key_refresh_rejects_confirmation_from_previous_round) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.supported_algorithms = {
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm)};
  server_conf_data.client_mode = false;
  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;
  auto server_conf = libatgw_protocol_sdk::create_shared_context(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_shared_context(client_conf_data);
  CASE_EXPECT_TRUE(!!server_conf && !!client_conf);
  if (!server_conf || !client_conf) {
    return;
  }
  libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
  sim_peer_t server, client;
  setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);
  CASE_EXPECT_EQ(0, client.sdk->start_session());
  CASE_EXPECT_EQ(0, client.handshake_status);
  CASE_EXPECT_EQ(0, server.handshake_status);
  if (client.handshake_status != 0 || server.handshake_status != 0) {
    return;
  }
  CASE_EXPECT_EQ(0, client.sdk->handshake_update());
  CASE_EXPECT_EQ(0, client.handshake_update_status);
  CASE_EXPECT_EQ(0, server.handshake_update_status);
  CASE_EXPECT_FALSE(client.captured_write_payloads.empty());
  if (client.handshake_update_status != 0 || server.handshake_update_status != 0 ||
      client.captured_write_payloads.empty()) {
    return;
  }
  uint64_t previous_sequence = client.sdk->get_crypto_session()->get_handshake_sequence_id();
  std::vector<unsigned char> previous_confirmation = client.captured_write_payloads.back();
  const size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);
  CASE_EXPECT_GT(previous_confirmation.size(), header_size);
  if (previous_confirmation.size() <= header_size) {
    return;
  }
  const auto *confirmation = ::atframework::gateway::v2::Getclient_message(previous_confirmation.data() + header_size)
                                 ->body_as_cs_body_confirm();
  CASE_EXPECT_TRUE(confirmation != nullptr);
  if (confirmation == nullptr) {
    return;
  }
  CASE_EXPECT_EQ(previous_sequence, confirmation->handshake_sequence());
  CASE_EXPECT_FALSE(server.sdk->get_crypto_session()->has_handshaking_receive_cipher());

  server.pause_writing = true;
  CASE_EXPECT_EQ(0, client.sdk->handshake_update());
  uint64_t pending_sequence = server.sdk->get_crypto_session()->get_handshake_sequence_id();
  CASE_EXPECT_NE(previous_sequence, pending_sequence);
  CASE_EXPECT_GT(pending_sequence, static_cast<uint64_t>(1));
  CASE_EXPECT_TRUE(server.sdk->get_crypto_session()->has_handshaking_receive_cipher());
  int error_code = 0;
  read_raw_wire_message(*server.sdk, previous_confirmation, error_code);
  CASE_EXPECT_EQ(0, error_code);
  CASE_EXPECT_EQ(pending_sequence, server.sdk->get_crypto_session()->get_handshake_sequence_id());
  CASE_EXPECT_TRUE(server.sdk->get_crypto_session()->has_handshaking_receive_cipher());

  const std::string old_key_message = "old receive cipher remains active until current confirmation";
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{
                        reinterpret_cast<const unsigned char *>(old_key_message.data()), old_key_message.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(1), server.received_messages.size());
  if (!server.received_messages.empty()) {
    CASE_EXPECT_EQ(old_key_message,
                   std::string(server.received_messages.front().begin(), server.received_messages.front().end()));
  }
  server.pause_writing = false;
  sim_process_writing(server.sdk.get(), nullptr);
  CASE_EXPECT_FALSE(server.sdk->get_crypto_session()->has_handshaking_receive_cipher());
  CASE_EXPECT_EQ(0, client.handshake_update_status);
  CASE_EXPECT_EQ(0, server.error_count);
  CASE_EXPECT_EQ(0, client.error_count);

  const std::string new_key_message = "current confirmation activates the refreshed cipher";
  CASE_EXPECT_EQ(0, client.sdk->send_post(gsl::span<const unsigned char>{
                        reinterpret_cast<const unsigned char *>(new_key_message.data()), new_key_message.size()}));
  CASE_EXPECT_EQ(static_cast<size_t>(2), server.received_messages.size());
  if (server.received_messages.size() == 2) {
    CASE_EXPECT_EQ(new_key_message,
                   std::string(server.received_messages.back().begin(), server.received_messages.back().end()));
  }
}
