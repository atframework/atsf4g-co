// Copyright 2026 atframework
// Refactored: 2-message ECDH handshake (like libatbus node_register_req/rsp)

#include "atgateway/protocol/v2/libatgw_protocol_sdk.h"

#include <gsl/select-gsl.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <gsl/span>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "algorithm/compression.h"
#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "algorithm/crypto_hmac.h"
#include "algorithm/murmur_hash.h"
#include "algorithm/sha.h"
#include "common/string_oprs.h"
#include "lock/seq_alloc.h"
#include "nostd/string_view.h"
#include "nostd/type_traits.h"
#include "random/random_generator.h"
#include "string/string_format.h"

namespace atframework {
namespace gateway {
namespace v2 {
namespace {

// session token使用UUID生成，固定32字节
const constexpr size_t kSessionTokenSize = 32;

const constexpr size_t kCipherAadSize = 16;

// Contains iv+aad/tag for ciphers, and some extra cost for future algorithm upgrade.
const constexpr uint64_t kPostExtraCostSize = 32 * 1024;

const constexpr uint64_t kDefaultMaxPostMessageSize = 2 * 1024 * 1024;
const constexpr uint64_t kDefaultCompressionThresholdSize = 1024;
using protocol_crypto_algorithm_t = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atfw::gateway::v2,
                                                                                crypto_algorithm_t);
using protocol_key_exchange_t = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atfw::gateway::v2, key_exchange_t);
using protocol_compression_algorithm_t = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atfw::gateway::v2,
                                                                                     compression_algorithm_t);
using protocol_compression_level_t = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atfw::gateway::v2,
                                                                                 compression_level_t);
using protocol_kdf_algorithm_t = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atfw::gateway::v2, kdf_algorithm_t);

static std::string random_buffer(size_t size) {
  static atfw::util::random::xoshiro256_starstar random_engine(
      static_cast<atfw::util::random::xoshiro256_starstar::result_type>(
          std::chrono::system_clock::now().time_since_epoch().count()));
  using random_unit_type = decltype(random_engine.random());
  constexpr size_t kRandomUnit = sizeof(random_unit_type);

  std::string ret;
  ret.resize((size + kRandomUnit - 1) / kRandomUnit * kRandomUnit);

  for (char *ptr = ret.data(); ptr + kRandomUnit <= ret.data() + ret.size(); ptr += kRandomUnit) {
    *reinterpret_cast<random_unit_type *>(ptr) = random_engine.random();
  }

  if (ret.size() > size) {
    ret.resize(size);
  }

  return ret;
}

static uint64_t alloc_seq() {
  static atfw::util::lock::seq_alloc_u64 seq_alloc;
  uint64_t ret = seq_alloc.inc();
  while (0 == ret) {
    ret = seq_alloc.inc();
  }
  return ret;
}

static const std::string &get_shared_empty_string() {
  static std::string empty;
  return empty;
}

template <class T>
struct ATFW_UTIL_SYMBOL_LOCAL supported_name_mapping {
  using underlying_type = atfw::util::nostd::underlying_type_t<T>;
  std::unordered_map<underlying_type, std::vector<std::string>> supported_values;
  std::unordered_map<std::string, T> name_mapping;

  void add_mapping(atfw::util::nostd::string_view name, T value) {
    std::vector<std::string> &names = supported_values[static_cast<underlying_type>(value)];
    names.push_back(std::string(name));
    name_mapping.emplace(std::string(name), value);
  }

  bool is_supported(T value) const noexcept {
    return supported_values.end() != supported_values.find(static_cast<underlying_type>(value));
  }

  const std::string &get_default_name(T value) const noexcept {
    auto iter = supported_values.find(static_cast<underlying_type>(value));
    if (supported_values.end() != iter) {
      return iter->second.empty() ? get_shared_empty_string() : iter->second.front();
    }
    return get_shared_empty_string();
  }

  gsl::span<const std::string> get_names(T value) const noexcept {
    auto iter = supported_values.find(static_cast<underlying_type>(value));
    if (supported_values.end() != iter) {
      return gsl::span<const std::string>(iter->second);
    }
    return {};
  }
};

static const supported_name_mapping<protocol_key_exchange_t> &get_supported_key_exchanges() {
  static supported_name_mapping<protocol_key_exchange_t> supported;
  if ATFW_UTIL_LIKELY_CONDITION (!supported.supported_values.empty()) {
    return supported;
  }

  for (const auto &name : atfw::util::crypto::dh::get_all_curve_names()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:x25519")) {
      supported.add_mapping("ecdh:x25519", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kX25519));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:secp256r1")) {
      supported.add_mapping("ecdh:secp256r1", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp256r1));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:p-256")) {
      supported.add_mapping("ecdh:p-256", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp256r1));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:secp384r1")) {
      supported.add_mapping("ecdh:secp384r1", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp384r1));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:p-384")) {
      supported.add_mapping("ecdh:p-384", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp384r1));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:secp521r1")) {
      supported.add_mapping("ecdh:secp521r1", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp521r1));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "ecdh:p-521")) {
      supported.add_mapping("ecdh:p-521", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp521r1));
    }
  }
  return supported;
}

static const supported_name_mapping<protocol_crypto_algorithm_t> &get_supported_crypto_algorithms() {
  static supported_name_mapping<protocol_crypto_algorithm_t> supported;
  if ATFW_UTIL_LIKELY_CONDITION (!supported.supported_values.empty()) {
    return supported;
  }

  for (const auto &name : atfw::util::crypto::cipher::get_all_cipher_names()) {
    if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "xxtea")) {
      supported.add_mapping("xxtea", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXxtea));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-128-cbc")) {
      supported.add_mapping("aes-128-cbc", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Cbc));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-192-cbc")) {
      supported.add_mapping("aes-192-cbc", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Cbc));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-256-cbc")) {
      supported.add_mapping("aes-256-cbc", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Cbc));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-128-gcm")) {
      supported.add_mapping("aes-128-gcm", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Gcm));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-192-gcm")) {
      supported.add_mapping("aes-192-gcm", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Gcm));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "aes-256-gcm")) {
      supported.add_mapping("aes-256-gcm", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Gcm));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "chacha20")) {
      supported.add_mapping("chacha20", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "chacha20-poly1305-ietf")) {
      supported.add_mapping("chacha20-poly1305-ietf",
                            ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20Poly1305Ietf));
    } else if (0 == UTIL_STRFUNC_STRCASE_CMP(name.c_str(), "xchacha20-poly1305-ietf")) {
      supported.add_mapping("xchacha20-poly1305-ietf",
                            ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXchacha20Poly1305Ietf));
    }
  }

  return supported;
}

static const supported_name_mapping<protocol_compression_algorithm_t> &get_supported_compression_algorithm() {
  static supported_name_mapping<protocol_compression_algorithm_t> supported;
  if ATFW_UTIL_LIKELY_CONDITION (!supported.supported_values.empty()) {
    return supported;
  }

  for (const auto &algo_id : atfw::util::compression::get_supported_algorithms()) {
    if (algo_id == atfw::util::compression::algorithm_t::kNone) {
      continue;
    }

    switch (algo_id) {
      case atfw::util::compression::algorithm_t::kZstd:
        supported.add_mapping("zstd", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZstd));
        break;
      case atfw::util::compression::algorithm_t::kLz4:
        supported.add_mapping("lz4", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kLz4));
        break;
      case atfw::util::compression::algorithm_t::kSnappy:
        supported.add_mapping("snappy", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kSnappy));
        break;
      case atfw::util::compression::algorithm_t::kZlib:
        supported.add_mapping("zlib", ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZlib));
        break;
      default:
        break;
    }
  }
  return supported;
}

/// Map key_exchange_t to "ecdh:<curve>" string for shared_context::init()
static const std::string &map_key_exchange_to_dh_param(protocol_key_exchange_t ke) {
  return get_supported_key_exchanges().get_default_name(ke);
}

/// Map flatbuffers crypto_algorithm_t to cipher name string
static const std::string &map_crypto_algorithm_to_name(protocol_crypto_algorithm_t alg) {
  return get_supported_crypto_algorithms().get_default_name(alg);
}

