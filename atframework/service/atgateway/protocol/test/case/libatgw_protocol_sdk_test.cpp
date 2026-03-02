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
using crypt_conf_t = libatgw_protocol_sdk::crypt_conf_t;
using crypt_session_t = libatgw_protocol_sdk::crypt_session_t;

}  // namespace

// ========== crypt_session_t tests ==========

CASE_TEST(atgateway_protocol_sdk, crypt_session_default_state) {
  ensure_openssl_init();

  crypt_session_t session;
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

CASE_TEST(atgateway_protocol_sdk, global_reload) {
  ensure_openssl_init();

  crypt_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = false;

  // Add an access token
  std::vector<unsigned char> token = {'t', 'e', 's', 't', '-', 't', 'o', 'k', 'e', 'n'};
  conf.access_tokens.push_back(token);
  conf.update_interval = 300;

  int ret = libatgw_protocol_sdk::global_reload(conf);
  CASE_EXPECT_EQ(0, ret);

  CASE_MSG_INFO() << "global_reload with secp256r1 + aes-256-gcm succeeded" << '\n';
}

CASE_TEST(atgateway_protocol_sdk, setup_crypto_with_key_and_roundtrip) {
  ensure_openssl_init();

  // Test setup_crypto_with_key directly (bypasses DH, for testing encrypt/decrypt)
  crypt_session_t sender;
  crypt_session_t receiver;

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

  crypt_session_t session;

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
  crypt_conf_t conf;
  conf.key_exchange_algorithm =
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::key_exchange_t, kSecp256r1);
  conf.supported_algorithms.push_back(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kAes256Gcm));
  conf.client_mode = false;

  std::vector<unsigned char> token = {'m', 'y', '-', 'a', 'c', 'c', 'e', 's', 's', '-', 't', 'o', 'k', 'e', 'n'};
  conf.access_tokens.push_back(token);
  conf.update_interval = 300;

  int ret = libatgw_protocol_sdk::global_reload(conf);
  CASE_EXPECT_EQ(0, ret);

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

CASE_TEST(atgateway_protocol_sdk, crypt_session_close) {
  ensure_openssl_init();

  crypt_session_t session;

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
