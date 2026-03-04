// Copyright 2026 atframework
// Refactored: 2-message ECDH handshake (like libatbus node_register_req/rsp)

#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "algorithm/compression.h"
#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "detail/buffer.h"
#include "log/log_wrapper.h"

#include "atgateway/protocol/libatgw_protocol_api.h"

// MSVC hack
#ifdef _MSC_VER
#  ifdef max
#    undef max
#  endif

#  ifdef min
#    undef min
#  endif
#endif

#include "atgateway/protocol/v2/libatgw_protocol_sdk_generated.h"

// By now, message header may cost about 150 bytes, some special message may cost more. we hope one
// connection will cost no more than 8KB, so 100K connections will cost no more than 800MB memory so we use 7KB for
// small message buffer, and left about 800 Bytes in future use. This can be 512 or smaller (but not smaller than 1KB),
#ifndef ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE
#  define ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE 7168
#endif

#if defined(ATFRAMEWORK_GATEWAY_MACRO_FLATC_USE_SCOPE_ENUM) && ATFRAMEWORK_GATEWAY_MACRO_FLATC_USE_SCOPE_ENUM
#  define ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(__ORIGIN, __VALUE) __ORIGIN::__VALUE
#  define ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(__NAMESPACE, __ORIGIN) __NAMESPACE::__ORIGIN
#else
#  define ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(__ORIGIN, __VALUE) __ORIGIN##_##__VALUE
#  define ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(__NAMESPACE, __ORIGIN) int
#endif

namespace atframework {
namespace gateway {
namespace v2 {
namespace detail {
struct crypto_global_configure_t;
}

class libatgw_protocol_sdk : public libatgw_protocol_api {
 public:
  /**
   * @brief Crypto configuration for the gateway protocol.
   * @note Uses ECDH key exchange exclusively (like libatbus).
   */
  struct ATFW_UTIL_SYMBOL_VISIBLE crypto_conf_t {
    /// Access tokens for HMAC-SHA256 authentication (multiple for rolling rotation)
    std::vector<std::vector<unsigned char>> access_tokens;

    /// Key refresh interval (seconds). Re-runs handshake to rotate keys periodically.
    time_t update_interval;

    /// ECDH key exchange algorithm (curve selection)
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange_algorithm;

    /// Supported crypto algorithms for session data encryption (ordered by preference)
    std::vector<ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
        supported_algorithms;

    /// Supported compression algorithms (ordered by preference)
    std::vector<ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, compression_algorithm_t)>
        compression_algorithms;

    /// Supported KDF algorithms (ordered by preference)
    std::vector<ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, kdf_algorithm_t)>
        supported_kdf_algorithms;

    /// Maximum post message size in bytes (default 2MB, sent to client during handshake)
    uint64_t max_post_message_size;

    /// Whether this is client mode (vs server mode)
    bool client_mode;

    /// @brief Initialize to default values (x25519, 2MB max message, 300s update interval)
    LIBATGW_PROTOCOL_API void set_default();
  };

  /**
   * @brief Per-connection crypto session state (like libatbus connection_context).
   * @note Owns the cipher pair for encrypt/decrypt, the handshake DH context, and compression state.
   */
  struct ATFW_UTIL_SYMBOL_VISIBLE crypto_session_t {
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::shared_ptr<detail::crypto_global_configure_t> shared_conf;

    /// Selected crypto algorithm after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t) selected_algorithm;

    /// Selected KDF type after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, kdf_algorithm_t) selected_kdf;

    /// Selected key exchange algorithm
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange_algorithm;

    /// Selected compression algorithm after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, compression_algorithm_t)
    selected_compression_algorithm;

    /// Maximum post message size in bytes (negotiated during handshake)
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    uint64_t max_post_message_size;

    LIBATGW_PROTOCOL_API crypto_session_t();
    LIBATGW_PROTOCOL_API ~crypto_session_t();