/// Map flatbuffers compression_algorithm_t to atfw::util::compression::algorithm_t
static ::atfw::util::compression::algorithm_t map_compression_algorithm(protocol_compression_algorithm_t alg) {
  using namespace ::atfw::gateway::v2;
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

/// Padding buffer size to aligned size classes (like mimalloc/atbus).
/// <=64: align to 8, <=512: align to 16, <=8K: next power of 2, >8K: align to 4K page.
static size_t padding_buffer_size(size_t origin_size) {
  if (origin_size <= 64) {
    return (origin_size + 7) & ~static_cast<size_t>(7);
  }
  if (origin_size <= 512) {
    return (origin_size + 15) & ~static_cast<size_t>(15);
  }

  if (origin_size <= 8192) {
    size_t result = 512;
    while (result < origin_size) {
      result <<= 1;
    }
    return result;
  }

  return (origin_size + 4095) & ~static_cast<size_t>(4095);
}

/// Map compression_level_t to atfw::util::compression::level_t
static ::atfw::util::compression::level_t map_compression_level(protocol_compression_level_t level) {
  using namespace ::atfw::gateway::v2;
  switch (level) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kStorage):
      return ::atfw::util::compression::level_t::kStorage;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kFast):
      return ::atfw::util::compression::level_t::kFast;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kLowCpu):
      return ::atfw::util::compression::level_t::kLowCpu;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kBalanced):
      return ::atfw::util::compression::level_t::kBalanced;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kHighRatio):
      return ::atfw::util::compression::level_t::kHighRatio;
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kMaxRatio):
      return ::atfw::util::compression::level_t::kMaxRatio;
    default:
      return ::atfw::util::compression::level_t::kDefault;
  }
}
}  // namespace

struct crypto_shared_context_t {
  using ptr_t = std::shared_ptr<crypto_shared_context_t>;

  crypto_shared_context_t(libatgw_protocol_sdk::crypto_conf_t conf) : conf_(std::move(conf)), inited_(false) {
    shared_dh_context_ = atfw::util::crypto::dh::shared_context::create();
  }
  ~crypto_shared_context_t() { close(); }

  int setup_key_exchange_algorithm() {
    using namespace ::atfw::gateway::v2;

    // Initialize DH shared context based on key exchange algorithm
    if (conf_.key_exchange_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kNone)) {
      if (shared_dh_context_) {
        shared_dh_context_->reset();
      }
      const std::string &dh_param = map_key_exchange_to_dh_param(conf_.key_exchange_algorithm);
      if (dh_param.empty()) {
        return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
      }

      int ret = shared_dh_context_->init(dh_param);
      if (0 != ret) {
        return static_cast<int>(::atfw::gateway::error_code_t::kCryptoInitDhparam);
      }
    }

    return 0;
  }

  int reload_key_exchange_algorithm(protocol_key_exchange_t key_exchange_algorithm) {
    if (conf_.key_exchange_algorithm == key_exchange_algorithm) {
      return 0;
    }

    conf_.key_exchange_algorithm = key_exchange_algorithm;
    return setup_key_exchange_algorithm();
  }

  int setup_crypto_algorithm() {
    using namespace ::atfw::gateway::v2;

    ordered_algorithms_.clear();
    available_algorithms_.clear();
    ordered_algorithms_.reserve(conf_.supported_algorithms.size());
    available_algorithms_.reserve(conf_.supported_algorithms.size());

    const auto &all_supported = get_supported_crypto_algorithms();
    for (auto alg : conf_.supported_algorithms) {
      if (all_supported.is_supported(alg)) {
        available_algorithms_.insert(alg);
        ordered_algorithms_.push_back(alg);
      }
    }
    return 0;
  }

  int reload_crypto_algorithm(gsl::span<const crypto_algorithm_t> algorithms) {
    if (algorithms.data() == conf_.supported_algorithms.data()) {
      if (algorithms.size() < conf_.supported_algorithms.size()) {
        conf_.supported_algorithms.resize(algorithms.size());
      }
    } else {
      conf_.supported_algorithms.assign(algorithms.begin(), algorithms.end());
    }

    return setup_crypto_algorithm();
  }

  int setup_compression_algorithm() {
    using namespace ::atfw::gateway::v2;

    available_compression_algorithms_.clear();
    ordered_compression_algorithms_.clear();

    available_compression_algorithms_.reserve(conf_.compression_algorithms.size());
    ordered_compression_algorithms_.reserve(conf_.compression_algorithms.size());

    const auto &all_supported = get_supported_compression_algorithm();
    for (auto calg : conf_.compression_algorithms) {
      if (all_supported.is_supported(calg)) {
        available_compression_algorithms_.insert(calg);
        ordered_compression_algorithms_.push_back(calg);
      }
    }

    return 0;
  }

  int reload_compression_algorithm(gsl::span<const protocol_compression_algorithm_t> algorithms) {
    if (algorithms.data() == conf_.compression_algorithms.data()) {
      if (algorithms.size() < conf_.compression_algorithms.size()) {
        conf_.compression_algorithms.resize(algorithms.size());
      }
    } else {
      conf_.compression_algorithms.assign(algorithms.begin(), algorithms.end());
    }

    return setup_compression_algorithm();
  }

  int init() {
    int ret = 0;
    close();

    ret = setup_key_exchange_algorithm();
    if (ret != 0) {
      return ret;
    }

    ret = setup_crypto_algorithm();
    if (ret != 0) {
      return ret;
    }

    ret = setup_compression_algorithm();
    if (ret != 0) {
      return ret;
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
    ordered_algorithms_.clear();
    available_compression_algorithms_.clear();
    ordered_compression_algorithms_.clear();
    if (shared_dh_context_) {
      shared_dh_context_->reset();
    }
  }

  bool check_algorithm(protocol_crypto_algorithm_t alg) const {
    using namespace ::atfw::gateway::v2;
    if (alg == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
      return true;
    }
    return available_algorithms_.count(alg) > 0;
  }

  libatgw_protocol_sdk::crypto_conf_t conf_;
  bool inited_;
  std::unordered_set<protocol_crypto_algorithm_t> available_algorithms_;
  std::vector<protocol_crypto_algorithm_t> ordered_algorithms_;
  std::unordered_set<protocol_compression_algorithm_t> available_compression_algorithms_;
  std::vector<protocol_compression_algorithm_t> ordered_compression_algorithms_;
  atfw::util::crypto::dh::shared_context::ptr_t shared_dh_context_;
};

// ========================= crypto_conf_t =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t::crypto_conf_t()
    : update_interval(0),
      key_exchange_algorithm(key_exchange_type::kNone),
      max_post_message_size(kDefaultMaxPostMessageSize),
      compression_threshold_size(kDefaultCompressionThresholdSize),
      compression_level(compression_level_t::kDefault),
      client_mode(false) {
  set_default();
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t::crypto_conf_t(const crypto_conf_t &other) = default;

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t::crypto_conf_t(crypto_conf_t &&other) noexcept = default;

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t &libatgw_protocol_sdk::crypto_conf_t::operator=(
    const crypto_conf_t &other) = default;

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t &libatgw_protocol_sdk::crypto_conf_t::operator=(
    crypto_conf_t &&other) noexcept = default;

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t::~crypto_conf_t() = default;

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypto_conf_t::set_default() {
  access_tokens.clear();
  key_exchange_algorithm = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kX25519);

  // Default crypto algorithms ordered by priority:
  //   aes-256-gcm > aes-192-gcm > aes-128-gcm > chacha20-poly1305-ietf > xchacha20-poly1305-ietf
  //   > aes-256-cbc > aes-192-cbc > aes-128-cbc > chacha20 > xxtea
  supported_algorithms.clear();
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Gcm));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Gcm));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Gcm));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20Poly1305Ietf));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXchacha20Poly1305Ietf));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Cbc));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes192Cbc));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes128Cbc));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kChacha20));
  supported_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kXxtea));

  // Default compression algorithms ordered by priority: zstd > lz4 > snappy > zlib
  compression_algorithms.clear();
  compression_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZstd));
  compression_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kLz4));
  compression_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kSnappy));
  compression_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZlib));

  // Default KDF
  supported_kdf_algorithms.clear();
  supported_kdf_algorithms.push_back(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256));

  max_post_message_size = kDefaultMaxPostMessageSize;             // 2MB default
  compression_threshold_size = kDefaultCompressionThresholdSize;  // 1024 bytes minimum for compression
  compression_level = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kDefault);
  update_interval = 300;
  client_mode = false;
}

