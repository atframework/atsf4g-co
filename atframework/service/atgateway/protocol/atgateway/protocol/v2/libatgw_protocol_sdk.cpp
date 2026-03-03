// Copyright 2026 atframework
// Refactored: 2-message ECDH handshake (like libatbus node_register_req/rsp)

#include "atgateway/protocol/v2/libatgw_protocol_sdk.h"

#include <gsl/select-gsl.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "algorithm/compression.h"
#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "algorithm/crypto_hmac.h"
#include "algorithm/murmur_hash.h"
#include "algorithm/sha.h"
#include "common/string_oprs.h"
#include "lock/lock_holder.h"
#include "lock/seq_alloc.h"
#include "lock/spin_lock.h"
#include "random/random_generator.h"
#include "string/string_format.h"
#include "time/time_utility.h"

namespace atframework {
namespace gateway {
namespace v2 {
namespace detail {
namespace {

static uint64_t alloc_seq() {
  static atfw::util::lock::seq_alloc_u64 seq_alloc;
  uint64_t ret = seq_alloc.inc();
  while (0 == ret) {
    ret = seq_alloc.inc();
  }
  return ret;
}

/// Map key_exchange_t to "ecdh:<curve>" string for shared_context::init()
static const char *map_key_exchange_to_dh_param(ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                                            key_exchange_t) ke) {
  using namespace ::atframework::gateway::v2;
  switch (ke) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kX25519):
      return "ecdh:x25519";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp256r1):
      return "ecdh:secp256r1";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp384r1):
      return "ecdh:secp384r1";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp521r1):
      return "ecdh:secp521r1";
    default:
      return nullptr;
  }
}

/// Map flatbuffers crypto_algorithm_t to cipher name string
static const char *map_crypto_algorithm_to_name(ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                                            crypto_algorithm_t) alg) {
  using namespace ::atframework::gateway::v2;
  switch (alg) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXxtea):
      return "xxtea";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Cbc):
      return "aes-128-cbc";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Cbc):
      return "aes-192-cbc";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Cbc):
      return "aes-256-cbc";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Gcm):
      return "aes-128-gcm";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Gcm):
      return "aes-192-gcm";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Gcm):
      return "aes-256-gcm";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20):
      return "chacha20";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20Poly1305Ietf):
      return "chacha20-poly1305-ietf";
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXchacha20Poly1305Ietf):
      return "xchacha20-poly1305-ietf";
    default:
      return "";
  }
}

/// Map flatbuffers compression_algorithm_t to atfw::util::compression::algorithm_t
static ::atfw::util::compression::algorithm_t map_compression_algorithm(
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, compression_algorithm_t) alg) {
  using namespace ::atframework::gateway::v2;
  switch (alg) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZstd):
      return ::atfw::util::compression::algorithm_t::kZstd;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kLz4):
      return ::atfw::util::compression::algorithm_t::kLz4;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kSnappy):
      return ::atfw::util::compression::algorithm_t::kSnappy;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZlib):
      return ::atfw::util::compression::algorithm_t::kZlib;
    default:
      return ::atfw::util::compression::algorithm_t::kNone;
  }
}

}  // namespace

struct crypto_global_configure_t {
  using ptr_t = std::shared_ptr<crypto_global_configure_t>;

  crypto_global_configure_t(libatgw_protocol_sdk::crypto_conf_t conf) : conf_(std::move(conf)), inited_(false) {
    shared_dh_context_ = atfw::util::crypto::dh::shared_context::create();
  }
  ~crypto_global_configure_t() { close(); }

  int init() {
    int ret = 0;
    close();

    using namespace ::atframework::gateway::v2;

    // Initialize DH shared context based on key exchange algorithm
    if (conf_.key_exchange_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kNone)) {
      const char *dh_param = map_key_exchange_to_dh_param(conf_.key_exchange_algorithm);
      if (nullptr == dh_param) {
        return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
      }

      ret = shared_dh_context_->init(dh_param);
      if (0 != ret) {
        return static_cast<int>(::atframework::gateway::error_code_t::kCryptInitDhparam);
      }
    }

    // Validate and collect supported algorithms
    std::unordered_set<std::string> all_supported_cipher_set;
    const std::vector<std::string> &all_supported_ciphers = atfw::util::crypto::cipher::get_all_cipher_names();
    for (const auto &supported_type : all_supported_ciphers) {
      std::string lower_name = supported_type;
      std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), atfw::util::string::tolower<char>);
      all_supported_cipher_set.insert(lower_name);
    }

    for (auto alg : conf_.supported_algorithms) {
      const char *name = map_crypto_algorithm_to_name(alg);
      if (nullptr != name && name[0] != '\0') {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), atfw::util::string::tolower<char>);
        if (all_supported_cipher_set.find(lower_name) != all_supported_cipher_set.end()) {
          available_algorithms_.push_back(alg);
        }
      }
    }

    // Validate compression algorithms
    for (auto calg : conf_.compression_algorithms) {
      auto mapped = map_compression_algorithm(calg);
      if (mapped != ::atfw::util::compression::algorithm_t::kNone &&
          ::atfw::util::compression::is_algorithm_supported(mapped)) {
        available_compression_algorithms_.push_back(calg);
      }
    }

    inited_ = true;
    return ret;
  }

  void close() {
    if (!inited_) {
      return;
    }
    inited_ = false;
    available_algorithms_.clear();
    if (shared_dh_context_) {
      shared_dh_context_->reset();
    }
  }

  bool check_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)
                           alg) const {
    using namespace ::atframework::gateway::v2;
    if (alg == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
      return true;
    }
    for (auto a : available_algorithms_) {
      if (a == alg) {
        return true;
      }
    }
    return false;
  }

  static void default_crypto_configure(libatgw_protocol_sdk::crypto_conf_t &dconf) {
    dconf.access_tokens.clear();
    dconf.key_exchange_algorithm =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kX25519);
    dconf.supported_algorithms.clear();
    dconf.compression_algorithms.clear();
    dconf.supported_kdf_algorithms.clear();
    dconf.max_post_message_size = 2 * 1024 * 1024;  // 2MB default
    dconf.update_interval = 300;
    dconf.client_mode = false;
  }

  libatgw_protocol_sdk::crypto_conf_t conf_;
  bool inited_;
  std::vector<ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
      available_algorithms_;
  std::vector<ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, compression_algorithm_t)>
      available_compression_algorithms_;
  atfw::util::crypto::dh::shared_context::ptr_t shared_dh_context_;
};
}  // namespace detail