    /**
     * @brief Generate ECDH key pair for handshake
     * @param peer_sequence_id peer sequence if server mode, 0 if client mode
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int handshake_generate_self_key(uint64_t peer_sequence_id);

    /**
     * @brief Read peer's public key and derive shared secret + setup ciphers
     * @param peer_public_key peer's ECDH public key bytes
     * @param peer_algorithms peer's supported algorithm list
     * @param local_algorithms local supported algorithm list
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int handshake_read_peer_key(
        gsl::span<const unsigned char> peer_public_key,
        gsl::span<const ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
            peer_algorithms,
        gsl::span<const ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t)>
            local_algorithms);

    /**
     * @brief Write self public key to output buffer
     * @param out_public_key output buffer for public key
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int handshake_write_self_public_key(std::vector<unsigned char> &out_public_key);

    /**
     * @brief Setup crypto with explicit key (for testing, bypasses DH)
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int setup_crypto_with_key(
        ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t) algorithm,
        const unsigned char *key, size_t key_size, const unsigned char *iv, size_t iv_size);

    LIBATGW_PROTOCOL_API void close();

    /// Check if handshake DH is initialized
    ATFW_UTIL_FORCEINLINE bool has_handshake_data() const { return handshake_dh_ != nullptr; }

    /// Get handshake sequence ID
    ATFW_UTIL_FORCEINLINE uint64_t get_handshake_sequence_id() const { return handshake_sequence_id_; }

    /// Encrypt data using send cipher
    LIBATGW_PROTOCOL_API int encrypt_data(gsl::span<const unsigned char> in, gsl::span<unsigned char> &out);

    /// Decrypt data using receive cipher
    LIBATGW_PROTOCOL_API int decrypt_data(gsl::span<const unsigned char> in, gsl::span<unsigned char> &out);

    /// Compress data
    LIBATGW_PROTOCOL_API int compress_data(gsl::span<const unsigned char> in, gsl::span<unsigned char> &out);

    /// Decompress data
    LIBATGW_PROTOCOL_API int decompress_data(gsl::span<const unsigned char> in, size_t original_size,
                                             gsl::span<unsigned char> &out);

   private:
    int derive_key_from_shared_secret(const std::vector<unsigned char> &shared_secret);

    uint64_t handshake_sequence_id_;
    std::chrono::system_clock::time_point handshake_start_time_;
    std::vector<unsigned char> handshake_self_public_key_;
    ::atfw::util::crypto::dh::shared_context::ptr_t handshake_ctx_;
    std::unique_ptr<::atfw::util::crypto::dh> handshake_dh_;
    std::unique_ptr<::atfw::util::crypto::cipher> send_cipher_;
    std::unique_ptr<::atfw::util::crypto::cipher> receive_cipher_;
  };

  using crypto_session_ptr_t = std::shared_ptr<crypto_session_t>;

  /// @brief Backward compatibility aliases
  using crypt_session_t = crypto_session_t;
  using crypt_session_ptr_t = crypto_session_ptr_t;

  // ping/pong
  struct ATFW_UTIL_SYMBOL_VISIBLE ping_data_t {
    using clk_t = std::chrono::system_clock;
    clk_t::time_point last_ping;
    time_t last_delta = 0;
  };

 public:
  /**
   * @brief Construct with a shared global crypto configuration.
   * @param shared_conf shared pointer to global crypto configuration (created via create_global_configure)
   */
  LIBATGW_PROTOCOL_API explicit libatgw_protocol_sdk(std::shared_ptr<detail::crypto_global_configure_t> shared_conf);
  LIBATGW_PROTOCOL_API ~libatgw_protocol_sdk();

  LIBATGW_PROTOCOL_API void alloc_receive_buffer(size_t suggested_size, char *&out_buf, size_t &out_len) override;
  LIBATGW_PROTOCOL_API void read(int ssz, gsl::span<const unsigned char> buffer, int &errcode) override;

  LIBATGW_PROTOCOL_API void dispatch_data(gsl::span<const unsigned char> data, int errcode);

