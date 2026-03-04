// Copyright 2026 atframework

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
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
  bool closed;
  int close_reason;
  uint64_t new_session_id_counter;

  sim_peer_t()
      : remote(nullptr),
        handshake_status(-1),
        handshake_update_status(-1),
        error_count(0),
        closed(false),
        close_reason(0),
        new_session_id_counter(0x1000) {}
};

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

  // Deliver to remote peer using alloc_receive_buffer + read protocol
  auto &remote_sdk = self->remote->sdk;
  size_t remaining = payload_len;
  const unsigned char *src = payload;

  while (remaining > 0) {
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

  // Signal write done
  if (nullptr != is_done) {
    *is_done = true;
  }
  proto->write_done(0);
  return 0;
}

static int sim_message_fn(libatgw_protocol_api *proto, gsl::span<const unsigned char> buffer) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr == self) {
    return 0;
  }
  self->received_messages.emplace_back(buffer.data(), buffer.data() + buffer.size());
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

static int sim_close_fn(libatgw_protocol_api *proto, int reason) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    self->closed = true;
    self->close_reason = reason;
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

static int sim_error_fn(libatgw_protocol_api *proto, const char * /*filename*/, uint32_t /*line*/, int /*errcode*/,
                        const char * /*errmsg*/) {
  auto *self = reinterpret_cast<sim_peer_t *>(proto->get_private_data());
  if (nullptr != self) {
    ++self->error_count;
  }
  return 0;
}

/// @brief Setup a server-client pair with callbacks wired for loopback simulation.
static void setup_sim_pair(sim_peer_t &server, sim_peer_t &client,
                           std::shared_ptr<::atframework::gateway::v2::detail::crypto_global_configure_t> server_conf,
                           std::shared_ptr<::atframework::gateway::v2::detail::crypto_global_configure_t> client_conf,
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
  gsl::span<unsigned char> out;
  int ret = session.encrypt_data(in, out);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), out.size());

  ret = session.decrypt_data(in, out);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), out.size());
}

CASE_TEST(atgateway_protocol_sdk, create_global_configure) {
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
  conf.update_interval = 300;
  conf.max_post_message_size = 4 * 1024 * 1024;

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  CASE_MSG_INFO() << "create_global_configure with secp256r1 + aes-256-gcm succeeded" << '\n';
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
  int ret = sender.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);
  if (ret != 0) {
    CASE_MSG_ERROR() << "sender setup_crypto_with_key failed: " << ret << '\n';
    return;
  }

  ret = receiver.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);
  if (ret != 0) {
    CASE_MSG_ERROR() << "receiver setup_crypto_with_key failed: " << ret << '\n';
    return;
  }

  // Encrypt
  const char *plaintext = "Hello, atgateway v2 protocol!";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<unsigned char> encrypted;
  ret = sender.encrypt_data(in, encrypted);
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
  gsl::span<unsigned char> decrypted;
  ret = receiver.decrypt_data(enc_in, decrypted);
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
  gsl::span<unsigned char> out;

  int ret = session.encrypt_data(in, out);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), out.size());

  ret = session.decrypt_data(in, out);
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
  conf.update_interval = 300;

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  // Test make_access_data_plaintext
  std::vector<unsigned char> pubkey = {0x01, 0x02, 0x03, 0x04};
  std::string plaintext = libatgw_protocol_sdk::make_access_data_plaintext(
      12345, 100, 200, 99, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
      gsl::span<const unsigned char>{pubkey.data(), pubkey.size()});

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
  int ret = session.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);

  session.close();
  CASE_EXPECT_FALSE(session.has_handshake_data());
  CASE_EXPECT_EQ(static_cast<uint64_t>(0), session.get_handshake_sequence_id());

  // After close, encrypt/decrypt should passthrough again
  const char *data = "post-close data";
  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(data), strlen(data)};
  gsl::span<unsigned char> out;
  ret = session.encrypt_data(in, out);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), out.size());
}