// ========================= crypto_session_t =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_session_t::crypto_session_t()
    : selected_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)),
      selected_kdf(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256)),
      key_exchange_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kNone)),
      selected_compression_algorithm(ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)),
      max_post_message_size(kDefaultMaxPostMessageSize),
      handshake_sequence_id_(0) {}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_session_t::~crypto_session_t() { close(); }

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::handshake_generate_self_key() {
  if (!shared_conf || !shared_conf->shared_dh_context_) {
    return static_cast<int>(::atfw::gateway::error_code_t::kHandshake);
  }

  // Create DH context and generate key pair
  handshake_dh_ = std::make_unique<::atfw::util::crypto::dh>();
  int res = handshake_dh_->init(shared_conf->shared_dh_context_);
  if (0 != res) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // generate params (this generates the key pair internally)
  std::vector<unsigned char> dh_params;
  res = handshake_dh_->make_params(dh_params);
  if (0 != res) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // extract just the public key bytes
  res = handshake_dh_->make_public(handshake_self_public_key_);
  if (0 != res) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::handshake_read_peer_key(
    gsl::span<const unsigned char> peer_public_key, gsl::span<const protocol_crypto_algorithm_t> peer_algorithms,
    gsl::span<const protocol_crypto_algorithm_t> local_algorithms, bool need_confirm) {
  if (!handshake_dh_) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
  }

  // Negotiate algorithm: pick first mutually supported one
  selected_algorithm = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone);
  std::unordered_set<std::underlying_type<protocol_crypto_algorithm_t>::type> peer_alg_set;
  peer_alg_set.reserve(peer_algorithms.size());
  for (const auto &alg : peer_algorithms) {
    peer_alg_set.insert(static_cast<std::underlying_type<protocol_crypto_algorithm_t>::type>(alg));
  }
  for (size_t i = 0; i < local_algorithms.size(); ++i) {
    if (local_algorithms[i] != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone) &&
        peer_alg_set.end() != peer_alg_set.find(static_cast<std::underlying_type<protocol_crypto_algorithm_t>::type>(
                                  local_algorithms[i]))) {
      selected_algorithm = local_algorithms[i];
      break;
    }
  }

  // Read peer public key
  int res = handshake_dh_->read_public(peer_public_key.data(), peer_public_key.size());
  if (0 != res) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // Calculate shared secret
  std::vector<unsigned char> shared_secret;
  res = handshake_dh_->calc_secret(shared_secret);
  if (0 != res) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // Derive key from shared secret and setup ciphers
  int ret = derive_key_from_shared_secret(shared_secret, need_confirm);

  // Release DH context -- no longer needed after key derivation
  handshake_dh_.reset();

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::setup_crypto_with_key(
    protocol_crypto_algorithm_t algorithm, gsl::span<const unsigned char> key, gsl::span<const unsigned char> iv,
    bool need_confirm) {
  selected_algorithm = algorithm;

  if (algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
    send_cipher_.reset();
    handshaking_receive_cipher_.reset();
    if (!need_confirm) {
      receive_cipher_.reset();
    }
    return 0;
  }

  const auto &cipher_name = map_crypto_algorithm_to_name(algorithm);
  if (cipher_name.empty()) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
  }

  // Setup send cipher
  send_cipher_ = std::make_unique<::atfw::util::crypto::cipher>();
  if (send_cipher_->init(cipher_name) < 0) {
    send_cipher_.reset();
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
  }
  if (!key.empty() && send_cipher_->set_key(key.data(), static_cast<uint32_t>(key.size() * 8)) < 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }
  if (!iv.empty() && send_cipher_->set_iv(iv.data(), static_cast<uint32_t>(iv.size())) < 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // Setup receive cipher (same key and IV)
  std::unique_ptr<::atfw::util::crypto::cipher> *receive_cipher =
      need_confirm ? &handshaking_receive_cipher_ : &receive_cipher_;
  *receive_cipher = std::make_unique<::atfw::util::crypto::cipher>();
  if ((*receive_cipher)->init(cipher_name) < 0) {
    (*receive_cipher).reset();
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
  }
  if (!key.empty() && (*receive_cipher)->set_key(key.data(), static_cast<uint32_t>(key.size() * 8)) < 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }
  if (!iv.empty() && (*receive_cipher)->set_iv(iv.data(), static_cast<uint32_t>(iv.size())) < 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  return 0;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypto_session_t::close() {
  handshake_dh_.reset();
  send_cipher_.reset();
  receive_cipher_.reset();
  handshaking_receive_cipher_.reset();
  handshake_self_public_key_.clear();
  handshake_sequence_id_ = 0;
}

int libatgw_protocol_sdk::crypto_session_t::derive_key_from_shared_secret(
    const std::vector<unsigned char> &shared_secret, bool need_confirm) {
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
    return 0;
  }

  const std::string &cipher_name = map_crypto_algorithm_to_name(selected_algorithm);
  if (cipher_name.empty()) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
  }

  // Create a temporary cipher to get key/iv sizes
  ::atfw::util::crypto::cipher temp_cipher;
  if (temp_cipher.init(cipher_name) < 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoAlgorithmNotSupported);
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
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  // KEY = key_material[0..key_size-1], IV = key_material[key_size..key_size+iv_size-1]
  return setup_crypto_with_key(
      selected_algorithm, gsl::span<const unsigned char>(key_material.data(), key_size),
      gsl::span<const unsigned char>(iv_size > 0 ? key_material.data() + key_size : nullptr, iv_size), need_confirm);
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypto_session_t::update_handshake(uint64_t handshake_sequence_id) {
  // Generate sequence ID
  if (0 == handshake_sequence_id) {
    if (handshake_sequence_id_ == 0) {
      auto now = std::chrono::system_clock::now();
      handshake_sequence_id_ =
          static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
    } else {
      ++handshake_sequence_id_;
    }
    // 1是特殊值，表示无效
    if (handshake_sequence_id_ == 1) {
      ++handshake_sequence_id_;
    }
  } else {
    handshake_sequence_id_ = handshake_sequence_id;
  }
  handshake_start_time_ = std::chrono::system_clock::now();
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypto_session_t::confirm_handshake(uint64_t handshake_sequence_id) {
  if (handshake_sequence_id_ != 0 && handshake_sequence_id_ == handshake_sequence_id) {
    receive_cipher_ = std::move(handshaking_receive_cipher_);
    handshaking_receive_cipher_.reset();
    handshake_sequence_id_ = 1;
  }
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::encrypt_data(
    gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
    std::vector<unsigned char> &compression_heap_buffer, std::unique_ptr<unsigned char[]> &crypto_heap_buffer,
    gsl::span<const unsigned char> &iv, atfw::util::nostd::string_view &aad) {
  // 加密
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone) || !send_cipher_) {
    // No cipher: output points to same data (const_cast is safe here since caller owns the buffer for passthrough)
    out = in;
    return 0;
  }

  if (in.empty()) {
    out = gsl::span<const unsigned char>{};
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  // 对齐到加密算法的block size，有些套件需要我们手动对齐
  size_t block_size = send_cipher_->get_block_size();
  std::vector<unsigned char> padding_input_buffer;
  if (block_size > 1 && send_cipher_ && !send_cipher_->is_aead()) {
    // 对于非AEAD加密算法（如CBC），需要对齐到block size
    size_t padded_size = ((in.size() + block_size - 1) / block_size) * block_size;
    unsigned char padding_value = static_cast<unsigned char>(padded_size - in.size());
    // 填充字节使用 PKCS#7 padding
    // in 和 compression_heap_buffer 如果是同一块内存可以直接 compression_heap_buffer.resize(),
    // 大概率不需要重新分配内存。
    if (in.data() == compression_heap_buffer.data()) {
      if (padded_size > compression_heap_buffer.size()) {
        compression_heap_buffer.resize(padded_size, padding_value);
      }
      in = gsl::span<const unsigned char>{compression_heap_buffer.data(), padded_size};
    } else if (padded_size > in.size()) {
      auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCompress);
      if (tls_buffer.size() >= padded_size) {
        memcpy(tls_buffer.data(), in.data(), in.size());
        memset(tls_buffer.data() + in.size(), padding_value, padded_size - in.size());
        in = gsl::span<const unsigned char>{tls_buffer.data(), padded_size};
      } else {
        padding_input_buffer.reserve(padded_size);
        padding_input_buffer.assign(in.data(), in.data() + in.size());
        padding_input_buffer.resize(padded_size, padding_value);
        in = gsl::span<const unsigned char>{padding_input_buffer.data(), padded_size};
      }
    }
  }

  if (aad.empty()) {
    send_aad_ = random_buffer(kCipherAadSize);
    aad = send_aad_;
  }

  size_t needed_size =
      padding_buffer_size(in.size() + aad.size() + send_cipher_->get_block_size() + send_cipher_->get_tag_size());
  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCrypto);
  unsigned char *buffer = tls_buffer.data();
  size_t len = tls_buffer.size();

  if (len < needed_size) {
    size_t padded_needed_size = padding_buffer_size(needed_size);
    crypto_heap_buffer.reset(new (std::nothrow) unsigned char[padded_needed_size]);
    if (!crypto_heap_buffer) {
      out = gsl::span<const unsigned char>{};
      return static_cast<int>(::atfw::gateway::error_code_t::kMalloc);
    }
    buffer = crypto_heap_buffer.get();
    len = padded_needed_size;
  }

  if (!iv.empty()) {
    send_cipher_->set_iv(iv);
  } else {
    auto current_iv = send_cipher_->get_iv();
    send_iv_.assign(current_iv.data(), current_iv.data() + current_iv.size());
    iv = gsl::span<const unsigned char>{send_iv_.data(), send_iv_.size()};
  }

  int res = 0;
  if (send_cipher_->is_aead()) {
    res = send_cipher_->encrypt_aead(in.data(), in.size(), buffer, &len,
                                     reinterpret_cast<const unsigned char *>(aad.data()), aad.size());
  } else {
    res = send_cipher_->encrypt(in.data(), in.size(), buffer, &len);
  }
  if (res < 0) {
    out = gsl::span<const unsigned char>{};
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  out = gsl::span<const unsigned char>{buffer, len};
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::decrypt_data(
    gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
    std::unique_ptr<unsigned char[]> &heap_buffer, gsl::span<const unsigned char> iv,
    atfw::util::nostd::string_view aad) {
  if (selected_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone) || !receive_cipher_) {
    out = in;
    return 0;
  }

  if (in.empty()) {
    out = gsl::span<const unsigned char>{};
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  size_t needed = in.size() + receive_cipher_->get_block_size() + receive_cipher_->get_iv_size();

  auto tls_buffer = libatgw_protocol_api::get_tls_buffer(libatgw_protocol_api::tls_buffer_t::kCrypto);
  unsigned char *buffer = tls_buffer.data();
  size_t len = tls_buffer.size();

  if (len < needed) {
    size_t padded_size = padding_buffer_size(needed);
    heap_buffer.reset(new (std::nothrow) unsigned char[padded_size]);
    if (!heap_buffer) {
      out = gsl::span<unsigned char>{};
      return static_cast<int>(::atfw::gateway::error_code_t::kMalloc);
    }
    buffer = heap_buffer.get();
    len = padded_size;
  }

  if (!iv.empty()) {
    receive_cipher_->set_iv(iv);
  }

  int res = 0;
  if (receive_cipher_->is_aead()) {
    res = receive_cipher_->decrypt_aead(in.data(), in.size(), buffer, &len,
                                        reinterpret_cast<const unsigned char *>(aad.data()), aad.size());
  } else {
    res = receive_cipher_->decrypt(in.data(), in.size(), buffer, &len);
  }
  if (res < 0) {
    out = gsl::span<const unsigned char>{};
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoOperation);
  }

  out = gsl::span<const unsigned char>{buffer, len};
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::compress_data(gsl::span<const unsigned char> in,
                                                                               gsl::span<const unsigned char> &out,
                                                                               std::vector<unsigned char> &heap_buffer,
                                                                               compression_level_type level,
                                                                               uint64_t threshold) {
  if (selected_compression_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    out = in;
    return 0;
  }

  if (in.empty()) {
    out = gsl::span<unsigned char>{};
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  // Skip compression for small packets below threshold
  if (threshold > 0 && in.size() < static_cast<size_t>(threshold)) {
    out = in;
    return 0;
  }

  auto mapped_alg = map_compression_algorithm(selected_compression_algorithm);
  if (mapped_alg == ::atfw::util::compression::algorithm_t::kNone) {
    out = in;
    return 0;
  }

  auto mapped_level = map_compression_level(level);

  int res = ::atfw::util::compression::compress(mapped_alg, in, heap_buffer, mapped_level);
  if (0 != res) {
    out = in;
    return static_cast<int>(::atfw::gateway::error_code_t::kCompressionOperation);
  }

  out = gsl::span<const unsigned char>{heap_buffer.data(), heap_buffer.size()};
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypto_session_t::decompress_data(
    gsl::span<const unsigned char> in, size_t original_size, gsl::span<const unsigned char> &out,
    std::vector<unsigned char> &heap_buffer) {
  if (selected_compression_algorithm == ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    out = in;
    return 0;
  }

  if (in.empty()) {
    out = in;
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  auto mapped_alg = map_compression_algorithm(selected_compression_algorithm);
  if (mapped_alg == ::atfw::util::compression::algorithm_t::kNone) {
    out = in;
    return 0;
  }

  int res = ::atfw::util::compression::decompress(mapped_alg, in, original_size, heap_buffer);
  if (0 != res) {
    out = in;
    return static_cast<int>(::atfw::gateway::error_code_t::kCompressionOperation);
  }

  out = gsl::span<const unsigned char>{heap_buffer.data(), heap_buffer.size()};
  return 0;
}

// ========================= libatgw_protocol_sdk =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::libatgw_protocol_sdk(std::shared_ptr<crypto_shared_context_t> shared_conf)
    : shared_conf_(std::move(shared_conf)),
      session_id_(0),
      read_head_{},
      last_write_ptr_(nullptr),
      close_reason_(0),
      close_sub_reason_(0),
      crypto_session_(std::make_shared<crypto_session_t>()) {
  read_head_.len = 0;

  ping_.last_ping = ping_data_t::clk_t::from_time_t(0);
  ping_.last_delta = 0;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::~libatgw_protocol_sdk() {
  close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kUnknown), false, 0, "");
  close_handshake(static_cast<int>(::atfw::gateway::error_code_t::kSessionExpired));
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
    errcode = static_cast<int>(::atfw::gateway::error_code_t::kClosing);
    return;
  }

  errcode = static_cast<int>(::atfw::gateway::error_code_t::kSuccess);
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

      uint64_t max_post_message_size = crypto_session_->max_post_message_size;
      if (msg_len > max_post_message_size + kPostExtraCostSize) {
        is_free = true;
        break;
      }

      if (buff_left_len >= msg_header_len + msg_len) {
        uint32_t check_hash =
            atfw::util::hash::murmur_hash3_x86_32(buff_start + msg_header_len, static_cast<int>(msg_len), 0);
        uint32_t expect_hash = 0;
        memcpy(&expect_hash, buff_start, sizeof(uint32_t));

        if (check_hash != expect_hash) {
          errcode = static_cast<int>(::atfw::gateway::error_code_t::kBadData);
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
      errcode = static_cast<int>(::atfw::gateway::error_code_t::kBadData);
    }

    dispatch_data(gsl::span<const unsigned char>{reinterpret_cast<unsigned char *>(data) + msg_header_len, msg_len},
                  errcode);
    read_buffers_.pop_front(0, true);
  }

  if (is_free) {
    errcode = static_cast<int>(::atfw::gateway::error_code_t::kInvalidSize);
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
  if (false == atfw::gateway::v2::Verifyclient_messageBuffer(cs_msg_verify)) {
    close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData));
    return;
  }

  const atfw::gateway::v2::client_message *msg = atfw::gateway::v2::Getclient_message(data.data());
  if (nullptr == msg->head()) {
    return;
  }

  switch (msg->head()->type()) {
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost): {
      const ::atfw::gateway::v2::cs_body_post *msg_body = msg->body_as_cs_body_post();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }

      // Server rejects data messages before handshake is done
      if (!check_flag(flag_t::kHandshakeDone)) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kHandshake), false, 0, "");
        break;
      }

      if (nullptr == msg_body->data() || msg_body->data()->size() == 0) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }

      uint64_t max_post_message_size = crypto_session_->max_post_message_size;

      if (msg_body->data()->size() > max_post_message_size) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidMessageSize), false, 0, "");
        break;
      }

      gsl::span<const unsigned char> decoded_out;
      gsl::span<const unsigned char> encoded_in{reinterpret_cast<const unsigned char *>(msg_body->data()->data()),
                                                static_cast<size_t>(msg_body->data()->size())};
      compression_algorithm_t compression_algorithm = compression_algorithm_t::kNone;
      size_t compression_origin_size = encoded_in.size();
      const auto *compression_info = msg_body->compression();
      if (compression_info != nullptr) {
        compression_algorithm = compression_info->type();
        compression_origin_size = static_cast<size_t>(compression_info->original_size());
      }
      gsl::span<const unsigned char> iv;
      atfw::util::nostd::string_view aad;
      const auto *crypto_info = msg_body->crypto();
      if (crypto_info != nullptr) {
        if (crypto_info->iv() != nullptr) {
          iv = gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(crypto_info->iv()->data()),
                                              crypto_info->iv()->size()};
        }
        if (crypto_info->aad() != nullptr) {
          aad = atfw::util::nostd::string_view{reinterpret_cast<const char *>(crypto_info->aad()->data()),
                                               crypto_info->aad()->size()};
        }
      }
      std::vector<unsigned char> compression_heap_buffer;
      std::unique_ptr<unsigned char[]> crypto_heap_buffer;
      int res = decode_post(encoded_in, static_cast<size_t>(msg_body->length()), decoded_out, compression_algorithm,
                            compression_origin_size, iv, aad, compression_heap_buffer, crypto_heap_buffer);
      if (0 == res) {
        if (nullptr != callbacks_ && callbacks_->message_fn) {
          callbacks_->message_fn(
              this, gsl::span<const unsigned char>{decoded_out.data(), static_cast<size_t>(msg_body->length())});
        }
      } else {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
      }
      break;
    }
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake): {
      const ::atfw::gateway::v2::cs_body_handshake *msg_body = msg->body_as_cs_body_handshake();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }

      dispatch_handshake(*msg_body);
      break;
    }
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kConfirm): {
      const ::atfw::gateway::v2::cs_body_confirm *msg_body = msg->body_as_cs_body_confirm();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }
      crypto_session_->confirm_handshake(msg_body->handshake_sequence());
      break;
    }
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPing): {
      // Server rejects ping before handshake
      if (!check_flag(flag_t::kHandshakeDone)) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kHandshake), false, 0, "");
        break;
      }

      const ::atfw::gateway::v2::cs_body_ping *msg_body = msg->body_as_cs_body_ping();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }

      send_pong(msg_body->timepoint());
      break;
    }
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPong): {
      const ::atfw::gateway::v2::cs_body_ping *msg_body = msg->body_as_cs_body_ping();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData));
        break;
      }

      if (0 != msg_body->timepoint()) {
        ping_.last_delta = static_cast<time_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                   ping_data_t::clk_t::now().time_since_epoch())
                                                   .count()) -
                           static_cast<time_t>(msg_body->timepoint());
        if (ping_.last_delta <= 0) {
          ping_.last_delta = 1;  // Avoid zero or negative delta due to clock issues
        }
      }
      break;
    }
    case atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kKickoff): {
      const ::atfw::gateway::v2::cs_body_kickoff *msg_body = msg->body_as_cs_body_kickoff();
      if (msg_body == nullptr) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
        break;
      }

      close(msg_body->reason(), false, msg_body->sub_reason(),
            msg_body->message() != nullptr
                ? atfw::util::nostd::string_view{msg_body->message()->data(), msg_body->message()->size()}
                : atfw::util::nostd::string_view{});
      break;
    }
    default: {
      // Unknown message type before handshake: disconnect
      if (!check_flag(flag_t::kHandshakeDone)) {
        close(static_cast<int32_t>(::atfw::gateway::close_reason_t::kInvalidData), false, 0, "");
      }
      break;
    }
  }
}