// ========================= crypto_session_t =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_session_t::crypto_session_t()
    : selected_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)),
      selected_kdf(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256)),
      key_exchange_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kNone)),
      selected_compression_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)),
      max_post_message_size(2 * 1024 * 1024),
      handshake_sequence_id_(0) {}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_session_t::~crypto_session_t() { close(); }

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::handshake_generate_self_key(
    uint64_t peer_sequence_id) {
  if (!shared_conf || !shared_conf->shared_dh_context_) {
    return static_cast<int>(::atframework::gateway::error_code_t::kHandshake);
  }

  // Generate sequence ID
  if (0 == peer_sequence_id) {
    auto now = std::chrono::system_clock::now();
    handshake_sequence_id_ =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
  } else {
    handshake_sequence_id_ = peer_sequence_id;
  }
  handshake_start_time_ = std::chrono::system_clock::now();

  // Create DH context and generate key pair
  handshake_dh_ = std::make_unique<::atfw::util::crypto::dh>();
  int res = handshake_dh_->init(shared_conf->shared_dh_context_);
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // generate params (this generates the key pair internally)
  std::vector<unsigned char> dh_params;
  res = handshake_dh_->make_params(dh_params);
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // extract just the public key bytes
  res = handshake_dh_->make_public(handshake_self_public_key_);
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::handshake_read_peer_key(
    gsl::span<const unsigned char> peer_public_key,
    gsl::span<const ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
        peer_algorithms,
    gsl::span<const ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
        local_algorithms) {
  if (!handshake_dh_) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  // Negotiate algorithm: pick first mutually supported one
  selected_algorithm = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone);
  for (size_t i = 0; i < local_algorithms.size(); ++i) {
    for (size_t j = 0; j < peer_algorithms.size(); ++j) {
      if (local_algorithms[i] == peer_algorithms[j] &&
          local_algorithms[i] != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
        selected_algorithm = local_algorithms[i];
        break;
      }
    }
    if (selected_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
      break;
    }
  }

  // Read peer public key
  int res = handshake_dh_->read_public(peer_public_key.data(), peer_public_key.size());
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // Calculate shared secret
  std::vector<unsigned char> shared_secret;
  res = handshake_dh_->calc_secret(shared_secret);
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // Derive key from shared secret and setup ciphers
  int ret = derive_key_from_shared_secret(shared_secret);

  // Release DH context -- no longer needed after key derivation
  handshake_dh_.reset();

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::handshake_write_self_public_key(
    std::vector<unsigned char> &out_public_key) {
  out_public_key = handshake_self_public_key_;
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::setup_crypto_with_key(
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t) algorithm,
    const unsigned char *key, size_t key_size, const unsigned char *iv, size_t iv_size) {
  selected_algorithm = algorithm;

  if (algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
    send_cipher_.reset();
    receive_cipher_.reset();
    return 0;
  }

  const char *cipher_name = detail::map_crypto_algorithm_to_name(algorithm);
  if (nullptr == cipher_name || cipher_name[0] == '\0') {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  // Setup send cipher
  send_cipher_ = std::make_unique<::atfw::util::crypto::cipher>();
  if (send_cipher_->init(cipher_name) < 0) {
    send_cipher_.reset();
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }
  if (key_size > 0 && send_cipher_->set_key(key, static_cast<uint32_t>(key_size * 8)) < 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }
  if (iv_size > 0 && send_cipher_->set_iv(iv, iv_size) < 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // Setup receive cipher (same key and IV)
  receive_cipher_ = std::make_unique<::atfw::util::crypto::cipher>();
  if (receive_cipher_->init(cipher_name) < 0) {
    receive_cipher_.reset();
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }
  if (key_size > 0 && receive_cipher_->set_key(key, static_cast<uint32_t>(key_size * 8)) < 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }
  if (iv_size > 0 && receive_cipher_->set_iv(iv, iv_size) < 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  return 0;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypto_session_t::close() {
  handshake_dh_.reset();
  send_cipher_.reset();
  receive_cipher_.reset();
  handshake_self_public_key_.clear();
  handshake_sequence_id_ = 0;
}

int libatgw_protocol_sdk::crypto_session_t::derive_key_from_shared_secret(
    const std::vector<unsigned char> &shared_secret) {
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
    return 0;
  }

  const char *cipher_name = detail::map_crypto_algorithm_to_name(selected_algorithm);
  if (nullptr == cipher_name || cipher_name[0] == '\0') {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  // Create a temporary cipher to get key/iv sizes
  ::atfw::util::crypto::cipher temp_cipher;
  if (temp_cipher.init(cipher_name) < 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  size_t key_size = temp_cipher.get_key_bits() / 8;
  size_t iv_size = temp_cipher.get_iv_size();
  size_t needed = key_size + iv_size;

  // Derive key material using HKDF-SHA256
  std::vector<unsigned char> key_material(needed, 0);
  int res = ::atfw::util::crypto::hkdf::derive(::atfw::util::crypto::digest_type_t::kSha256, nullptr, 0,  // salt (none)
                                               shared_secret.data(), shared_secret.size(),  // ikm (shared secret)
                                               nullptr, 0,                                  // info (none)
                                               key_material.data(), needed);                // output
  if (0 != res) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // KEY = key_material[0..key_size-1], IV = key_material[key_size..key_size+iv_size-1]
  return setup_crypto_with_key(selected_algorithm, key_material.data(), key_size,
                               iv_size > 0 ? key_material.data() + key_size : nullptr, iv_size);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::encrypt_data(const void *in, size_t insz,
                                                                              const void *&out, size_t &outsz) {
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone) || !send_cipher_) {
    out = in;
    outsz = insz;
    return 0;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return static_cast<int>(::atframework::gateway::error_code_t::kParam);
  }

  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCrypt);
  size_t len = tls_buffer.size();
  unsigned char *buffer = tls_buffer.data();

  std::unique_ptr<unsigned char[]> unique_buffer;
  if (len < insz + send_cipher_->get_block_size() + send_cipher_->get_iv_size()) {
    len = insz + send_cipher_->get_block_size() + send_cipher_->get_iv_size();
    unique_buffer.reset(new unsigned char[len]);
    buffer = unique_buffer.get();
  }

  int res = 0;
  if (send_cipher_->is_aead()) {
    res = send_cipher_->encrypt_aead(reinterpret_cast<const unsigned char *>(in), insz, buffer, &len, nullptr, 0);
  } else {
    res = send_cipher_->encrypt(reinterpret_cast<const unsigned char *>(in), insz, buffer, &len);
  }
  if (res < 0) {
    out = nullptr;
    outsz = 0;
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  out = buffer;
  outsz = len;
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::decrypt_data(const void *in, size_t insz,
                                                                              const void *&out, size_t &outsz) {
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone) || !receive_cipher_) {
    out = in;
    outsz = insz;
    return 0;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return static_cast<int>(::atframework::gateway::error_code_t::kParam);
  }

  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCrypt);
  unsigned char *buffer = tls_buffer.data();
  size_t len = tls_buffer.size();

  std::unique_ptr<unsigned char[]> unique_buffer;
  if (len < insz + receive_cipher_->get_block_size() + receive_cipher_->get_iv_size()) {
    len = insz + receive_cipher_->get_block_size() + receive_cipher_->get_iv_size();
    unique_buffer.reset(new unsigned char[len]);
    buffer = unique_buffer.get();
  }

  int res = 0;
  if (receive_cipher_->is_aead()) {
    res = receive_cipher_->decrypt_aead(reinterpret_cast<const unsigned char *>(in), insz, buffer, &len, nullptr, 0);
  } else {
    res = receive_cipher_->decrypt(reinterpret_cast<const unsigned char *>(in), insz, buffer, &len);
  }
  if (res < 0) {
    out = nullptr;
    outsz = 0;
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  out = buffer;
  outsz = len;
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::compress_data(const void *in, size_t insz,
                                                                               const void *&out, size_t &outsz) {
  if (selected_compression_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    out = in;
    outsz = insz;
    return 0;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return static_cast<int>(::atframework::gateway::error_code_t::kParam);
  }

  auto mapped_alg = detail::map_compression_algorithm(selected_compression_algorithm);
  if (mapped_alg == ::atfw::util::compression::algorithm_t::kNone) {
    out = in;
    outsz = insz;
    return 0;
  }

  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCompress);

  std::vector<unsigned char> compressed;
  int res = ::atfw::util::compression::compress(
      mapped_alg, gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(in), insz}, compressed);
  if (0 != res) {
    out = nullptr;
    outsz = 0;
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  // Copy to TLS buffer if it fits, otherwise use the vector directly
  unsigned char *buffer = tls_buffer.data();
  if (compressed.size() <= tls_buffer.size()) {
    memcpy(buffer, compressed.data(), compressed.size());
    out = buffer;
    outsz = compressed.size();
  } else {
    // Compressed data is larger than TLS buffer - this can happen
    out = compressed.data();
    outsz = compressed.size();
  }
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::decompress_data(const void *in, size_t insz,
                                                                                 const void *&out, size_t &outsz) {
  if (selected_compression_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    out = in;
    outsz = insz;
    return 0;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return static_cast<int>(::atframework::gateway::error_code_t::kParam);
  }

  auto mapped_alg = detail::map_compression_algorithm(selected_compression_algorithm);
  if (mapped_alg == ::atfw::util::compression::algorithm_t::kNone) {
    out = in;
    outsz = insz;
    return 0;
  }

  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCompress);

  std::vector<unsigned char> decompressed;
  int res = ::atfw::util::compression::decompress(
      mapped_alg, gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(in), insz},
      outsz,  // original_size hint
      decompressed);
  if (0 != res) {
    out = nullptr;
    outsz = 0;
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptOperation);
  }

  unsigned char *buffer = tls_buffer.data();
  if (decompressed.size() <= tls_buffer.size()) {
    memcpy(buffer, decompressed.data(), decompressed.size());
    out = buffer;
    outsz = decompressed.size();
  } else {
    out = decompressed.data();
    outsz = decompressed.size();
  }
  return 0;
}

// ========================= libatgw_protocol_sdk =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::libatgw_protocol_sdk(
    std::shared_ptr<detail::crypto_global_configure_t> shared_conf)
    : shared_conf_(std::move(shared_conf)), session_id_(0), read_head_{}, last_write_ptr_(nullptr), close_reason_(0) {
  crypto_session_ = std::make_shared<crypto_session_t>();

  read_head_.len = 0;

  ping_.last_ping = ping_data_t::clk_t::from_time_t(0);
  ping_.last_delta = 0;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::~libatgw_protocol_sdk() {
  close(static_cast<int>(::atframework::gateway::close_reason_t::kUnknown), false);
  close_handshake(static_cast<int>(::atframework::gateway::error_code_t::kSessionExpired));
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::alloc_receive_buffer(size_t /*suggested_size*/, char *&out_buf,
                                                                     size_t &out_len) {
  flag_guard_t flag_guard(flags_, flag_t::kInCallback);

  if (check_flag(flag_t::kClosing)) {
    out_buf = nullptr;
    out_len = 0;
    return;
  }

  void *data = nullptr;
  size_t sread = 0, swrite = 0;
  read_buffers_.back(data, sread, swrite);

  if (nullptr == data || 0 == swrite) {
    out_len = sizeof(read_head_.buffer) - read_head_.len;

    if (0 == out_len) {
      out_buf = nullptr;
      assert(false);
    } else {
      out_buf = reinterpret_cast<char *>(&read_head_.buffer[read_head_.len]);
    }
    return;
  }

  out_buf = reinterpret_cast<char *>(data);
  out_len = swrite;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::read(int /*ssz*/, gsl::span<const unsigned char> buffer, int &errcode) {
  if (check_flag(flag_t::kClosing)) {
    errcode = static_cast<int>(::atframework::gateway::error_code_t::kClosing);
    return;
  }

  errcode = static_cast<int>(::atframework::gateway::error_code_t::kSuccess);
  flag_guard_t flag_guard(flags_, flag_t::kInCallback);

  void *data = nullptr;
  size_t sread = 0, swrite = 0;
  read_buffers_.back(data, sread, swrite);
  bool is_free = false;

  const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  if (nullptr == data || 0 == swrite) {
    assert(buffer.size() <= sizeof(read_head_.buffer) - read_head_.len);
    read_head_.len += buffer.size();

    char *buff_start = reinterpret_cast<char *>(read_head_.buffer);
    size_t buff_left_len = read_head_.len;

    while (buff_left_len > sizeof(uint32_t) + sizeof(uint32_t)) {
      uint32_t msg_len = 0;
      msg_len = flatbuffers::ReadScalar<uint32_t>(buff_start + sizeof(uint32_t));

      if (buff_left_len >= msg_header_len + msg_len) {
        uint32_t check_hash =
            atfw::util::hash::murmur_hash3_x86_32(buff_start + msg_header_len, static_cast<int>(msg_len), 0);
        uint32_t expect_hash = 0;
        memcpy(&expect_hash, buff_start, sizeof(uint32_t));

        if (check_hash != expect_hash) {
          errcode = static_cast<int>(::atframework::gateway::error_code_t::kBadData);
        }

        dispatch_data(
            gsl::span<const unsigned char>{reinterpret_cast<unsigned char *>(buff_start) + msg_header_len, msg_len},
            errcode);

        buff_start += msg_header_len + msg_len;
        buff_left_len -= msg_header_len + msg_len;
      } else {
        if (0 == read_buffers_.push_back(data, msg_header_len + msg_len)) {
          memcpy(data, buff_start, buff_left_len);
          read_buffers_.pop_back(buff_left_len, false);

          buff_start += buff_left_len;
          buff_left_len = 0;
        } else {
          is_free = true;
          buff_start += msg_header_len;
          buff_left_len -= msg_header_len;
          break;
        }
      }
    }

    if (buff_start != reinterpret_cast<char *>(read_head_.buffer) && buff_left_len > 0) {
      memmove(read_head_.buffer, buff_start, buff_left_len);
    }
    read_head_.len = buff_left_len;
  } else {
    read_buffers_.pop_back(buffer.size(), false);
  }

  read_buffers_.front(data, sread, swrite);
  if (nullptr != data && 0 == swrite) {
    data = reinterpret_cast<char *>(data) - sread;

    uint32_t check_hash = atfw::util::hash::murmur_hash3_x86_32(reinterpret_cast<char *>(data) + msg_header_len,
                                                                static_cast<int>(sread - msg_header_len), 0);
    uint32_t expect_hash = 0;
    memcpy(&expect_hash, data, sizeof(uint32_t));
    size_t msg_len = sread - msg_header_len;

    if (check_hash != expect_hash) {
      errcode = static_cast<int>(::atframework::gateway::error_code_t::kBadData);
    }

    dispatch_data(gsl::span<const unsigned char>{reinterpret_cast<unsigned char *>(data) + msg_header_len, msg_len},
                  errcode);
    read_buffers_.pop_front(0, true);
  }

  if (is_free) {
    errcode = static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize);
    if (read_head_.len > 0) {
      dispatch_data(
          gsl::span<const unsigned char>{reinterpret_cast<unsigned char *>(read_head_.buffer), read_head_.len},
          errcode);
    }
  }
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::dispatch_data(gsl::span<const unsigned char> data, int errcode) {
  if (check_flag(flag_t::kClosing)) {
    return;
  }

  if (errcode < 0 || data.empty()) {
    return;
  }

  ::flatbuffers::Verifier cs_msg_verify(reinterpret_cast<const uint8_t *>(data.data()), data.size());
  if (false == atframework::gateway::v2::Verifyclient_messageBuffer(cs_msg_verify)) {
    close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData));
    return;
  }

  const atframework::gateway::v2::client_message *msg = atframework::gateway::v2::Getclient_message(data.data());
  if (nullptr == msg->head()) {
    return;
  }

  switch (msg->head()->type()) {
    case atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost): {
      if (::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_post) !=
          msg->body_type()) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), false);
        break;
      }

      // Server rejects data messages before handshake is done
      if (!check_flag(flag_t::kHandshakeDone) || !crypto_session_) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kHandshake), false);
        break;
      }

      const ::atframework::gateway::v2::cs_body_post *msg_body =
          static_cast<const ::atframework::gateway::v2::cs_body_post *>(msg->body());

      const void *out = nullptr;
      size_t outsz = static_cast<size_t>(msg_body->length());
      int res = decode_post(msg_body->data()->data(), static_cast<size_t>(msg_body->data()->size()), out, outsz);
      if (0 == res) {
        if (nullptr != callbacks_ && callbacks_->message_fn) {
          callbacks_->message_fn(this, gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(out),
                                                                      static_cast<size_t>(msg_body->length())});
        }
      } else {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), false);
      }
      break;
    }
    case atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake): {
      if (::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake) !=
          msg->body_type()) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), false);
        break;
      }
      const ::atframework::gateway::v2::cs_body_handshake *msg_body =
          static_cast<const ::atframework::gateway::v2::cs_body_handshake *>(msg->body());

      dispatch_handshake(*msg_body);
      break;
    }
    case atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPing): {
      // Server rejects ping before handshake
      if (!check_flag(flag_t::kHandshakeDone)) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kHandshake), false);
        break;
      }

      if (::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping) !=
          msg->body_type()) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData));
        break;
      }

      const ::atframework::gateway::v2::cs_body_ping *msg_body =
          static_cast<const ::atframework::gateway::v2::cs_body_ping *>(msg->body());
      send_pong(msg_body->timepoint());
      break;
    }
    case atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPong): {
      if (::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping) !=
          msg->body_type()) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData));
        break;
      }

      if (0 != ping_data_t::clk_t::to_time_t(ping_.last_ping)) {
        ping_.last_delta = static_cast<time_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(ping_data_t::clk_t::now() - ping_.last_ping).count());
      }
      break;
    }
    case atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kKickoff): {
      if (::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_kickoff) !=
          msg->body_type()) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), false);
        break;
      }

      const ::atframework::gateway::v2::cs_body_kickoff *msg_body =
          static_cast<const ::atframework::gateway::v2::cs_body_kickoff *>(msg->body());
      close(msg_body->reason(), false);
      break;
    }
    default: {
      // Unknown message type before handshake: disconnect
      if (!check_flag(flag_t::kHandshakeDone)) {
        close(static_cast<int>(::atframework::gateway::close_reason_t::kInvalidData), false);
      }
      break;
    }
  }
}

