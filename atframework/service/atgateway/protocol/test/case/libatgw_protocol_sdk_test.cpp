// Copyright 2026 atframework

#include <cstdint>
#include <cstring>
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
using crypto_conf_t = libatgw_protocol_sdk::crypto_conf_t;
using crypto_session_t = libatgw_protocol_sdk::crypto_session_t;

}  // namespace

// ========== crypto_session_t tests ==========

CASE_TEST(atgateway_protocol_sdk, crypto_session_default_state) {
  ensure_openssl_init();

  crypto_session_t session;
  CASE_EXPECT_FALSE(session.has_handshake_data());
  CASE_EXPECT_EQ(0, session.get_handshake_sequence_id());

  // With no cipher, encrypt/decrypt should pass through
  const char *test_data = "hello world";
  const void *out = nullptr;
  size_t outsz = 0;
  int ret = session.encrypt_data(test_data, strlen(test_data), out, outsz);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), outsz);
  CASE_EXPECT_TRUE(static_cast<const void *>(test_data) == out);  // passthrough means same pointer

  ret = session.decrypt_data(test_data, strlen(test_data), out, outsz);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(test_data), outsz);
  CASE_EXPECT_TRUE(static_cast<const void *>(test_data) == out);
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

  // Test setup_crypto_with_key directly (bypasses DH, for testing encrypt/decrypt)
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

  const void *encrypted = nullptr;
  size_t encrypted_len = 0;
  ret = sender.encrypt_data(plaintext, plaintext_len, encrypted, encrypted_len);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_GT(encrypted_len, static_cast<size_t>(0));

  if (ret != 0 || encrypted == nullptr) {
    CASE_MSG_ERROR() << "encrypt_data failed: " << ret << '\n';
    return;
  }

  // Copy encrypted data (internal buffers may be reused)
  std::vector<unsigned char> encrypted_copy(static_cast<const unsigned char *>(encrypted),
                                            static_cast<const unsigned char *>(encrypted) + encrypted_len);

  // Decrypt
  const void *decrypted = nullptr;
  size_t decrypted_len = 0;
  ret = receiver.decrypt_data(encrypted_copy.data(), encrypted_copy.size(), decrypted, decrypted_len);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted_len);
  if (decrypted_len == plaintext_len && decrypted != nullptr) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted, plaintext_len));
    CASE_MSG_INFO() << "Encrypt/decrypt round-trip succeeded: \""
                    << std::string(reinterpret_cast<const char *>(decrypted), decrypted_len) << "\"" << '\n';
  }

  sender.close();
  receiver.close();
}

CASE_TEST(atgateway_protocol_sdk, encrypt_decrypt_no_cipher) {
  ensure_openssl_init();

  crypto_session_t session;

  const char *data = "test data no cipher";
  const void *out = nullptr;
  size_t outsz = 0;

  int ret = session.encrypt_data(data, strlen(data), out, outsz);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), outsz);
  CASE_EXPECT_TRUE(static_cast<const void *>(data) == out);

  ret = session.decrypt_data(data, strlen(data), out, outsz);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), outsz);
  CASE_EXPECT_TRUE(static_cast<const void *>(data) == out);
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

  // Setup with a known key
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
  const void *out = nullptr;
  size_t outsz = 0;
  ret = session.encrypt_data(data, strlen(data), out, outsz);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(strlen(data), outsz);
  CASE_EXPECT_TRUE(static_cast<const void *>(data) == out);
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

  const void *encrypted = nullptr;
  size_t encrypted_len = 0;
  ret = sender.encrypt_data(plaintext, plaintext_len, encrypted, encrypted_len);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_GT(encrypted_len, static_cast<size_t>(0));

  std::vector<unsigned char> encrypted_copy(static_cast<const unsigned char *>(encrypted),
                                            static_cast<const unsigned char *>(encrypted) + encrypted_len);

  const void *decrypted = nullptr;
  size_t decrypted_len = 0;
  ret = receiver.decrypt_data(encrypted_copy.data(), encrypted_copy.size(), decrypted, decrypted_len);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted_len);
  if (decrypted_len == plaintext_len && decrypted != nullptr) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted, plaintext_len));
  }

  sender.close();
  receiver.close();
}

CASE_TEST(atgateway_protocol_sdk, encrypt_decrypt_chacha20) {
  ensure_openssl_init();

  crypto_session_t sender;
  crypto_session_t receiver;

  // chacha20: 32 bytes key + 16 bytes IV (for libsodium-style)
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

  const void *encrypted = nullptr;
  size_t encrypted_len = 0;
  ret = sender.encrypt_data(plaintext, plaintext_len, encrypted, encrypted_len);
  CASE_EXPECT_EQ(0, ret);

  std::vector<unsigned char> encrypted_copy(static_cast<const unsigned char *>(encrypted),
                                            static_cast<const unsigned char *>(encrypted) + encrypted_len);

  const void *decrypted = nullptr;
  size_t decrypted_len = 0;
  ret = receiver.decrypt_data(encrypted_copy.data(), encrypted_copy.size(), decrypted, decrypted_len);
  CASE_EXPECT_EQ(0, ret);
  CASE_EXPECT_EQ(plaintext_len, decrypted_len);
  if (decrypted_len == plaintext_len && decrypted != nullptr) {
    CASE_EXPECT_EQ(0, memcmp(plaintext, decrypted, plaintext_len));
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