// ========================= Handshake (2-message exchange) =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  using namespace atfw::gateway::v2;

  // Reconnect requests are allowed even after the initial handshake is done,
  // because kHandshakeUpdate is set later inside setup_handshake() when the
  // server processes the reconnect/key-refresh flow.
  if (check_flag(flag_t::kHandshakeDone) && !check_flag(flag_t::kHandshakeUpdate)) {
    if (body_handshake.step() != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectReq) &&
        body_handshake.step() != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectRsp)) {
      return static_cast<int>(::atfw::gateway::error_code_t::kHandshake);
    }
  }

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
      ret = static_cast<int>(::atfw::gateway::error_code_t::kBadProtocol);
      break;
    }
  }

  // handshake failed => close connection
  if (ret < 0) {
    close_handshake(ret);
    close(static_cast<int>(::atfw::gateway::close_reason_t::kHandshake), false, 0, "");
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_key_exchange_req(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Server-side: receive kKeyExchangeReq from client =====
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !callbacks_->new_session_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  // Verify access data first (before setup_handshake)
  int ret = verify_access_data(body_handshake);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "key exchange req: access data verification failed");
    return ret;
  }

  // Allocate new session ID
  {
    flag_guard_t flag_guard(flags_, flag_t::kInCallback);
    callbacks_->new_session_fn(this, session_id_);
  }

  // Dispatch using shared server-side handshake logic
  return dispatch_handshake_server_common(body_handshake,
                                          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeRsp));
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_key_exchange_rsp(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Client-side: receive kKeyExchangeRsp from server =====
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(static_cast<int>(::atfw::gateway::error_code_t::kHandshake),
                             "key exchange rsp: session_id is 0");
    return static_cast<int>(::atfw::gateway::error_code_t::kHandshake);
  }

  session_id_ = body_handshake.session_id();
  return dispatch_handshake_client_common(body_handshake);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_req(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Server-side: receive kReconnectReq from client =====
  // Performs full inline ECDH handshake while reusing the session_id.
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  int ret = 0;
  // Verify access data
  ret = verify_access_data(body_handshake);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "reconnect req: access data verification failed");
    return ret;
  }

  if (callbacks_->reconnect_fn) {
    flag_guard_t flag_guard(flags_, flag_t::kInCallback);
    ret = callbacks_->reconnect_fn(this, body_handshake.session_id());
  } else {
    ret = static_cast<int>(::atfw::gateway::error_code_t::kRefuseReconnect);
  }

  if (0 != ret) {
    // Reconnect refused, send rejection response with session_id = 0
    using namespace ::atfw::gateway::v2;

    flatbuffers::FlatBufferBuilder builder;
    flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
        builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), alloc_seq());

    flatbuffers::Offset<cs_body_handshake> handshake_body =
        Createcs_body_handshake(builder, 0, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectRsp),
                                crypto_session_->key_exchange_algorithm);

    builder.Finish(Createclient_message(builder, header_data,
                                        ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                        handshake_body.Union()),
                   client_messageIdentifier());
    return write_message(builder);
  }

  // Adopt the reconnected session_id before building the response
  session_id_ = body_handshake.session_id();
  // Dispatch using shared server-side handshake logic
  return dispatch_handshake_server_common(body_handshake,
                                          ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectRsp));
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_rsp(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  // ===== Client-side: receive kReconnectRsp from server =====
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(static_cast<int>(::atfw::gateway::error_code_t::kRefuseReconnect),
                             "reconnect refused by server");
    return static_cast<int>(::atfw::gateway::error_code_t::kRefuseReconnect);
  }

  // Verify access data
  int ret = verify_access_data(body_handshake);
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "reconnect rsp: access data verification failed");
    return ret;
  }

  session_id_ = body_handshake.session_id();
  return dispatch_handshake_client_common(body_handshake);
}