// ========================= Handshake (2-message exchange) =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (check_flag(flag_t::kHandshakeDone) && !check_flag(flag_t::kHandshakeUpdate)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kHandshake);
  }

  using namespace atframework::gateway::v2;
  int ret = 0;
  switch (body_handshake.step()) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeReq): {
      ret = dispatch_handshake_key_exchange_req(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeRsp): {
      ret = dispatch_handshake_key_exchange_rsp(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectReq): {
      ret = dispatch_handshake_reconn_req(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectRsp): {
      ret = dispatch_handshake_reconn_rsp(body_handshake);
      break;
    }
    default: {
      ret = static_cast<int>(::atframework::gateway::error_code_t::kBadProtocol);
      break;
    }
  }

  // handshake failed => close connection
  if (ret < 0) {
    close_handshake(ret);
    close(static_cast<int>(::atframework::gateway::close_reason_t::kHandshake), false);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_key_exchange_req(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Server-side: receive kKeyExchangeReq from client =====
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !callbacks_->new_session_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  // Verify access data first (before setup_handshake)
  int ret = verify_access_data(body_handshake);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "key exchange req: access data verification failed");
    return ret;
  }

  std::shared_ptr<detail::crypto_global_configure_t> global_cfg = shared_conf_;
  ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  // Allocate new session ID
  {
    flag_guard_t flag_guard(flags_, flag_t::kInCallback);
    callbacks_->new_session_fn(this, session_id_);
  }

  // Server generates its own ECDH key pair
  uint64_t client_sequence = body_handshake.session_id();
  if (0 == client_sequence) {
    client_sequence = 1;
  }
  ret = crypto_session_->handshake_generate_self_key(client_sequence);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "server: generate ECDH key pair failed");
    return ret;
  }

  // Collect peer's algorithms
  using alg_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t);
  std::vector<alg_type> peer_algorithms;
  if (body_handshake.algorithms() != nullptr) {
    for (flatbuffers::uoffset_t i = 0; i < body_handshake.algorithms()->size(); ++i) {
      peer_algorithms.push_back(body_handshake.algorithms()->Get(i));
    }
  }

  // Get local algorithms
  gsl::span<const alg_type> local_alg_span;
  if (global_cfg) {
    local_alg_span =
        gsl::span<const alg_type>{global_cfg->available_algorithms_.data(), global_cfg->available_algorithms_.size()};
  }

  // Read peer's public key and derive shared secret
  if (body_handshake.public_key() != nullptr && body_handshake.public_key()->size() > 0) {
    ret = crypto_session_->handshake_read_peer_key(
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(body_handshake.public_key()->data()),
                                       body_handshake.public_key()->size()},
        gsl::span<const alg_type>{peer_algorithms.data(), peer_algorithms.size()}, local_alg_span);
    if (ret < 0) {
      ATFRAME_GATEWAY_ON_ERROR(ret, "server: read peer key and derive secret failed");
      return ret;
    }
  }

  // Negotiate compression algorithm
  if (body_handshake.compression_algorithm() != nullptr && global_cfg) {
    for (size_t i = 0; i < global_cfg->available_compression_algorithms_.size(); ++i) {
      for (flatbuffers::uoffset_t j = 0; j < body_handshake.compression_algorithm()->size(); ++j) {
        if (global_cfg->available_compression_algorithms_[i] == body_handshake.compression_algorithm()->Get(j)) {
          crypto_session_->selected_compression_algorithm = global_cfg->available_compression_algorithms_[i];
          goto compression_negotiated;
        }
      }
    }
  }
