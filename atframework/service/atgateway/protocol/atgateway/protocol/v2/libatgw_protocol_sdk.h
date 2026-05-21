// Copyright 2026 atframework
// Refactored: 2-message ECDH handshake (like libatbus node_register_req/rsp)

#pragma once

#include <gsl/select-gsl.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "detail/buffer.h"
#include "log/log_wrapper.h"

#include "atgateway/protocol/libatgw_protocol_api.h"
#include "nostd/nullability.h"
#include "nostd/string_view.h"

// undef some maros by UE
#if defined(_WIN32) || defined(_WIN64)
#  pragma push_macro("GetObject")
#  ifdef GetObject
#    undef GetObject
#  endif
#  pragma push_macro("DeleteFile")
#  ifdef DeleteFile
#    undef DeleteFile
#  endif
#  pragma push_macro("CreateFile")
#  ifdef CreateFile
#    undef CreateFile
#  endif
#  pragma push_macro("max")
#  ifdef max
#    undef max
#  endif
#  pragma push_macro("min")
#  ifdef min
#    undef min
#  endif
#endif
// Unreal Engine will define these macros
#pragma push_macro("check")
#ifdef check
#  undef check
#endif
#pragma push_macro("verify")
#ifdef verify
#  undef verify
#endif
#pragma push_macro("cast")
#ifdef cast
#  undef cast
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

    /// Key refresh interval . Re-runs handshake to rotate keys periodically.
    std::chrono::microseconds key_refresh_interval;

    /// Ping interval.
    std::chrono::microseconds ping_interval;

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

  struct ATFW_UTIL_SYMBOL_VISIBLE crypto_session_internal_data_t;

  /**
   * @brief Per-connection crypto session state (like libatbus connection_context).
   * @note Owns the cipher pair for encrypt/decrypt, the handshake DH context, and compression state.
   *       Internal data is hidden behind a pimpl to avoid exposing crypto headers.
   */
  struct ATFW_UTIL_SYMBOL_VISIBLE crypto_session_t {
    LIBATGW_PROTOCOL_API crypto_session_t();
    LIBATGW_PROTOCOL_API ~crypto_session_t();

    // Non-copyable, movable
    crypto_session_t(const crypto_session_t &) = delete;
    crypto_session_t &operator=(const crypto_session_t &) = delete;
    LIBATGW_PROTOCOL_API crypto_session_t(crypto_session_t &&other) noexcept;
    LIBATGW_PROTOCOL_API crypto_session_t &operator=(crypto_session_t &&other) noexcept;

    // ========== Getters / Setters for negotiated state ==========

    LIBATGW_PROTOCOL_API const std::shared_ptr<crypto_shared_context_t> &get_shared_conf() const noexcept;
    LIBATGW_PROTOCOL_API void set_shared_conf(std::shared_ptr<crypto_shared_context_t> conf);

    LIBATGW_PROTOCOL_API crypto_algorithm_type get_selected_algorithm() const noexcept;
    LIBATGW_PROTOCOL_API void set_selected_algorithm(crypto_algorithm_type alg);

    LIBATGW_PROTOCOL_API kdf_algorithm_type get_selected_kdf() const noexcept;
    LIBATGW_PROTOCOL_API void set_selected_kdf(kdf_algorithm_type kdf);

    LIBATGW_PROTOCOL_API key_exchange_type get_key_exchange_algorithm() const noexcept;
    LIBATGW_PROTOCOL_API void set_key_exchange_algorithm(key_exchange_type ke);

    LIBATGW_PROTOCOL_API compression_algorithm_type get_selected_compression_algorithm() const noexcept;
    LIBATGW_PROTOCOL_API void set_selected_compression_algorithm(compression_algorithm_type alg);

    LIBATGW_PROTOCOL_API uint64_t get_max_post_message_size() const noexcept;
    LIBATGW_PROTOCOL_API void set_max_post_message_size(uint64_t size);

    // ========== Handshake operations ==========

    LIBATGW_PROTOCOL_API int handshake_generate_self_key();

    LIBATGW_PROTOCOL_API int handshake_read_peer_key(gsl::span<const unsigned char> peer_public_key,
                                                     gsl::span<const crypto_algorithm_type> peer_algorithms,
                                                     gsl::span<const crypto_algorithm_type> local_algorithms,
                                                     bool need_confirm);

    LIBATGW_PROTOCOL_API gsl::span<const unsigned char> get_handshake_self_public_key() const noexcept;

    LIBATGW_PROTOCOL_API int setup_crypto_with_key(crypto_algorithm_type algorithm, gsl::span<const unsigned char> key,
                                                   gsl::span<const unsigned char> iv, bool need_confirm);

    LIBATGW_PROTOCOL_API void close();

    LIBATGW_PROTOCOL_API bool has_handshake_data() const noexcept;

    LIBATGW_PROTOCOL_API uint64_t get_handshake_sequence_id() const noexcept;

    LIBATGW_PROTOCOL_API void update_handshake(uint64_t handshake_sequence_id);

    LIBATGW_PROTOCOL_API void confirm_handshake(uint64_t handshake_sequence_id);

    // ========== Cipher state queries ==========

    LIBATGW_PROTOCOL_API bool has_send_cipher() const noexcept;
    LIBATGW_PROTOCOL_API bool has_receive_cipher() const noexcept;
    LIBATGW_PROTOCOL_API bool has_handshaking_receive_cipher() const noexcept;

    // ========== Encrypt / Decrypt / Compress / Decompress ==========

    LIBATGW_PROTOCOL_API int encrypt_data(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
                                          std::vector<unsigned char> &compression_heap_buffer,
                                          std::unique_ptr<unsigned char[]> &crypto_heap_buffer,
                                          gsl::span<const unsigned char> &iv, atfw::util::nostd::string_view &aad);

    LIBATGW_PROTOCOL_API int decrypt_data(gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out,
                                          std::unique_ptr<unsigned char[]> &heap_buffer,
                                          gsl::span<const unsigned char> iv, atfw::util::nostd::string_view aad);

    LIBATGW_PROTOCOL_API int compress_data(
        gsl::span<const unsigned char> in, gsl::span<const unsigned char> &out, std::vector<unsigned char> &heap_buffer,
        compression_level_type level = ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kDefault),
        uint64_t threshold = 0);

    LIBATGW_PROTOCOL_API int decompress_data(gsl::span<const unsigned char> in, size_t original_size,
                                             gsl::span<const unsigned char> &out,
                                             std::vector<unsigned char> &heap_buffer);

   private:
    int derive_key_from_shared_secret(const std::vector<unsigned char> &shared_secret, bool need_confirm);

    std::shared_ptr<crypto_session_internal_data_t> internal_data_;
  };

  using crypto_session_ptr_t = std::shared_ptr<crypto_session_t>;

  /// @brief Backward compatibility aliases
  using crypt_session_t = crypto_session_t;
  using crypt_session_ptr_t = crypto_session_ptr_t;

  // ping/pong
  struct ATFW_UTIL_SYMBOL_VISIBLE ping_data_t {
    using clk_t = std::chrono::system_clock;
    clk_t::time_point last_ping;
    clk_t::time_point last_handshake;
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
  LIBATGW_PROTOCOL_API int start_session(gsl::span<const unsigned char> hash_data = {});

  LIBATGW_PROTOCOL_API int reconnect_session(uint64_t session_id, gsl::span<const unsigned char> session_token,
                                             gsl::span<const unsigned char> hash_data = {});

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

  LIBATGW_PROTOCOL_API void tick() noexcept override;

  LIBATGW_PROTOCOL_API gsl::span<const unsigned char> get_session_token() const noexcept override;

  LIBATGW_PROTOCOL_API gsl::span<const unsigned char> get_router_hash_data() const noexcept override;

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

  /// @brief Get the maximum message size allowed for untrusted connections (before handshake completion).
  /// @return maximum untrusted message size in bytes
  static LIBATGW_PROTOCOL_API uint64_t get_max_untrusted_message_size() noexcept;

 private:
  std::shared_ptr<crypto_shared_context_t> shared_conf_;
  uint64_t session_id_;
  std::vector<unsigned char> session_token_;
  std::vector<unsigned char> router_hash_data_;
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

#pragma pop_macro("cast")
#pragma pop_macro("verify")
#pragma pop_macro("check")
#if defined(_WIN32) || defined(_WIN64)
#  pragma pop_macro("min")
#  pragma pop_macro("max")
#  pragma pop_macro("CreateFile")
#  pragma pop_macro("DeleteFile")
#  pragma pop_macro("GetObject")
#endif