int libatgw_protocol_sdk::dispatch_handshake_server_common(const ::atfw::gateway::v2::cs_body_handshake &body_handshake,
                                                           ::atfw::gateway::v2::handshake_step_t rsp_step) {
  // Shared server-side handshake logic for both key_exchange_req and reconn_req
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  // Server generates its own ECDH key pair
  uint64_t client_sequence = body_handshake.session_id();
  if (0 == client_sequence) {
    client_sequence = 1;
  }
  ret = crypto_session_->handshake_generate_self_key();
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "server: generate ECDH key pair failed");
    return ret;
  }
  crypto_session_->update_handshake(0);

  // Collect peer's algorithms
  using alg_type = crypto_algorithm_type;
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
        gsl::span<const alg_type>{global_cfg->ordered_algorithms_.data(), global_cfg->ordered_algorithms_.size()};
  }

  // Read peer's public key and derive shared secret
  if (body_handshake.public_key() != nullptr && body_handshake.public_key()->size() > 0) {
    ret = crypto_session_->handshake_read_peer_key(
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(body_handshake.public_key()->data()),
                                       body_handshake.public_key()->size()},
        gsl::span<const alg_type>{peer_algorithms.data(), peer_algorithms.size()}, local_alg_span, true);
    if (ret < 0) {
      ATFRAME_GATEWAY_ON_ERROR(ret, "server: read peer key and derive secret failed");
      return ret;
    }
  }

  // Negotiate compression algorithm (pick first locally-preferred one that peer also supports)
  if (body_handshake.compression_algorithm() != nullptr && global_cfg) {
    for (flatbuffers::uoffset_t j = 0; j < body_handshake.compression_algorithm()->size(); ++j) {
      auto remote_compression_algorithm = body_handshake.compression_algorithm()->Get(j);
      if (global_cfg->available_compression_algorithms_.end() !=
          global_cfg->available_compression_algorithms_.find(remote_compression_algorithm)) {
        crypto_session_->selected_compression_algorithm = remote_compression_algorithm;
        break;
      }
    }
  }

  // Set max_post_message_size
  crypto_session_->max_post_message_size =
      global_cfg ? global_cfg->conf_.max_post_message_size : (kDefaultMaxPostMessageSize);

  // Build response message
  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), alloc_seq());

  // Write server's public key
  gsl::span<const unsigned char> self_public_key = crypto_session_->get_handshake_self_public_key();

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

  // Generate server access data(使用老的token生成access_data)
  std::vector<flatbuffers::Offset<cs_body_handshake_access_data>> access_data_offsets;
  generate_access_data(builder, access_data_offsets, session_id_, crypto_session_->key_exchange_algorithm,
                       self_public_key, gsl::span<const unsigned char>{session_token_.data(), session_token_.size()});

  // Generate session token
  {
    auto token = random_buffer(kSessionTokenSize);
    session_token_.assign(reinterpret_cast<const unsigned char *>(token.data()),
                          reinterpret_cast<const unsigned char *>(token.data()) + token.size());
  }
  // Token加密传输,数据量小,不用压缩
  flatbuffers::Offset<cs_body_post_crypto> initialize_crypto_offset;
  flatbuffers::Offset<flatbuffers::Vector<int8_t>> session_token_offset;
  if (crypto_session_->get_current_send_cipher() &&
      crypto_session_->selected_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
    gsl::span<const unsigned char> encrypted_token;
    std::unique_ptr<unsigned char[]> heap_buffer;
    atfw::util::nostd::string_view output_aad;
    gsl::span<const unsigned char> output_iv;
    std::vector<unsigned char> compression_heap_buffer;

    crypto_session_->encrypt_data(gsl::span<const unsigned char>{session_token_.data(), session_token_.size()},
                                  encrypted_token, compression_heap_buffer, heap_buffer, output_iv, output_aad);
    initialize_crypto_offset = Createcs_body_post_crypto(
        builder, crypto_session_->selected_algorithm,
        builder.CreateVector(reinterpret_cast<const int8_t *>(output_iv.data()), output_iv.size()),
        builder.CreateVector(reinterpret_cast<const int8_t *>(output_aad.data()), output_aad.size()));

    session_token_offset =
        builder.CreateVector(reinterpret_cast<const int8_t *>(encrypted_token.data()), encrypted_token.size());
  } else {
    session_token_offset =
        builder.CreateVector(reinterpret_cast<const int8_t *>(session_token_.data()), session_token_.size());
  }
  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, session_id_, rsp_step, crypto_session_->key_exchange_algorithm, builder.CreateVector(selected_kdfs),
      builder.CreateVector(selected_algs), builder.CreateVector(access_data_offsets),
      builder.CreateVector(reinterpret_cast<const int8_t *>(self_public_key.data()), self_public_key.size()),
      initialize_crypto_offset,
      builder.CreateVector(selected_comp_algs),  // compression_algorithm (selected)
      crypto_session_->max_post_message_size, session_token_offset,
      crypto_session_->get_handshake_sequence_id());  // session_token

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