compression_negotiated:

  // Set max_post_message_size
  crypto_session_->max_post_message_size = global_cfg ? global_cfg->conf_.max_post_message_size : (2 * 1024 * 1024);

  // Build kKeyExchangeRsp
  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), detail::alloc_seq());

  // Write server's public key
  std::vector<unsigned char> self_public_key;
  crypto_session_->handshake_write_self_public_key(self_public_key);

  // Selected algorithm (single item)
  std::vector<crypto_algorithm_t> selected_algs;
  selected_algs.push_back(crypto_session_->selected_algorithm);
  std::vector<kdf_algorithm_t> selected_kdfs;
  selected_kdfs.push_back(crypto_session_->selected_kdf);

  // Selected compression algorithm (single item in response)
  std::vector<compression_algorithm_t> selected_comp_algs;
  if (crypto_session_->selected_compression_algorithm !=
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    selected_comp_algs.push_back(crypto_session_->selected_compression_algorithm);
  }

  // Generate server access data
  std::vector<flatbuffers::Offset<cs_body_handshake_access_data>> access_data_offsets;
  generate_access_data(builder, access_data_offsets, session_id_, crypto_session_->key_exchange_algorithm,
                       gsl::span<const unsigned char>{self_public_key.data(), self_public_key.size()});

  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, session_id_, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeRsp),
      crypto_session_->key_exchange_algorithm, builder.CreateVector(selected_kdfs), builder.CreateVector(selected_algs),
      builder.CreateVector(access_data_offsets),
      builder.CreateVector(reinterpret_cast<const int8_t *>(self_public_key.data()), self_public_key.size()),
      0,                                         // iv_size
      0,                                         // tag_size
      builder.CreateVector(selected_comp_algs),  // compression_algorithm (selected)
      crypto_session_->max_post_message_size);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());

  ret = write_message(builder);
  if (ret >= 0) {
    // Handshake complete on server side
    close_handshake(0);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_key_exchange_rsp(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Client-side: receive kKeyExchangeRsp from server =====
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(static_cast<int>(::atframework::gateway::error_code_t::kHandshake),
                             "key exchange rsp: session_id is 0");
    return static_cast<int>(::atframework::gateway::error_code_t::kHandshake);
  }

  // Verify access data
  int ret = verify_access_data(body_handshake);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "key exchange rsp: access data verification failed");
    return ret;
  }

  session_id_ = body_handshake.session_id();

  // Collect server's selected algorithms
  using alg_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t);
  std::vector<alg_type> server_algorithms;
  if (body_handshake.algorithms() != nullptr) {
    for (flatbuffers::uoffset_t i = 0; i < body_handshake.algorithms()->size(); ++i) {
      server_algorithms.push_back(body_handshake.algorithms()->Get(i));
    }
  }

  // Get local algorithms
  std::shared_ptr<detail::crypto_global_configure_t> global_cfg = shared_conf_;
  gsl::span<const alg_type> local_alg_span;
  if (global_cfg) {
    local_alg_span =
        gsl::span<const alg_type>{global_cfg->available_algorithms_.data(), global_cfg->available_algorithms_.size()};
  }

  // Read server's public key and derive shared secret
  if (body_handshake.public_key() != nullptr && body_handshake.public_key()->size() > 0) {
    ret = crypto_session_->handshake_read_peer_key(
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(body_handshake.public_key()->data()),
                                       body_handshake.public_key()->size()},
        gsl::span<const alg_type>{server_algorithms.data(), server_algorithms.size()}, local_alg_span);
    if (ret < 0) {
      ATFRAME_GATEWAY_ON_ERROR(ret, "client: read server key and derive secret failed");
      return ret;
    }
  }

  // Read negotiated compression algorithm and max message size from server response
  if (body_handshake.compression_algorithm() != nullptr && body_handshake.compression_algorithm()->size() > 0) {
    crypto_session_->selected_compression_algorithm = body_handshake.compression_algorithm()->Get(0);
  }
  if (body_handshake.max_post_message_size() > 0) {
    crypto_session_->max_post_message_size = body_handshake.max_post_message_size();
  }

  // Handshake complete on client side
  close_handshake(0);
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_req(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  int ret = 0;
  if (callbacks_->reconnect_fn) {
    flag_guard_t flag_guard(flags_, flag_t::kInCallback);
    ret = callbacks_->reconnect_fn(this, body_handshake.session_id());
  } else {
    ret = static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  }

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), detail::alloc_seq());

  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, 0 == ret ? session_id_ : 0, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectRsp),
      crypto_session_->key_exchange_algorithm);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());
  ret = write_message(builder);

  if (0 == ret) {
    close_handshake(0);
    // After reconnect, force key rotation
    handshake_update();
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_rsp(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect),
                             "reconnect refused by server");
    return static_cast<int>(::atframework::gateway::error_code_t::kRefuseReconnect);
  }

  session_id_ = body_handshake.session_id();
  close_handshake(0);
  return 0;
}