  /**
   * @brief Dispatch handshake message (new 2-message exchange)
   * @note kKeyExchangeReq → server processes, sends kKeyExchangeRsp
   *       kKeyExchangeRsp → client processes, handshake complete
   */
  LIBATGW_PROTOCOL_API int dispatch_handshake(const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  /// Server-side: handle kKeyExchangeReq from client
  LIBATGW_PROTOCOL_API int dispatch_handshake_key_exchange_req(
      const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  /// Client-side: handle kKeyExchangeRsp from server
  LIBATGW_PROTOCOL_API int dispatch_handshake_key_exchange_rsp(
      const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  /// Server-side: handle kReconnectReq from client
  LIBATGW_PROTOCOL_API int dispatch_handshake_reconn_req(
      const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  /// Client-side: handle kReconnectRsp from server
  LIBATGW_PROTOCOL_API int dispatch_handshake_reconn_rsp(
      const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  LIBATGW_PROTOCOL_API int try_write();
  LIBATGW_PROTOCOL_API int write_message(flatbuffers::FlatBufferBuilder &builder);
  LIBATGW_PROTOCOL_API int write(gsl::span<const unsigned char>) override;
  LIBATGW_PROTOCOL_API int write_done(int status) override;

  LIBATGW_PROTOCOL_API int close(int reason) override;
  LIBATGW_PROTOCOL_API int close(int reason, bool is_send_kickoff);

  LIBATGW_PROTOCOL_API int setup_handshake(std::shared_ptr<detail::crypto_global_configure_t> &shared_conf);
  LIBATGW_PROTOCOL_API void close_handshake(int status);

  LIBATGW_PROTOCOL_API bool check_reconnect(const libatgw_protocol_api *other) override;

  LIBATGW_PROTOCOL_API void set_receive_buffer_limit(size_t max_size, size_t max_number) override;
  LIBATGW_PROTOCOL_API void set_send_buffer_limit(size_t max_size, size_t max_number) override;

  LIBATGW_PROTOCOL_API int handshake_update() override;

  LIBATGW_PROTOCOL_API std::string get_info() const override;

  /**
   * @brief Client-side: start a new session by sending kKeyExchangeReq
   * @return 0 or error code
   */
  LIBATGW_PROTOCOL_API int start_session();

  LIBATGW_PROTOCOL_API int reconnect_session(uint64_t session_id, const std::vector<unsigned char> &session_token);

  LIBATGW_PROTOCOL_API int send_post(::atframework::gateway::v2::client_message_type_t message_type,
                                     gsl::span<const unsigned char> data);
  LIBATGW_PROTOCOL_API int send_post(gsl::span<const unsigned char> data);
  LIBATGW_PROTOCOL_API int send_ping();
  LIBATGW_PROTOCOL_API int send_pong(int64_t tp);
  LIBATGW_PROTOCOL_API int send_kickoff(int reason, int sub_reason = 0, const char *message = nullptr);

  ATFW_UTIL_FORCEINLINE const ping_data_t &get_last_ping() const { return ping_; }

  LIBATGW_PROTOCOL_API const crypto_session_ptr_t &get_crypto_session() const;

  ATFW_UTIL_FORCEINLINE uint64_t get_session_id() const { return session_id_; }

  LIBATGW_PROTOCOL_API void set_logger(atfw::util::log::log_wrapper::ptr_t logger);

  // ========== Access data authentication (like libatbus) ==========

  /**
   * @brief Generate access_data for handshake authentication
   * @param ad output FlatBuffers builder offset
   * @param builder FlatBuffers builder
   * @param session_id session ID (0 for new session req)
   * @param handshake_body_for_signing handshake body for HMAC (includes public key hash)
   * @return 0 or error code
   */
  LIBATGW_PROTOCOL_API int generate_access_data(
      flatbuffers::FlatBufferBuilder &builder,
      std::vector<flatbuffers::Offset<::atframework::gateway::v2::cs_body_handshake_access_data>> &out,
      uint64_t session_id,
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange,
      gsl::span<const unsigned char> public_key);

  /**
   * @brief Verify access_data from peer
   * @return 0 if verified, negative error code otherwise
   */
  LIBATGW_PROTOCOL_API int verify_access_data(const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  static LIBATGW_PROTOCOL_API std::string make_access_data_plaintext(
      uint64_t session_id, int64_t timestamp, uint64_t nonce1, uint64_t nonce2,
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) key_exchange,
      gsl::span<const unsigned char> public_key);

  static LIBATGW_PROTOCOL_API std::string calculate_access_data_signature(gsl::span<const unsigned char> access_token,
                                                                          const std::string &plaintext);

 private:
  int encode_post(gsl::span<const unsigned char> in, gsl::span<unsigned char> &out);
  int decode_post(gsl::span<const unsigned char> in, size_t original_size, gsl::span<unsigned char> &out);

 public:
  // ========== Algorithm name ↔ enum conversion helpers ==========

  /// @brief Convert key exchange algorithm name to enum value.
  /// @param name algorithm name (e.g. "x25519", "secp256r1")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t)
      key_exchange_algorithm_from_name(const char *name);

  /// @brief Convert key exchange enum to name string.
  static LIBATGW_PROTOCOL_API const char *key_exchange_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t) alg);

  /// @brief Get available key exchange algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<const char *> &get_all_key_exchange_algorithm_names();

  /// @brief Convert crypto algorithm name to enum value.
  /// @param name algorithm name (e.g. "aes-256-gcm", "chacha20-poly1305-ietf")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                          crypto_algorithm_t)
      crypto_algorithm_from_name(const char *name);

  /// @brief Convert crypto algorithm enum to name string.
  static LIBATGW_PROTOCOL_API const char *crypto_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, crypto_algorithm_t) alg);

  /// @brief Get available crypto algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<const char *> &get_all_crypto_algorithm_names();

  /// @brief Convert compression algorithm name to enum value.
  /// @param name algorithm name (e.g. "zstd", "lz4", "snappy", "zlib")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                          compression_algorithm_t)
      compression_algorithm_from_name(const char *name);

  /// @brief Convert compression algorithm enum to name string.
  static LIBATGW_PROTOCOL_API const char *compression_algorithm_to_name(
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, compression_algorithm_t) alg);