int libatgw_protocol_sdk::dispatch_handshake_client_common(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  // Shared client-side handshake logic for both key_exchange_rsp and reconn_rsp

  // Collect server's selected algorithms
  using alg_type = crypto_algorithm_type;
  std::vector<alg_type> server_algorithms;
  if (body_handshake.algorithms() != nullptr) {
    for (flatbuffers::uoffset_t i = 0; i < body_handshake.algorithms()->size(); ++i) {
      server_algorithms.push_back(body_handshake.algorithms()->Get(i));
    }
  }

  // Get local algorithms
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
  gsl::span<const alg_type> local_alg_span;
  if (global_cfg) {
    local_alg_span =
        gsl::span<const alg_type>{global_cfg->ordered_algorithms_.data(), global_cfg->ordered_algorithms_.size()};
  }

  // update handshake state with server's sequence number (for replay protection and future handshake updates)
  crypto_session_->update_handshake(body_handshake.handshake_sequence());

  // Read server's public key and derive shared secret
  int ret = 0;
  if (body_handshake.public_key() != nullptr && body_handshake.public_key()->size() > 0) {
    ret = crypto_session_->handshake_read_peer_key(
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(body_handshake.public_key()->data()),
                                       body_handshake.public_key()->size()},
        gsl::span<const alg_type>{server_algorithms.data(), server_algorithms.size()}, local_alg_span, false);
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

  // Store session token if provided by server
  if (body_handshake.session_token() != nullptr && body_handshake.session_token()->size() > 0) {
    // Token加密传输,数据量小,不用压缩。这里需要解密
    if (crypto_session_->get_current_receive_cipher() &&
        crypto_session_->selected_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone)) {
      const auto *initialize_crypto = body_handshake.initialize_crypto();
      gsl::span<const unsigned char> iv;
      atfw::util::nostd::string_view aad;
      if (initialize_crypto != nullptr) {
        if (initialize_crypto->iv() != nullptr) {
          iv = gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(initialize_crypto->iv()->data()),
                                              initialize_crypto->iv()->size()};
        }
        if (initialize_crypto->aad() != nullptr) {
          aad = atfw::util::nostd::string_view{reinterpret_cast<const char *>(initialize_crypto->aad()->data()),
                                               initialize_crypto->aad()->size()};
        }
      }

      gsl::span<const unsigned char> out;
      std::unique_ptr<unsigned char[]> heap_buffer;
      crypto_session_->decrypt_data(gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(
                                                                       body_handshake.session_token()->data()),
                                                                   body_handshake.session_token()->size()},
                                    out, heap_buffer, iv, aad);
      session_token_.assign(reinterpret_cast<const unsigned char *>(out.data()),
                            reinterpret_cast<const unsigned char *>(out.data()) + out.size());
    } else {
      session_token_.assign(reinterpret_cast<const unsigned char *>(body_handshake.session_token()->data()),
                            reinterpret_cast<const unsigned char *>(body_handshake.session_token()->data()) +
                                body_handshake.session_token()->size());
    }
  }

  // Handshake complete on client side
  close_handshake(0);

  // send confirm to server
  ret = send_confirm();
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "client: send confirm failed");
    return ret;
  }
  return ret;
}