// ========================= Access Data (HMAC-SHA256 authentication) =========================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::generate_access_data(
    flatbuffers::FlatBufferBuilder &builder,
    std::vector<flatbuffers::Offset<::atframework::gateway::v2::cs_body_handshake_access_data>> &out,
    uint64_t session_id,
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange,
    gsl::span<const unsigned char> public_key) {
  std::shared_ptr<detail::crypto_global_configure_t> global_cfg = shared_conf_;
  if (!global_cfg || global_cfg->conf_.access_tokens.empty()) {
    return 0;  // No access tokens configured, skip authentication
  }

  int64_t timestamp = static_cast<int64_t>(std::time(nullptr));

  // Generate random nonces using the shared DH context random function
  uint64_t nonce1 = 0;
  uint64_t nonce2 = 0;
  if (global_cfg->shared_dh_context_) {
    global_cfg->shared_dh_context_->random(&nonce1, sizeof(nonce1));
    global_cfg->shared_dh_context_->random(&nonce2, sizeof(nonce2));
  } else {
    // Fallback: use timestamp-based nonces (less secure but functional)
    nonce1 = static_cast<uint64_t>(timestamp) ^ 0x5A5A5A5A5A5A5A5AULL;
    auto now = std::chrono::high_resolution_clock::now();
    nonce2 =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
  }

  std::string plaintext = make_access_data_plaintext(session_id, timestamp, nonce1, nonce2, key_exchange, public_key);

  for (const auto &token : global_cfg->conf_.access_tokens) {
    std::string signature =
        calculate_access_data_signature(gsl::span<const unsigned char>{token.data(), token.size()}, plaintext);

    auto sig_offset = builder.CreateVector(reinterpret_cast<const int8_t *>(signature.data()), signature.size());
    auto ad = ::atframework::gateway::v2::Createcs_body_handshake_access_data(
        builder, ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(access_data_algorithm_t, kHmacSha256),
        timestamp, nonce1, nonce2, sig_offset);
    out.push_back(ad);
  }

  return 0;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::verify_access_data(
    const ::atframework::gateway::v2::cs_body_handshake &body_handshake) {
  std::shared_ptr<detail::crypto_global_configure_t> global_cfg = shared_conf_;
  if (!global_cfg || global_cfg->conf_.access_tokens.empty()) {
    return 0;  // No access tokens configured, skip verification
  }

  if (body_handshake.access_data() == nullptr || body_handshake.access_data()->size() == 0) {
    return static_cast<int>(::atframework::gateway::error_code_t::kCryptVerify);
  }

  // For each access_data entry, try to verify against any local token
  for (flatbuffers::uoffset_t i = 0; i < body_handshake.access_data()->size(); ++i) {
    const auto *ad = body_handshake.access_data()->Get(i);
    if (nullptr == ad || nullptr == ad->signature()) {
      continue;
    }

    // Check timestamp window (±300 seconds)
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (std::abs(now - ad->timestamp()) > 300) {
      continue;
    }

    // Reconstruct plaintext
    gsl::span<const unsigned char> pub_key;
    if (body_handshake.public_key() != nullptr && body_handshake.public_key()->size() > 0) {
      pub_key =
          gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(body_handshake.public_key()->data()),
                                         body_handshake.public_key()->size()};
    }

    std::string plaintext = make_access_data_plaintext(body_handshake.session_id(), ad->timestamp(), ad->nonce1(),
                                                       ad->nonce2(), body_handshake.key_exchange(), pub_key);

    // Try each local access token
    for (const auto &token : global_cfg->conf_.access_tokens) {
      std::string expected_sig =
          calculate_access_data_signature(gsl::span<const unsigned char>{token.data(), token.size()}, plaintext);

      if (expected_sig.size() == static_cast<size_t>(ad->signature()->size()) &&
          0 == memcmp(expected_sig.data(), ad->signature()->data(), expected_sig.size())) {
        return 0;  // Verification succeeded
      }
    }
  }

  return static_cast<int>(::atframework::gateway::error_code_t::kCryptVerify);
}

