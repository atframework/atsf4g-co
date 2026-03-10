// Copyright 2026 atframework
// Refactored: 2-message ECDH handshake (like libatbus node_register_req/rsp)

#pragma once

#include <chrono>
#include <gsl/pointers>
#include <memory>
#include <string>
#include <vector>

#include "algorithm/compression.h"
#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "detail/buffer.h"
#include "log/log_wrapper.h"

#include "atgateway/protocol/libatgw_protocol_api.h"
#include "nostd/nullability.h"
#include "nostd/string_view.h"

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
struct crypto_shared_context_t;

class libatgw_protocol_sdk : public libatgw_protocol_api {
 public:
  // ========== Type aliases to avoid using macros in public API ==========
  using key_exchange_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, key_exchange_t);
  using crypto_algorithm_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                            crypto_algorithm_t);
  using compression_algorithm_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                                 compression_algorithm_t);
  using kdf_algorithm_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2, kdf_algorithm_t);
  using compression_level_type = ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gateway::v2,
                                                                             compression_level_t);

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
    key_exchange_type key_exchange_algorithm;

    /// Supported crypto algorithms for session data encryption (ordered by preference)
    std::vector<crypto_algorithm_type> supported_algorithms;

    /// Supported compression algorithms (ordered by preference)
    std::vector<compression_algorithm_type> compression_algorithms;

    /// Supported KDF algorithms (ordered by preference)
    std::vector<kdf_algorithm_type> supported_kdf_algorithms;

    /// Maximum post message size in bytes (default 2MB, sent to client during handshake)
    uint64_t max_post_message_size;

    /// Compression threshold: messages smaller than this are not compressed (default 1024 bytes)
    uint64_t compression_threshold_size;

    /// Compression level (default: kDefault)
    compression_level_type compression_level;

    /// Whether this is client mode (vs server mode)
    bool client_mode;

    /// @brief Constructor - calls set_default()
    LIBATGW_PROTOCOL_API crypto_conf_t();

    /// @brief Copy constructor
    LIBATGW_PROTOCOL_API crypto_conf_t(const crypto_conf_t &other);

    /// @brief Move constructor
    LIBATGW_PROTOCOL_API crypto_conf_t(crypto_conf_t &&other) noexcept;

    /// @brief Copy assignment
    LIBATGW_PROTOCOL_API crypto_conf_t &operator=(const crypto_conf_t &other);

    /// @brief Move assignment
    LIBATGW_PROTOCOL_API crypto_conf_t &operator=(crypto_conf_t &&other) noexcept;

    /// @brief Destructor
    LIBATGW_PROTOCOL_API ~crypto_conf_t();

    /// @brief Initialize to default values (x25519, all algorithms by priority, 2MB max message, 300s update interval)
    LIBATGW_PROTOCOL_API void set_default();
  };

  /**
   * @brief Per-connection crypto session state (like libatbus connection_context).
   * @note Owns the cipher pair for encrypt/decrypt, the handshake DH context, and compression state.
   */
  struct ATFW_UTIL_SYMBOL_VISIBLE crypto_session_t {
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::shared_ptr<crypto_shared_context_t> shared_conf;

    /// Selected crypto algorithm after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    crypto_algorithm_type selected_algorithm;

    /// Selected KDF type after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    kdf_algorithm_type selected_kdf;

    /// Selected key exchange algorithm
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    key_exchange_type key_exchange_algorithm;

    /// Selected compression algorithm after negotiation
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    compression_algorithm_type selected_compression_algorithm;

    /// Maximum post message size in bytes (negotiated during handshake)
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    uint64_t max_post_message_size;

    LIBATGW_PROTOCOL_API crypto_session_t();
    LIBATGW_PROTOCOL_API ~crypto_session_t();

    /**
     * @brief Generate ECDH key pair for handshake
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int handshake_generate_self_key();

    /**
     * @brief Read peer's public key and derive shared secret + setup ciphers
     * @param peer_public_key peer's ECDH public key bytes
     * @param peer_algorithms peer's supported algorithm list
     * @param local_algorithms local supported algorithm list
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int handshake_read_peer_key(gsl::span<const unsigned char> peer_public_key,
                                                     gsl::span<const crypto_algorithm_type> peer_algorithms,
                                                     gsl::span<const crypto_algorithm_type> local_algorithms,
                                                     bool need_confirm);

    /**
     * @brief Get self public key to output buffer
     * @return span of the public key bytes
     */
    ATFW_UTIL_FORCEINLINE gsl::span<const unsigned char> get_handshake_self_public_key() const noexcept {
      return gsl::span<const unsigned char>{handshake_self_public_key_.data(), handshake_self_public_key_.size()};
    }

    /**
     * @brief Setup crypto with explicit key (for testing, bypasses DH)
     * @return 0 or error code
     */
    LIBATGW_PROTOCOL_API int setup_crypto_with_key(crypto_algorithm_type algorithm, gsl::span<const unsigned char> key,
                                                   gsl::span<const unsigned char> iv, bool need_confirm);

    LIBATGW_PROTOCOL_API void close();

    /// Check if handshake DH is initialized
    ATFW_UTIL_FORCEINLINE bool has_handshake_data() const { return handshake_dh_ != nullptr; }

    /// Get handshake sequence ID
    ATFW_UTIL_FORCEINLINE uint64_t get_handshake_sequence_id() const { return handshake_sequence_id_; }

    LIBATGW_PROTOCOL_API void update_handshake(uint64_t handshake_sequence_id);

    LIBATGW_PROTOCOL_API void confirm_handshake(uint64_t handshake_sequence_id);

    ATFW_UTIL_FORCEINLINE const std::unique_ptr<::atfw::util::crypto::cipher> &get_current_receive_cipher()
        const noexcept {
      return receive_cipher_;
    }

    ATFW_UTIL_FORCEINLINE const std::unique_ptr<::atfw::util::crypto::cipher> &get_current_send_cipher()
        const noexcept {
      return send_cipher_;
    }

    ATFW_UTIL_FORCEINLINE const std::unique_ptr<::atfw::util::crypto::cipher> &get_handshaking_receive_cipher()
        const noexcept {
      return handshaking_receive_cipher_;
    }

    /// Encrypt data using send cipher
    /// @param in input data
    /// @param out output span (may point into tls_buffer or heap_buffer)
    /// @param compression_heap_buffer heap buffer for compression (used if compression is applied before encryption)
    /// @param crypto_heap_buffer heap fallback buffer (populated if data exceeds TLS buffer)
    /// @param iv optional IV/nonce
    /// @param aad AAD for AEAD ciphers
    LIBATGW_PROTOCOL_API int encrypt_data(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
                                          std::vector<unsigned char> &compression_heap_buffer,
                                          std::unique_ptr<unsigned char[]> &crypto_heap_buffer,
                                          gsl::span<const unsigned char> &iv, atfw::util::nostd::string_view &aad);

    /// Decrypt data using receive cipher
    /// @param in input data
    /// @param out output span (may point into tls_buffer or heap_buffer)
    /// @param heap_buffer heap fallback buffer (populated if data exceeds TLS buffer)
    /// @param iv optional IV/nonce
    /// @param iv_size IV size in bytes
    /// @param aad optional AAD for AEAD
    LIBATGW_PROTOCOL_API int decrypt_data(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
                                          std::unique_ptr<unsigned char[]> &heap_buffer,
                                          gsl::span<const unsigned char> iv, atfw::util::nostd::string_view aad);

    /// Compress data
    /// @param in input data
    /// @param out output span (may point into tls_buffer or heap_buffer)
    /// @param heap_buffer heap fallback buffer (populated if data exceeds TLS buffer)
    /// @param level compression level
    /// @param threshold minimum size to compress (below this, data is passed through)
    LIBATGW_PROTOCOL_API int compress_data(
        gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out, std::vector<unsigned char> &heap_buffer,
        compression_level_type level = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kDefault),
        uint64_t threshold = 0);

    /// Decompress data
    /// @param in input data
    /// @param original_size original uncompressed size
    /// @param out output span (may point into tls_buffer or heap_buffer)
    /// @param heap_buffer heap fallback buffer (populated if data exceeds TLS buffer)
    LIBATGW_PROTOCOL_API int decompress_data(gsl::span<const unsigned char> in, size_t original_size,
                                             gsl::span<const unsigned char> &out,
                                             std::vector<unsigned char> &heap_buffer);

   private:
    int derive_key_from_shared_secret(const std::vector<unsigned char> &shared_secret, bool need_confirm);

    uint64_t handshake_sequence_id_;
    std::chrono::system_clock::time_point handshake_start_time_;
    std::vector<unsigned char> handshake_self_public_key_;
    ::atfw::util::crypto::dh::shared_context::ptr_t handshake_ctx_;
    std::unique_ptr<::atfw::util::crypto::dh> handshake_dh_;
    std::unique_ptr<::atfw::util::crypto::cipher> send_cipher_;
    std::unique_ptr<::atfw::util::crypto::cipher> receive_cipher_;
    std::unique_ptr<::atfw::util::crypto::cipher> handshaking_receive_cipher_;

    std::vector<unsigned char> send_iv_;
    std::string send_aad_;
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
   * @param shared_conf shared pointer to global crypto configuration (created via create_shared_context)
   */
  LIBATGW_PROTOCOL_API explicit libatgw_protocol_sdk(std::shared_ptr<crypto_shared_context_t> shared_conf);
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

  LIBATGW_PROTOCOL_API int close(int32_t reason, int32_t sub_reason = 0,
                                 atfw::util::nostd::string_view message = {}) override;
  LIBATGW_PROTOCOL_API int close(int32_t reason, bool is_send_kickoff, int32_t sub_reason,
                                 atfw::util::nostd::string_view message);

  LIBATGW_PROTOCOL_API int setup_handshake(std::shared_ptr<crypto_shared_context_t> &shared_conf);
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

  LIBATGW_PROTOCOL_API int reconnect_session(uint64_t session_id, gsl::span<const unsigned char> session_token);

  LIBATGW_PROTOCOL_API int send_post(::atframework::gateway::v2::client_message_type_t message_type,
                                     gsl::span<const unsigned char> data);
  LIBATGW_PROTOCOL_API int send_post(gsl::span<const unsigned char> data);
  LIBATGW_PROTOCOL_API int send_ping();
  LIBATGW_PROTOCOL_API int send_pong(int64_t tp);
  LIBATGW_PROTOCOL_API int send_kickoff(int32_t reason, int32_t sub_reason = 0,
                                        atfw::util::nostd::string_view message = {});
  LIBATGW_PROTOCOL_API int send_confirm();

  ATFW_UTIL_FORCEINLINE const ping_data_t &get_last_ping() const { return ping_; }

  LIBATGW_PROTOCOL_API const crypto_session_ptr_t &get_crypto_session() const;

  LIBATGW_PROTOCOL_API uint64_t get_session_id() const noexcept override;

  LIBATGW_PROTOCOL_API gsl::span<const unsigned char> get_session_token() const noexcept override;

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
      uint64_t session_id, key_exchange_type key_exchange, gsl::span<const unsigned char> public_key,
      gsl::span<const unsigned char> session_token);

  /**
   * @brief Verify access_data from peer
   * @return 0 if verified, negative error code otherwise
   */
  LIBATGW_PROTOCOL_API int verify_access_data(const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

  static LIBATGW_PROTOCOL_API std::string make_access_data_plaintext(uint64_t session_id, int64_t timestamp,
                                                                     uint64_t nonce1, uint64_t nonce2,
                                                                     key_exchange_type key_exchange,
                                                                     gsl::span<const unsigned char> public_key,
                                                                     gsl::span<const unsigned char> session_token);

  static LIBATGW_PROTOCOL_API std::string calculate_access_data_signature(gsl::span<const unsigned char> access_token,
                                                                          const std::string &plaintext);

 private:
  int encode_post(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out_buffer,
                  compression_algorithm_t &out_compression_algorithm, size_t &out_compression_origin_size,
                  gsl::span<const unsigned char> &iv, atfw::util::nostd::string_view &aad,
                  std::vector<unsigned char> &compression_heap_buffer,
                  std::unique_ptr<unsigned char[]> &crypto_heap_buffer);
  int decode_post(gsl::span<const unsigned char> in, size_t original_size, gsl::span<const unsigned char> &out,
                  compression_algorithm_t compression_algorithm, size_t compression_origin_size,
                  gsl::span<const unsigned char> iv, atfw::util::nostd::string_view aad,
                  std::vector<unsigned char> &compression_heap_buffer,
                  std::unique_ptr<unsigned char[]> &crypto_heap_buffer);

  /// Shared server-side handshake logic (key exchange & reconnect)
  int dispatch_handshake_server_common(const ::atframework::gateway::v2::cs_body_handshake &body_handshake,
                                       ::atframework::gateway::v2::handshake_step_t rsp_step);

  /// Shared client-side handshake logic (key exchange & reconnect)
  int dispatch_handshake_client_common(const ::atframework::gateway::v2::cs_body_handshake &body_handshake);

 public:
  // ========== Algorithm name ↔ enum conversion helpers ==========

  /// @brief Convert key exchange algorithm name to enum value.
  /// @param name algorithm name (e.g. "x25519", "secp256r1")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API key_exchange_type key_exchange_algorithm_from_name(atfw::util::nostd::string_view name);

  /// @brief Get available key exchange algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<std::string> &get_all_key_exchange_algorithm_names();

  /// @brief Convert crypto algorithm name to enum value.
  /// @param name algorithm name (e.g. "aes-256-gcm", "chacha20-poly1305-ietf")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API crypto_algorithm_type crypto_algorithm_from_name(atfw::util::nostd::string_view name);

  /// @brief Get available crypto algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<std::string> &get_all_crypto_algorithm_names();

  /// @brief Convert compression algorithm name to enum value.
  /// @param name algorithm name (e.g. "zstd", "lz4", "snappy", "zlib")
  /// @return enum value, or kNone if not found
  static LIBATGW_PROTOCOL_API compression_algorithm_type
  compression_algorithm_from_name(atfw::util::nostd::string_view name);

  /// @brief Get available compression algorithm names.
  static LIBATGW_PROTOCOL_API const std::vector<std::string> &get_all_compression_algorithm_names();

  /**
   * @brief Create a shared global crypto configuration from crypto_conf_t.
   * @param conf the configuration to use
   * @return shared_ptr to the global configure object
   */
  static LIBATGW_PROTOCOL_API std::shared_ptr<crypto_shared_context_t> create_shared_context(crypto_conf_t &conf);

  /**
   * @brief Get a mutable pointer to the crypto_conf_t stored inside a shared context.
   * @param ctx the shared context
   * @return pointer to the mutable crypto_conf_t, or nullptr if ctx is empty
   */
  static LIBATGW_PROTOCOL_API crypto_conf_t *get_shared_context_mutable_conf(
      const std::shared_ptr<crypto_shared_context_t> &ctx);

  /**
   * @brief Set the crypto algorithms for a shared context.
   * @param ctx the shared context
   * @param alg the key exchange algorithm to set
   * @return pointer to the mutable crypto_conf_t, or nullptr if ctx is empty
   */
  static LIBATGW_PROTOCOL_API int set_shared_context_key_exchange_algorithm(
      const std::shared_ptr<crypto_shared_context_t> &ctx, const key_exchange_type alg);

  /**
   * @brief Set the crypto algorithms for a shared context.
   * @param ctx the shared context
   * @param alg the list of crypto algorithms to set
   * @return pointer to the mutable crypto_conf_t, or nullptr if ctx is empty
   */
  static LIBATGW_PROTOCOL_API int set_shared_context_crypto_algorithm(
      const std::shared_ptr<crypto_shared_context_t> &ctx, gsl::span<const crypto_algorithm_type> alg);

  /**
   * @brief Set the compression algorithms for a shared context.
   * @param ctx the shared context
   * @param alg the list of compression algorithms to set
   * @return pointer to the mutable crypto_conf_t, or nullptr if ctx is empty
   */
  static LIBATGW_PROTOCOL_API int set_shared_context_compression_algorithm(
      const std::shared_ptr<crypto_shared_context_t> &ctx, gsl::span<const compression_algorithm_type> alg);

 private:
  std::shared_ptr<crypto_shared_context_t> shared_conf_;
  uint64_t session_id_;
  std::vector<unsigned char> session_token_;
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
  int32_t close_reason_;
  int32_t close_sub_reason_;
  std::string close_message_;

  // Single crypto session (like libatbus connection_context)
  atfw::util::nostd::nonnull<crypto_session_ptr_t> crypto_session_;

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