CASE_TEST(atgateway_protocol_sdk, create_global_configure_with_multiple_algorithms) {
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

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  CASE_MSG_INFO() << "create_global_configure with x25519 + multiple algorithms succeeded" << '\n';
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

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
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
  int ret = sender.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);
  ret = receiver.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);

  const char *plaintext = "AES-128-GCM round-trip test";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<unsigned char> encrypted;
  ret = sender.encrypt_data(in, encrypted);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_GT(encrypted.size(), static_cast<size_t>(0));

  std::vector<unsigned char> encrypted_copy(encrypted.data(), encrypted.data() + encrypted.size());

  gsl::span<const unsigned char> enc_in{encrypted_copy.data(), encrypted_copy.size()};
  gsl::span<unsigned char> decrypted;
  ret = receiver.decrypt_data(enc_in, decrypted);
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
  int ret = sender.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  if (ret != 0) {
    CASE_MSG_INFO() << "chacha20 not available on this platform, skip" << '\n';
    return;
  }

  ret = receiver.setup_crypto_with_key(alg, key, sizeof(key), iv, sizeof(iv));
  CASE_EXPECT_EQ(0, ret);

  const char *plaintext = "ChaCha20 round-trip test";
  size_t plaintext_len = strlen(plaintext);

  gsl::span<const unsigned char> in{reinterpret_cast<const unsigned char *>(plaintext), plaintext_len};
  gsl::span<unsigned char> encrypted;
  ret = sender.encrypt_data(in, encrypted);
  CASE_EXPECT_EQ(0, ret);

  std::vector<unsigned char> encrypted_copy(encrypted.data(), encrypted.data() + encrypted.size());

  gsl::span<const unsigned char> enc_in{encrypted_copy.data(), encrypted_copy.size()};
  gsl::span<unsigned char> decrypted;
  ret = receiver.decrypt_data(enc_in, decrypted);
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

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
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
  std::string plaintext = libatgw_protocol_sdk::make_access_data_plaintext(
      42, 1000, 2000, 77, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
      gsl::span<const unsigned char>{pubkey.data(), pubkey.size()});

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

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
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
  conf.update_interval = 1;
  conf.client_mode = true;

  // Call set_default
  conf.set_default();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519),
                 conf.key_exchange_algorithm);
  CASE_EXPECT_EQ(static_cast<uint64_t>(2 * 1024 * 1024), conf.max_post_message_size);
  CASE_EXPECT_EQ(static_cast<time_t>(300), conf.update_interval);
  CASE_EXPECT_FALSE(conf.client_mode);
  CASE_EXPECT_TRUE(conf.supported_algorithms.empty());
  CASE_EXPECT_TRUE(conf.compression_algorithms.empty());
  CASE_EXPECT_TRUE(conf.access_tokens.empty());

  CASE_MSG_INFO() << "crypto_conf_t::set_default verified" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, default_conf_creates_global_configure) {
  ensure_openssl_init();

  // Verify default config (with x25519 key exchange, no cipher) can create global configure
  crypto_conf_t conf;
  conf.set_default();

  auto global_conf = libatgw_protocol_sdk::create_global_configure(conf);
  CASE_EXPECT_TRUE(!!global_conf);

  if (global_conf) {
    auto *mutable_conf = libatgw_protocol_sdk::get_global_configure_mutable_conf(global_conf);
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
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("x25519"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("secp256r1"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("P-256"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kNone),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name("invalid"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kNone),
                 libatgw_protocol_sdk::key_exchange_algorithm_from_name(nullptr));

  const char *name = libatgw_protocol_sdk::key_exchange_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519));
  CASE_EXPECT_TRUE(nullptr != name);
  if (name != nullptr) {
    CASE_EXPECT_EQ(std::string("x25519"), std::string(name));
  }

  const auto &names = libatgw_protocol_sdk::get_all_key_exchange_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "key exchange algorithm count: " << names.size() << '\n';
}

CASE_TEST(atgateway_protocol_sdk, crypto_algorithm_name_conversion) {
  ensure_openssl_init();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("aes-256-gcm"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kXxtea),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("XXTEA"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone),
                 libatgw_protocol_sdk::crypto_algorithm_from_name("invalid"));

  const char *name = libatgw_protocol_sdk::crypto_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  CASE_EXPECT_TRUE(nullptr != name);
  if (name != nullptr) {
    CASE_EXPECT_EQ(std::string("aes-256-gcm"), std::string(name));
  }

  const auto &names = libatgw_protocol_sdk::get_all_crypto_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "crypto algorithm count: " << names.size() << '\n';
}

CASE_TEST(atgateway_protocol_sdk, compression_algorithm_name_conversion) {
  ensure_openssl_init();

  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kZstd),
                 libatgw_protocol_sdk::compression_algorithm_from_name("zstd"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kLz4),
                 libatgw_protocol_sdk::compression_algorithm_from_name("LZ4"));
  CASE_EXPECT_EQ(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone),
                 libatgw_protocol_sdk::compression_algorithm_from_name("invalid"));

  const char *name = libatgw_protocol_sdk::compression_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kZstd));
  CASE_EXPECT_TRUE(nullptr != name);
  if (name != nullptr) {
    CASE_EXPECT_EQ(std::string("zstd"), std::string(name));
  }

  const auto &names = libatgw_protocol_sdk::get_all_compression_algorithm_names();
  CASE_EXPECT_GT(names.size(), static_cast<size_t>(0));
  CASE_MSG_INFO() << "compression algorithm count: " << names.size() << '\n';
}

// ========== Server-Client simulation tests ==========

CASE_TEST(atgateway_protocol_sdk, server_client_handshake_x25519_aes256gcm) {
  ensure_openssl_init();

  crypto_conf_t server_conf_data;
  server_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  server_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  server_conf_data.client_mode = false;
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.update_interval = 300;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.update_interval = 300;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.client_mode = false;
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.client_mode = true;
  client_conf_data.update_interval = 300;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data = server_conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  conf_data.update_interval = 300;

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  conf_data.update_interval = 300;

  crypto_conf_t client_conf_data = conf_data;
  client_conf_data.client_mode = true;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes128Gcm));
  client_conf_data.client_mode = true;
  client_conf_data.update_interval = 300;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
  server_conf_data.update_interval = 300;

  crypto_conf_t client_conf_data;
  client_conf_data.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kX25519);
  client_conf_data.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  client_conf_data.access_tokens.push_back(token);
  client_conf_data.client_mode = true;
  client_conf_data.update_interval = 300;

  auto server_conf = libatgw_protocol_sdk::create_global_configure(server_conf_data);
  auto client_conf = libatgw_protocol_sdk::create_global_configure(client_conf_data);
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