LIBATGW_PROTOCOL_API std::string libatgw_protocol_sdk::make_access_data_plaintext(
    uint64_t session_id, int64_t timestamp, uint64_t nonce1, uint64_t nonce2,
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange,
    gsl::span<const unsigned char> public_key) {
  std::string result = atfw::util::string::format("{}:{}-{}:{}", timestamp, nonce1, nonce2, session_id);

  if (!public_key.empty()) {
    // Include key exchange type and SHA256 of public key
    std::string pub_key_hash = atfw::util::hash::sha::hash_to_hex(atfw::util::hash::sha::EN_ALGORITHM_SHA256,
                                                                  public_key.data(), public_key.size());
    result += atfw::util::string::format(":{}:{}", static_cast<int>(key_exchange), pub_key_hash);
  }

  return result;
}

LIBATGW_PROTOCOL_API std::string libatgw_protocol_sdk::calculate_access_data_signature(
    gsl::span<const unsigned char> access_token, const std::string &plaintext) {
  std::vector<unsigned char> result = ::atfw::util::crypto::hmac::compute_to_binary(
      ::atfw::util::crypto::digest_type_t::kSha256, access_token.data(), access_token.size(),
      reinterpret_cast<const unsigned char *>(plaintext.data()), plaintext.size());

  return std::string(reinterpret_cast<const char *>(result.data()), result.size());
}