  /// @brief Get available compression algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<const char *> &get_all_compression_algorithm_names();

  /**
   * @brief Create a shared global crypto configuration from crypto_conf_t.
   * @param conf the configuration to use
   * @return shared_ptr to the global configure object
   */
  static LIBATGW_PROTOCOL_API std::shared_ptr<detail::crypto_global_configure_t> create_global_configure(
      crypto_conf_t &conf);

  /**
   * @brief Get a mutable pointer to the crypto_conf_t stored inside a global configure object.
   * @param global_conf the shared global configure
   * @return pointer to the mutable crypto_conf_t, or nullptr if global_conf is empty
   */
  static LIBATGW_PROTOCOL_API crypto_conf_t *get_global_configure_mutable_conf(
      const std::shared_ptr<detail::crypto_global_configure_t> &global_conf);

 private:
  std::shared_ptr<detail::crypto_global_configure_t> shared_conf_;
  uint64_t session_id_;
  ::atbus::detail::buffer_manager read_buffers_;
  /**
   * @brief Since most packets are small, when the packet is small we store it directly in
   *        the read_head buffer alongside the dynamic packet header to reduce memory copies.
   */
  struct read_head_t {
    unsigned char buffer[ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE];  // Small message buffer
    size_t len;                                                       // Used length of the small buffer
  };
  read_head_t read_head_;

  ::atbus::detail::buffer_manager write_buffers_;
  const void *last_write_ptr_;
  int close_reason_;

  // Single crypto session (like libatbus connection_context)
  crypto_session_ptr_t crypto_session_;

  // ping data
  ping_data_t ping_;

  // logger
  atfw::util::log::log_wrapper::ptr_t logger_;
};
}  // namespace v2

// ABI bridge: keep outer references stable without explicitly naming v2.
inline namespace abi_v2 {
using v2::libatgw_protocol_sdk;
}
using libatgw_protocol_sdk = v2::libatgw_protocol_sdk;

}  // namespace gateway
}  // namespace atframework