// ========================= Access Data (HMAC-SHA256 authentication) =========================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::generate_access_data(
    flatbuffers::FlatBufferBuilder &builder,
    std::vector<flatbuffers::Offset<::atfw::gateway::v2::cs_body_handshake_access_data>> &out, uint64_t session_id,
    protocol_key_exchange_t key_exchange, gsl::span<const unsigned char> public_key,
    gsl::span<const unsigned char> session_token) {
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
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

  std::string plaintext =
      make_access_data_plaintext(session_id, timestamp, nonce1, nonce2, key_exchange, public_key, session_token);

  for (const auto &token : global_cfg->conf_.access_tokens) {
    std::string signature =
        calculate_access_data_signature(gsl::span<const unsigned char>{token.data(), token.size()}, plaintext);

    auto sig_offset = builder.CreateVector(reinterpret_cast<const int8_t *>(signature.data()), signature.size());
    auto ad = ::atfw::gateway::v2::Createcs_body_handshake_access_data(
        builder, ::atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(access_data_algorithm_t, kHmacSha256),
        timestamp, nonce1, nonce2, sig_offset);
    out.push_back(ad);
  }

  return 0;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::verify_access_data(
    const ::atfw::gateway::v2::cs_body_handshake &body_handshake) {
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
  if (!global_cfg || global_cfg->conf_.access_tokens.empty()) {
    return 0;  // No access tokens configured, skip verification
  }

  if (body_handshake.access_data() == nullptr || body_handshake.access_data()->size() == 0) {
    return static_cast<int>(::atfw::gateway::error_code_t::kCryptoVerify);
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

    std::string plaintext = make_access_data_plaintext(
        body_handshake.session_id(), ad->timestamp(), ad->nonce1(), ad->nonce2(), body_handshake.key_exchange(),
        pub_key, gsl::span<const unsigned char>{session_token_.data(), session_token_.size()});

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

  return static_cast<int>(::atfw::gateway::error_code_t::kCryptoVerify);
}

LIBATGW_PROTOCOL_API std::string libatgw_protocol_sdk::make_access_data_plaintext(
    uint64_t session_id, int64_t timestamp, uint64_t nonce1, uint64_t nonce2, key_exchange_type key_exchange,
    gsl::span<const unsigned char> public_key, gsl::span<const unsigned char> session_token) {
  std::string result = atfw::util::string::format("{}:{}-{}:{}", timestamp, nonce1, nonce2, session_id);

  if (!public_key.empty()) {
    // Include key exchange type and SHA256 of public key
    std::string pub_key_hash = atfw::util::hash::sha::hash_to_hex(atfw::util::hash::sha::EN_ALGORITHM_SHA256,
                                                                  public_key.data(), public_key.size());
    result += atfw::util::string::format(":{}:{}", static_cast<int>(key_exchange), pub_key_hash);
  }

  if (!session_token.empty()) {
    // Include SHA256 of session token
    std::string token_hash = atfw::util::hash::sha::hash_to_hex(atfw::util::hash::sha::EN_ALGORITHM_SHA256,
                                                                session_token.data(), session_token.size());
    result += ":";
    result += token_hash;
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

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::setup_handshake(std::shared_ptr<crypto_shared_context_t> &shared_conf) {
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

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::handshake_update() {
  if (0 == session_id_) {
    return start_session();
  }
  // Key refresh: reconnect with the same session_id
  return reconnect_session(session_id_, std::vector<unsigned char>());
}

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
      static_cast<int>(crypto_session_->key_exchange_algorithm), static_cast<int>(crypto_session_->selected_algorithm),
      static_cast<int>(crypto_session_->selected_compression_algorithm), crypto_session_->max_post_message_size,
      check_flag(flag_t::kWriting), check_flag(flag_t::kClosing), check_flag(flag_t::kClosed),
      check_flag(flag_t::kHandshakeDone), check_flag(flag_t::kHandshakeUpdate));

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
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (0 != session_id_) {
    return static_cast<int>(::atfw::gateway::error_code_t::kSessionAlreadyExist);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  // Setup global config
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  // Generate client ECDH key pair
  ret = crypto_session_->handshake_generate_self_key();
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "client start_session: generate ECDH key failed");
    return ret;
  }

  // Get client's public key
  gsl::span<const unsigned char> self_public_key = crypto_session_->get_handshake_self_public_key();

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), alloc_seq());

  // All supported algorithms
  std::vector<crypto_algorithm_t> algs;
  if (global_cfg) {
    for (auto a : global_cfg->ordered_algorithms_) {
      algs.push_back(a);
    }
  }
  std::vector<kdf_algorithm_t> kdfs = {ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256)};

  // Compression algorithms
  std::vector<compression_algorithm_t> comp_algs;
  if (global_cfg) {
    for (auto ca : global_cfg->ordered_compression_algorithms_) {
      comp_algs.push_back(ca);
    }
  }

  // Generate access data
  std::vector<flatbuffers::Offset<cs_body_handshake_access_data>> access_data_offsets;
  generate_access_data(builder, access_data_offsets, 0, crypto_session_->key_exchange_algorithm, self_public_key, {});

  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, 0, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kKeyExchangeReq),
      crypto_session_->key_exchange_algorithm, builder.CreateVector(kdfs), builder.CreateVector(algs),
      builder.CreateVector(access_data_offsets),
      builder.CreateVector(reinterpret_cast<const int8_t *>(self_public_key.data()), self_public_key.size()),
      {},  // handshake will determine crypto parameters, so IV and tag size are not specified in request
      builder.CreateVector(comp_algs), crypto_session_->max_post_message_size,
      {},  // session token is only send by server to client, so not included in client's request
      0);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::reconnect_session(uint64_t sess_id,
                                                                 gsl::span<const unsigned char> session_token) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  // 必须握手完成才能进行重连
  if (!check_flag(flag_t::kHandshakeDone)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kHandshake);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  // Setup global config and generate ECDH key pair (same as start_session)
  std::shared_ptr<crypto_shared_context_t> global_cfg = shared_conf_;
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  // Generate client ECDH key pair
  ret = crypto_session_->handshake_generate_self_key();
  if (ret < 0) {
    ATFRAME_GATEWAY_ON_ERROR(ret, "client reconnect_session: generate ECDH key failed");
    return ret;
  }

  // Get client's public key
  gsl::span<const unsigned char> self_public_key = crypto_session_->get_handshake_self_public_key();

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kHandshake), alloc_seq());

  // All supported algorithms
  std::vector<crypto_algorithm_t> algs;
  if (global_cfg) {
    for (auto a : global_cfg->ordered_algorithms_) {
      algs.push_back(a);
    }
  }
  std::vector<kdf_algorithm_t> kdfs = {ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(kdf_algorithm_t, kHkdfSha256)};

  // Compression algorithms
  std::vector<compression_algorithm_t> comp_algs;
  if (global_cfg) {
    for (auto ca : global_cfg->ordered_compression_algorithms_) {
      comp_algs.push_back(ca);
    }
  }

  // Generate access data
  std::vector<flatbuffers::Offset<cs_body_handshake_access_data>> access_data_offsets;
  generate_access_data(builder, access_data_offsets, sess_id, crypto_session_->key_exchange_algorithm, self_public_key,
                       session_token);

  flatbuffers::Offset<cs_body_handshake> handshake_body = Createcs_body_handshake(
      builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, kReconnectReq),
      crypto_session_->key_exchange_algorithm, builder.CreateVector(kdfs), builder.CreateVector(algs),
      builder.CreateVector(access_data_offsets),
      builder.CreateVector(reinterpret_cast<const int8_t *>(self_public_key.data()), self_public_key.size()),
      {},  // handshake will determine crypto parameters, so IV and tag size are not specified in request
      builder.CreateVector(comp_algs), crypto_session_->max_post_message_size,
      {},  // session token is only send by server to client, so not included in client's request
      crypto_session_->get_handshake_sequence_id());

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_handshake),
                                      handshake_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

// ========================= Data send/receive =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(::atfw::gateway::v2::client_message_type_t msg_type,
                                                         gsl::span<const unsigned char> data) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (!check_flag(flag_t::kHandshakeDone)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kHandshake);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  // Enforce max_post_message_size
  if (crypto_session_->max_post_message_size > 0 && data.size() > crypto_session_->max_post_message_size) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMessageTooLarge);
  }

  size_t ori_len = data.size();
  gsl::span<const unsigned char> out;
  compression_algorithm_t compression_type = compression_algorithm_t::kNone;
  size_t compression_origin_size = 0;
  gsl::span<const unsigned char> crypto_iv;
  atfw::util::nostd::string_view crypto_aad;
  std::vector<unsigned char> compression_heap_buffer;
  std::unique_ptr<unsigned char[]> crypto_heap_buffer;
  // 输出 compression_type , compression_origin_size , iv, aad
  int res = encode_post(data, out, compression_type, compression_origin_size, crypto_iv, crypto_aad,
                        compression_heap_buffer, crypto_heap_buffer);
  if (0 != res) {
    return res;
  }

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(builder, msg_type, alloc_seq());

  flatbuffers::Offset<cs_body_post_crypto> crypto_offset = Createcs_body_post_crypto(
      builder, crypto_session_->selected_algorithm,
      builder.CreateVector(reinterpret_cast<const int8_t *>(crypto_iv.data()), crypto_iv.size()),
      builder.CreateVector(reinterpret_cast<const int8_t *>(crypto_aad.data()), crypto_aad.size()));

  flatbuffers::Offset<cs_body_post_compression> compression_offset =
      Createcs_body_post_compression(builder, compression_type, static_cast<uint64_t>(compression_origin_size));

  flatbuffers::Offset<cs_body_post> post_body =
      Createcs_body_post(builder, crypto_offset, compression_offset, static_cast<uint64_t>(ori_len),
                         builder.CreateVector(reinterpret_cast<const int8_t *>(out.data()), out.size()));

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_post), post_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(gsl::span<const unsigned char> data) {
  return send_post(::atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost), data);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_ping() {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  ping_.last_ping = ping_data_t::clk_t::now();

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPing), alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body = Createcs_body_ping(
      builder, static_cast<int64_t>(
                   std::chrono::duration_cast<std::chrono::microseconds>(ping_.last_ping.time_since_epoch()).count()));

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping), ping_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_pong(int64_t tp) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPong), alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body = Createcs_body_ping(builder, tp);

  builder.Finish(
      Createclient_message(builder, header_data,
                           ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_ping), ping_body.Union()),
      client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_kickoff(int32_t reason, int32_t sub_reason,
                                                            atfw::util::nostd::string_view message) {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kKickoff), alloc_seq());

  flatbuffers::Offset<flatbuffers::String> msg_offset;
  if (!message.empty()) {
    msg_offset = builder.CreateString(message);
  }

  flatbuffers::Offset<cs_body_kickoff> kickoff_body = Createcs_body_kickoff(builder, reason, sub_reason, msg_offset);

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_kickoff),
                                      kickoff_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_confirm() {
  if (check_flag(flag_t::kClosing)) {
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
  }

  using namespace ::atfw::gateway::v2;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<client_message_head> header_data = Createclient_message_head(
      builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kConfirm), alloc_seq());

  flatbuffers::Offset<cs_body_confirm> confirm_body =
      Createcs_body_confirm(builder, get_session_id(), crypto_session_->get_handshake_sequence_id());

  builder.Finish(Createclient_message(builder, header_data,
                                      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_body, cs_body_confirm),
                                      confirm_body.Union()),
                 client_messageIdentifier());
  return write_message(builder);
}

LIBATGW_PROTOCOL_API const libatgw_protocol_sdk::crypto_session_ptr_t &libatgw_protocol_sdk::get_crypto_session()
    const {
  return crypto_session_;
}

LIBATGW_PROTOCOL_API uint64_t libatgw_protocol_sdk::get_session_id() const noexcept { return session_id_; }