// ========================= Setup / Teardown =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::setup_handshake(
    std::shared_ptr<detail::crypto_global_configure_t> &shared_conf) {
  if (!shared_conf) {
    return 0;
  }

  if (crypto_session_->has_handshake_data()) {
    return 0;
  }

  crypto_session_->shared_conf = shared_conf;
  crypto_session_->key_exchange_algorithm = shared_conf->conf_.key_exchange_algorithm;

  // Ready to update handshake
  set_flag(flag_t::kHandshakeUpdate, true);
  return 0;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::close_handshake(int status) {
  if (logger_) {
    FWINSTLOGDEBUG(*logger_, "close_handshake - status: {}", status);
  }

  handshake_done(status);
}

LIBATGW_PROTOCOL_API bool libatgw_protocol_sdk::check_reconnect(const libatgw_protocol_api *other) {
  if (nullptr == other) {
    return false;
  }

  const libatgw_protocol_sdk *other_proto = dynamic_cast<const libatgw_protocol_sdk *>(other);
  if (nullptr == other_proto) {
    return false;
  }

  // Must have same session ID
  if (session_id_ != other_proto->session_id_ || 0 == session_id_) {
    return false;
  }

  return true;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_receive_buffer_limit(size_t max_size, size_t max_number) {
  read_buffers_.set_mode(max_size, max_number);
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_send_buffer_limit(size_t max_size, size_t max_number) {
  write_buffers_.set_mode(max_size, max_number);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::handshake_update() { return start_session(); }

LIBATGW_PROTOCOL_API std::string libatgw_protocol_sdk::get_info() const {
  size_t limit_sz = 0;
  std::string result = atfw::util::string::format(
      "atgateway v2 protocol: session id={}\n"
      "    last ping delta={}\n"
      "    handshake={}\n"
      "    key_exchange={}\n"
      "    selected_algorithm={}\n"
      "    selected_compression={}\n"
      "    max_post_message_size={}\n"
      "    status: writing={},closing={},closed={},handshake done={},handshake update={}\n",
      session_id_, ping_.last_delta,
      (crypto_session_ && crypto_session_->has_handshake_data() ? "running" : "not running"),
      (crypto_session_ ? static_cast<int>(crypto_session_->key_exchange_algorithm) : 0),
      (crypto_session_ ? static_cast<int>(crypto_session_->selected_algorithm) : 0),
      (crypto_session_ ? static_cast<int>(crypto_session_->selected_compression_algorithm) : 0),
      (crypto_session_ ? crypto_session_->max_post_message_size : 0), check_flag(flag_t::kWriting),
      check_flag(flag_t::kClosing), check_flag(flag_t::kClosed), check_flag(flag_t::kHandshakeDone),
      check_flag(flag_t::kHandshakeUpdate));

  if (read_buffers_.limit().limit_size_ > 0) {
    limit_sz = read_buffers_.limit().limit_size_ + sizeof(read_head_.buffer) - read_head_.len -
               read_buffers_.limit().cost_size_;
    result += atfw::util::string::format("    read buffer: used size={}, free size={}\n",
                                         (read_head_.len + read_buffers_.limit().cost_size_), limit_sz);
  } else {
    result += atfw::util::string::format("    read buffer: used size={}, free size=unlimited\n",
                                         (read_head_.len + read_buffers_.limit().cost_size_));
  }

  if (write_buffers_.limit().limit_size_ > 0) {
    limit_sz = write_buffers_.limit().limit_size_ - write_buffers_.limit().cost_size_;
    result += atfw::util::string::format("    write buffer: used size={}, free size={}\n",
                                         write_buffers_.limit().cost_size_, limit_sz);
  } else {
    result += atfw::util::string::format("    write buffer: used size={}, free size=unlimited\n",
                                         write_buffers_.limit().cost_size_);
  }

  return result;
}

// ========================= Client-side session start =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::start_session() {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (0 != session_id_) {
    return static_cast<int>(::atframework::gateway::error_code_t::kSessionAlreadyExist);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  // Setup global config
  std::shared_ptr<detail::crypto_global_configure_t> global_cfg = shared_conf_;
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  // Generate client ECDH key pair
  ret = crypto_session_->handshake_generate_self_key(0);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "client start_session: generate ECDH key failed");
    return ret;
  }

  // Write client's public key
  std::vector<unsigned char> self_public_key;
  crypto_session_->handshake_write_self_public_key(self_public_key);

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), detail::alloc_seq());

  // All supported algorithms
  std::vector<crypto_algorithm_t> algs;
  if (global_cfg) {
    for (auto a : global_cfg->available_algorithms_) {
      algs.push_back(a);
    }
  }
  std::vector<kdf_algorithm_t> kdfs = {ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256)};

  // Compression algorithms
  std::vector<compression_algorithm_t> comp_algs;
  if (global_cfg) {
    for (auto ca : global_cfg->available_compression_algorithms_) {
      comp_algs.push_back(ca);
    }
  }

  // Generate access data
  std::vector<flatbuffers::Offset<cs_body_handshake_access_data>> access_data_offsets;
  generate_access_data(builder, access_data_offsets, 0, crypto_session_->key_exchange_algorithm,
                       gsl::span<const unsigned char>{self_public_key.data(), self_public_key.size()});

  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, 0, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeReq),
      crypto_session_->key_exchange_algorithm, builder.CreateVector(kdfs), builder.CreateVector(algs),
      builder.CreateVector(access_data_offsets),
      builder.CreateVector(reinterpret_cast<const int8_t *>(self_public_key.data()), self_public_key.size()),
      0,                                 // iv_size
      0,                                 // tag_size
      builder.CreateVector(comp_algs));  // compression_algorithm

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::reconnect_session(uint64_t sess_id,
                                                                 const std::vector<unsigned char> & /*secret*/) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), detail::alloc_seq());

  flatbuffers::Offset<cs_body_handshake> handshake_body =
      Createcs_body_handshake(builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectReq),
                              crypto_session_->key_exchange_algorithm);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