LIBATGW_PROTOCOL_API gsl::span<const unsigned char> libatgw_protocol_sdk::get_session_token() const noexcept {
  return gsl::span<const unsigned char>{session_token_.data(), session_token_.size()};
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_logger(atfw::util::log::log_wrapper::ptr_t logger) {
  logger_ = std::move(logger);
}

// ========================= Write infrastructure (preserved from original) =========================

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::try_write() {
  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return static_cast<int>(::atfw::gateway::error_code_t::kMissCallbacks);
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
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
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
    return write_done(static_cast<int>(::atfw::gateway::error_code_t::kNoData));
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
      return static_cast<int>(::atfw::gateway::error_code_t::kInvalidSize);
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
  return send_post(::atfw::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(client_message_type_t, kPost), data);
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
      return callbacks_->close_fn(this, close_reason_, close_sub_reason_, close_message_);
    }
  }

  return status;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int32_t reason, int32_t sub_reason,
                                                     atfw::util::nostd::string_view message) {
  return close(reason, true, sub_reason, message);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int32_t reason, bool is_send_kickoff, int32_t sub_reason,
                                                     atfw::util::nostd::string_view message) {
  if (check_flag(flag_t::kClosing)) {
    return 0;
  }
  close_reason_ = reason;
  close_sub_reason_ = sub_reason;
  close_message_ = std::string{message.data(), message.size()};

  if (is_send_kickoff) {
    send_kickoff(reason, sub_reason, message);
  }

  set_flag(flag_t::kClosing, true);

  if (!check_flag(flag_t::kWriting) && !check_flag(flag_t::kClosed)) {
    set_flag(flag_t::kClosed, true);

    if (nullptr != callbacks_ && callbacks_->close_fn) {
      return callbacks_->close_fn(this, close_reason_, sub_reason, message);
    }
  }

  return 0;
}

// ========================= Encode / Decode =========================
// Encode: compress first, then encrypt (like libatbus pack_message)

int libatgw_protocol_sdk::encode_post(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
                                      compression_algorithm_t &out_compression_algorithm,
                                      size_t &out_compression_origin_size, gsl::span<const unsigned char> &iv,
                                      atfw::util::nostd::string_view &aad,
                                      std::vector<unsigned char> &compression_heap_buffer,
                                      std::unique_ptr<unsigned char[]> &crypto_heap_buffer) {
  if (check_flag(flag_t::kClosing)) {
    out = in;
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  // Get compression config from shared_conf
  uint64_t comp_threshold = 0;
  auto comp_level = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kDefault);
  if (shared_conf_) {
    comp_threshold = shared_conf_->conf_.compression_threshold_size;
    comp_level = shared_conf_->conf_.compression_level;
  }

  // Step 1: Compress
  gsl::span<const unsigned char> compressed;
  int ret = crypto_session_->compress_data(in, compressed, compression_heap_buffer, comp_level, comp_threshold);
  if (ret < 0) {
    return ret;
  }
  if (!compressed.empty() && compressed.size() < in.size() && compressed != in) {
    out_compression_algorithm = crypto_session_->selected_compression_algorithm;
    out_compression_origin_size = compressed.size();
    in = compressed;
  } else {
    out_compression_algorithm = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone);
    out_compression_origin_size = in.size();
  }

  // Step 2: Encrypt the (possibly compressed) data
  ret = crypto_session_->encrypt_data(in, out, compression_heap_buffer, crypto_heap_buffer, iv, aad);
  return ret;
}

// Decode: decrypt first, then decompress (like libatbus unpack_message)

int libatgw_protocol_sdk::decode_post(gsl::span<const unsigned char> in, size_t original_size,
                                      gsl::span<const unsigned char> &out,
                                      compression_algorithm_t compression_algorithm, size_t compression_origin_size,
                                      gsl::span<const unsigned char> iv, atfw::util::nostd::string_view aad,
                                      std::vector<unsigned char> &compression_heap_buffer,
                                      std::unique_ptr<unsigned char[]> &crypto_heap_buffer) {
  if (check_flag(flag_t::kClosing)) {
    out = in;
    return static_cast<int>(::atfw::gateway::error_code_t::kClosing);
  }

  // Step 1: Decrypt
  gsl::span<const unsigned char> decrypted;
  int ret = crypto_session_->decrypt_data(in, decrypted, crypto_heap_buffer, iv, aad);
  if (ret < 0) {
    out = in;
    return ret;
  }
  in = decrypted;

  // Step 2: Decompress -- only if compression was actually applied.
  if (compression_algorithm != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone)) {
    if (in.size() > compression_origin_size) {
      in = in.subspan(0, compression_origin_size);
    }

    gsl::span<const unsigned char> decompressed;
    ret = crypto_session_->decompress_data(in, original_size, decompressed, compression_heap_buffer);
    if (ret < 0) {
      out = in;
      return ret;
    }
    out = decompressed;
  } else {
    out = in;
  }

  // 可能有padding，去掉多余的padding部分
  if (out.size() > original_size) {
    out = out.subspan(0, original_size);
  }
  return ret;
}

// ========================= Algorithm name ↔ enum helpers =========================

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::key_exchange_type libatgw_protocol_sdk::key_exchange_algorithm_from_name(
    atfw::util::nostd::string_view name) {
  const auto &all_supported = get_supported_key_exchanges();
  auto iter = all_supported.name_mapping.find(std::string{name});
  if (iter == all_supported.name_mapping.end()) {
    return ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kNone);
  }

  return iter->second;
}

LIBATGW_PROTOCOL_API const std::vector<std::string> &libatgw_protocol_sdk::get_all_key_exchange_algorithm_names() {
  static std::vector<std::string> names;
  if ATFW_UTIL_LIKELY_CONDITION (!names.empty()) {
    return names;
  }

  const auto &all_supported = get_supported_key_exchanges();
  names.reserve(all_supported.name_mapping.size());
  for (const auto &pair : all_supported.name_mapping) {
    names.push_back(pair.first);
  }

  return names;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_algorithm_type libatgw_protocol_sdk::crypto_algorithm_from_name(
    atfw::util::nostd::string_view name) {
  const auto &all_supported = get_supported_crypto_algorithms();
  auto iter = all_supported.name_mapping.find(std::string{name});
  if (iter == all_supported.name_mapping.end()) {
    return ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone);
  }

  return iter->second;
}

LIBATGW_PROTOCOL_API const std::vector<std::string> &libatgw_protocol_sdk::get_all_crypto_algorithm_names() {
  static std::vector<std::string> names;
  if ATFW_UTIL_LIKELY_CONDITION (!names.empty()) {
    return names;
  }

  const auto &all_supported = get_supported_crypto_algorithms();
  names.reserve(all_supported.name_mapping.size());
  for (const auto &pair : all_supported.name_mapping) {
    names.push_back(pair.first);
  }

  return names;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::compression_algorithm_type
libatgw_protocol_sdk::compression_algorithm_from_name(atfw::util::nostd::string_view name) {
  const auto &all_supported = get_supported_compression_algorithm();
  auto iter = all_supported.name_mapping.find(std::string{name});
  if (iter == all_supported.name_mapping.end()) {
    return ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone);
  }

  return iter->second;
}

LIBATGW_PROTOCOL_API const std::vector<std::string> &libatgw_protocol_sdk::get_all_compression_algorithm_names() {
  static std::vector<std::string> names;

  if ATFW_UTIL_LIKELY_CONDITION (!names.empty()) {
    return names;
  }

  const auto &all_supported = get_supported_compression_algorithm();
  names.reserve(all_supported.name_mapping.size());
  for (const auto &pair : all_supported.name_mapping) {
    names.push_back(pair.first);
  }

  return names;
}

// ========================= Create global configure =========================

LIBATGW_PROTOCOL_API std::shared_ptr<crypto_shared_context_t> libatgw_protocol_sdk::create_shared_context(
    crypto_conf_t &conf) {
  auto inst = std::make_shared<crypto_shared_context_t>(std::move(conf));
  if (!inst) {
    return nullptr;
  }

  int ret = inst->init();
  if (0 != ret) {
    return nullptr;
  }

  return inst;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypto_conf_t *libatgw_protocol_sdk::get_shared_context_mutable_conf(
    const std::shared_ptr<crypto_shared_context_t> &ctx) {
  if (!ctx) {
    return nullptr;
  }

  return &ctx->conf_;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::set_shared_context_key_exchange_algorithm(
    const std::shared_ptr<crypto_shared_context_t> &ctx, const key_exchange_type alg) {
  if (!ctx) {
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  return ctx->reload_key_exchange_algorithm(alg);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::set_shared_context_crypto_algorithm(
    const std::shared_ptr<crypto_shared_context_t> &ctx, gsl::span<const crypto_algorithm_type> alg) {
  if (!ctx) {
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  return ctx->reload_crypto_algorithm(alg);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::set_shared_context_compression_algorithm(
    const std::shared_ptr<crypto_shared_context_t> &ctx, gsl::span<const compression_algorithm_type> alg) {
  if (!ctx) {
    return static_cast<int>(::atfw::gateway::error_code_t::kParam);
  }

  return ctx->reload_compression_algorithm(alg);
}

}  // namespace v2
}  // namespace gateway
}  // namespace atframework