// ========================= Data send/receive =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(::atframework::gateway::v2::client_message_type_t msg_type,
                                                         gsl::span<const unsigned char> data) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !crypto_session_) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  // Enforce max_post_message_size
  if (crypto_session_->max_post_message_size > 0 && data.size() > crypto_session_->max_post_message_size) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMsgTooLarge);
  }

  size_t ori_len = data.size();
  const void *out = nullptr;
  size_t outsz = 0;
  int res = encode_post(data.data(), data.size(), out, outsz);
  if (0 != res) {
    return res;
  }

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data =
      Createclient_message_head(builder, msg_type, detail::alloc_seq());

  flatbuffers::Offset<cs_body_post> post_body =
      Createcs_body_post(builder, 0, 0, static_cast<uint64_t>(ori_len),
                         builder.CreateVector(reinterpret_cast<const int8_t *>(out), outsz));

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_post), post_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(gsl::span<const unsigned char> data) {
  return send_post(::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost),
                   data);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_ping() {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  ping_.last_ping = ping_data_t::clk_t::now();

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPing), detail::alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body =
      Createcs_body_ping(builder, static_cast<int64_t>(ping_.last_ping.time_since_epoch().count()));

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping), ping_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_pong(int64_t tp) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPong), detail::alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body = Createcs_body_ping(builder, tp);

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping), ping_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_kickoff(int reason, int sub_reason, const char *message) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atframework::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kKickoff), detail::alloc_seq());

  flatbuffers::Offset<flatbuffers::String> msg_offset;
  if (message != nullptr && message[0] != '\0') {
    msg_offset = builder.CreateString(message);
  }

  flatbuffers::Offset<cs_body_kickoff> kickoff_body = Createcs_body_kickoff(builder, reason, sub_reason, msg_offset);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_kickoff),
                                      kickoff_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API const libatgw_protocol_sdk::crypto_session_ptr_t &libatgw_protocol_sdk::get_crypto_session()
    const {
  return crypto_session_;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_logger(atfw::util::log::log_wrapper::ptr_t logger) {
  logger_ = std::move(logger);
}

// ========================= Write infrastructure (preserved from original) =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::try_write() {
  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  if (check_flag(flag_t::kWriting)) {
    return 0;
  }

  if (write_buffers_.empty()) {
    return 0;
  }

  if (check_flag(flag_t::kClosing)) {
    while (!write_buffers_.empty()) {
      ::atbus::detail::buffer_block *bb = write_buffers_.front();
      size_t nwrite = bb->raw_size();
      write_buffers_.pop_front(nwrite, true);
    }
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  int ret = 0;
  bool is_done = false;

  // Merge small writes for efficiency
  if (write_buffers_.limit().cost_number_ > 1 &&
      write_buffers_.front()->raw_size() <= ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE) {
    auto tls_buffer = get_tls_buffer(tls_buffer_t::kMerge);
    size_t available_bytes = tls_buffer.size() - write_header_offset_;
    unsigned char *buffer_start = tls_buffer.data();
    unsigned char *free_buffer = buffer_start;

    assert(ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE < available_bytes);

    ::atbus::detail::buffer_block *preview_bb = nullptr;
    while (!write_buffers_.empty() && available_bytes > 0) {
      ::atbus::detail::buffer_block *bb = write_buffers_.front();
      if (nullptr == bb || bb->raw_size() > available_bytes) {
        break;
      }

      if (write_buffers_.is_static_mode() && nullptr != preview_bb && preview_bb > bb) {
        break;
      }
      preview_bb = bb;

      size_t bb_size = bb->raw_size() - write_header_offset_;
      memcpy(free_buffer, ::atbus::detail::fn::buffer_next(bb->raw_data(), write_header_offset_), bb_size);
      free_buffer += bb_size;
      available_bytes -= bb_size;

      write_buffers_.pop_front(bb->raw_size(), true);
    }

    void *data = nullptr;
    write_buffers_.push_front(data, write_header_offset_ + static_cast<size_t>(free_buffer - buffer_start));

    assert(data);
    assert(free_buffer > buffer_start);
    assert(static_cast<size_t>(free_buffer - buffer_start) <= (tls_buffer.size() - write_header_offset_));

    data = ::atbus::detail::fn::buffer_next(data, write_header_offset_);
    memcpy(data, buffer_start, static_cast<size_t>(free_buffer - buffer_start));
  }

  ::atbus::detail::buffer_block *writing_block = write_buffers_.front();

  if (nullptr == writing_block) {
    assert(writing_block);
    write_buffers_.pop_front(0, true);
    set_flag(flag_t::kWriting, true);
    return write_done(static_cast<int>(::atframework::gateway::error_code_t::kNoData));
  }

  if (writing_block->raw_size() <= write_header_offset_) {
    write_buffers_.pop_front(writing_block->raw_size(), true);
    return try_write();
  }

  set_flag(flag_t::kWriting, true);
  last_write_ptr_ = writing_block->raw_data();
  ret = callbacks_->write_fn(
      this,
      gsl::span<unsigned char>{reinterpret_cast<unsigned char *>(writing_block->raw_data()), writing_block->raw_size()},
      &is_done);
  if (is_done) {
    return write_done(ret);
  }

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write_message(flatbuffers::FlatBufferBuilder &builder) {
  const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  const void *buf = reinterpret_cast<const void *>(builder.GetBufferPointer());
  size_t len = static_cast<size_t>(builder.GetSize());

  if (nullptr != buf && len > 0) {
    if (len >= std::numeric_limits<uint32_t>::max()) {
      return static_cast<int>(::atframework::gateway::error_code_t::kInvalidSize);
    }

    size_t total_buffer_size = write_header_offset_ + msg_header_len + len;

    void *data = nullptr;
    int res = write_buffers_.push_back(data, total_buffer_size);
    if (res < 0) {
      return res;
    }

    char *buff_start = reinterpret_cast<char *>(data) + write_header_offset_;

    uint32_t hash32 =
        atfw::util::hash::murmur_hash3_x86_32(reinterpret_cast<const char *>(buf), static_cast<int>(len), 0);
    memcpy(buff_start, &hash32, sizeof(uint32_t));

    flatbuffers::WriteScalar<uint32_t>(buff_start + sizeof(uint32_t), static_cast<uint32_t>(len));
    memcpy(buff_start + msg_header_len, buf, len);
  }

  return try_write();
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write(gsl::span<const unsigned char> data) {
  return send_post(::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost),
                   data);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write_done(int status) {
  if (!check_flag(flag_t::kWriting)) {
    return status;
  }
  flag_guard_t flag_guard(flags_, flag_t::kInCallback);

  void *data = nullptr;
  size_t nread = 0;
  size_t nwrite = 0;

  while (true) {
    write_buffers_.front(data, nread, nwrite);
    if (nullptr == data) {
      break;
    }

    assert(0 == nread);

    if (0 == nwrite) {
      write_buffers_.pop_front(0, true);
      break;
    }

    write_buffers_.pop_front(nwrite, true);

    if (last_write_ptr_ == data) {
      break;
    }
  };
  last_write_ptr_ = nullptr;

  set_flag(flag_t::kWriting, false);

  status = try_write();

  if (check_flag(flag_t::kClosing) && !check_flag(flag_t::kClosed) && !check_flag(flag_t::kWriting)) {
    set_flag(flag_t::kClosed, true);

    if (nullptr != callbacks_ && callbacks_->close_fn) {
      return callbacks_->close_fn(this, close_reason_);
    }
  }

  return status;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int reason) { return close(reason, true); }

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int reason, bool is_send_kickoff) {
  if (check_flag(flag_t::kClosing)) {
    return 0;
  }
  close_reason_ = reason;

  if (is_send_kickoff) {
    send_kickoff(reason);
  }

  set_flag(flag_t::kClosing, true);

  if (!check_flag(flag_t::kWriting) && !check_flag(flag_t::kClosed)) {
    set_flag(flag_t::kClosed, true);

    if (nullptr != callbacks_ && callbacks_->close_fn) {
      return callbacks_->close_fn(this, close_reason_);
    }
  }

  return 0;
}

// ========================= Encode / Decode =========================

int libatgw_protocol_sdk::encode_post(const void *in, size_t insz, const void *&out, size_t &outsz) {
  if (check_flag(flag_t::kClosing)) {
    outsz = insz;
    out = in;
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (!crypto_session_) {
    outsz = insz;
    out = in;
    return static_cast<int>(::atframework::gateway::error_code_t::kHandshake);
  }
  return crypto_session_->encrypt_data(in, insz, out, outsz);
}

int libatgw_protocol_sdk::decode_post(const void *in, size_t insz, const void *&out, size_t &outsz) {
  if (check_flag(flag_t::kClosing)) {
    outsz = insz;
    out = in;
    return static_cast<int>(::atframework::gateway::error_code_t::kClosing);
  }

  if (!crypto_session_) {
    outsz = insz;
    out = in;
    return static_cast<int>(::atframework::gateway::error_code_t::kHandshake);
  }
  int ret = crypto_session_->decrypt_data(in, insz, out, outsz);
  if (ret < 0) {
    out = in;
    outsz = insz;
  }
  return ret;
}

// ========================= Create global configure =========================

LIBATGW_PROTOCOL_API std::shared_ptr<detail::crypto_global_configure_t> libatgw_protocol_sdk::create_global_configure(
    crypto_conf_t &conf) {
  auto inst = std::make_shared<detail::crypto_global_configure_t>(std::move(conf));
  if (!inst) {
    return nullptr;
  }

  int ret = inst->init();
  if (0 != ret) {
    return nullptr;
  }

  return inst;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t *libatgw_protocol_sdk::get_global_configure_mutable_conf(
    const std::shared_ptr<detail::crypto_global_configure_t> &global_conf) {
  if (!global_conf) {
    return nullptr;
  }
  return &global_conf->conf_;
}
}  // namespace v2
}  // namespace gateway
}  // namespace atframework
