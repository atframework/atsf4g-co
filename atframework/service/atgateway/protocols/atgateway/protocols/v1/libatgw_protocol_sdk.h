// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "algorithm/crypto_cipher.h"
#include "algorithm/crypto_dh.h"
#include "algorithm/xxtea.h"
#include "detail/buffer.h"
#include "log/log_wrapper.h"

#include "atgateway/protocols/libatgw_protocol_api.h"

// MSVC hack
#ifdef _MSC_VER
#  ifdef max
#    undef max
#  endif

#  ifdef min
#    undef min
#  endif
#endif

#include "atgateway/protocols/v1/libatgw_protocol_sdk_generated.h"

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
inline namespace v1 {
namespace detail {
struct crypt_global_configure_t;
}

class libatgw_protocol_sdk : public libatgw_protocol_api {
 public:
  /**
   * @brief crypt configure
   * @note default reuse the definition of inner protocol, if it's useful for other protocol depends other protocol's
   * implement
   * @see protocols/inner_v1/libatgw_proto_inner.fbs
   */
  struct ATFW_UTIL_SYMBOL_VISIBLE crypt_conf_t {
    std::string default_key; /** default key, different used for different crypt protocol **/
    time_t update_interval;  /** crypt key refresh interval **/
    std::string type;        /** crypt type. XXTEA, AES and etc. **/
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gw::v1, switch_secret_t)
    switch_secret_type; /** how to generate the secret key, dh, rsa or direct send. recommander to use DH **/

    // Not supported now
    // int rsa_sign_type;           /** RSA sign type. PKCS1, PKCS1_V15 or PSS **/
    // int hash_id;                 /** hash id, md5,sha1,sha256,sha512 **/
    // std::string rsa_public_key;  /** RSA public key file path. **/
    // std::string rsa_private_key; /** RSA private key file path. **/
    std::string dh_param; /** DH parameter file path. **/

    bool client_mode; /** client mode, must be false in server when call global_reload(cfg) **/
  };

  struct crypt_session_t {
    std::shared_ptr<detail::crypt_global_configure_t> shared_conf;
    std::string type;                  /** crypt type. XXTEA, AES and etc. **/
    std::vector<unsigned char> secret; /** crypt secret. **/

    std::vector<unsigned char> param; /** cache data used for generate key, dhparam if using DH algorithm. **/

    LIBATGW_PROTOCOL_API crypt_session_t();
    LIBATGW_PROTOCOL_API ~crypt_session_t();

    LIBATGW_PROTOCOL_API int setup(const std::string &t);
    LIBATGW_PROTOCOL_API void close();
    LIBATGW_PROTOCOL_API int generate_secret(int &libres, const atfw::util::log::log_wrapper::ptr_t &logger);
    LIBATGW_PROTOCOL_API int swap_secret(std::vector<unsigned char> &in, int &libres,
                                         const atfw::util::log::log_wrapper::ptr_t &logger);

    atfw::util::crypto::cipher cipher;
    bool is_inited_;
  };
  using crypt_session_ptr_t = std::shared_ptr<crypt_session_t>;

  // ping/pong
  struct ATFW_UTIL_SYMBOL_VISIBLE ping_data_t {
    using clk_t = std::chrono::system_clock;
    clk_t::time_point last_ping;
    time_t last_delta;
  };

 public:
  LIBATGW_PROTOCOL_API libatgw_protocol_sdk();
  LIBATGW_PROTOCOL_API ~libatgw_protocol_sdk();

  LIBATGW_PROTOCOL_API void alloc_recv_buffer(size_t suggested_size, char *&out_buf, size_t &out_len) override;
  LIBATGW_PROTOCOL_API void read(int ssz, const char *buff, size_t len, int &errcode) override;

  LIBATGW_PROTOCOL_API void dispatch_data(const char *buff, size_t len, int errcode);
  LIBATGW_PROTOCOL_API int dispatch_handshake(const ::atframework::gw::v1::cs_body_handshake &body_handshake);

  LIBATGW_PROTOCOL_API int dispatch_handshake_start_req(const ::atframework::gw::v1::cs_body_handshake &body_handshake);
  LIBATGW_PROTOCOL_API int dispatch_handshake_start_rsp(const ::atframework::gw::v1::cs_body_handshake &body_handshake);
  LIBATGW_PROTOCOL_API int dispatch_handshake_reconn_req(
      const ::atframework::gw::v1::cs_body_handshake &body_handshake);
  LIBATGW_PROTOCOL_API int dispatch_handshake_reconn_rsp(
      const ::atframework::gw::v1::cs_body_handshake &body_handshake);
  LIBATGW_PROTOCOL_API int dispatch_handshake_dh_pubkey_req(
      const ::atframework::gw::v1::cs_body_handshake &body_handshake,
      ::atframework::gw::v1::handshake_step_t next_step);
  LIBATGW_PROTOCOL_API int dispatch_handshake_dh_pubkey_rsp(
      const ::atframework::gw::v1::cs_body_handshake &body_handshake);
  LIBATGW_PROTOCOL_API int dispatch_handshake_verify_ntf(
      const ::atframework::gw::v1::cs_body_handshake &body_handshake);

  LIBATGW_PROTOCOL_API int pack_handshake_start_rsp(
      flatbuffers::FlatBufferBuilder &builder, uint64_t sess_id, std::string &crypt_type,
      flatbuffers::Offset< ::atframework::gw::v1::cs_body_handshake> &handshake_body);
  LIBATGW_PROTOCOL_API int pack_handshake_dh_pubkey_req(
      flatbuffers::FlatBufferBuilder &builder, const ::atframework::gw::v1::cs_body_handshake &peer_body,
      flatbuffers::Offset< ::atframework::gw::v1::cs_body_handshake> &handshake_body,
      ::atframework::gw::v1::handshake_step_t next_step);

  LIBATGW_PROTOCOL_API int try_write();
  LIBATGW_PROTOCOL_API int write_msg(flatbuffers::FlatBufferBuilder &builder);
  LIBATGW_PROTOCOL_API int write(const void *buffer, size_t len) override;
  LIBATGW_PROTOCOL_API int write_done(int status) override;

  LIBATGW_PROTOCOL_API int close(int reason) override;
  LIBATGW_PROTOCOL_API int close(int reason, bool is_send_kickoff);

  LIBATGW_PROTOCOL_API int setup_handshake(std::shared_ptr<detail::crypt_global_configure_t> &shared_conf);
  LIBATGW_PROTOCOL_API void close_handshake(int status);

  LIBATGW_PROTOCOL_API bool check_reconnect(const libatgw_protocol_api *other) override;

  LIBATGW_PROTOCOL_API void set_recv_buffer_limit(size_t max_size, size_t max_number) override;
  LIBATGW_PROTOCOL_API void set_send_buffer_limit(size_t max_size, size_t max_number) override;

  LIBATGW_PROTOCOL_API int handshake_update() override;

  LIBATGW_PROTOCOL_API std::string get_info() const override;

  LIBATGW_PROTOCOL_API int start_session(const std::string &crypt_type);
  LIBATGW_PROTOCOL_API int reconnect_session(uint64_t sess_id, const std::string &crypt_type,
                                             const std::vector<unsigned char> &secret);

  LIBATGW_PROTOCOL_API int send_post(::atframework::gw::v1::cs_msg_type_t msg_type, const void *buffer, size_t len);
  LIBATGW_PROTOCOL_API int send_post(const void *buffer, size_t len);
  LIBATGW_PROTOCOL_API int send_ping();
  LIBATGW_PROTOCOL_API int send_pong(int64_t tp);
  LIBATGW_PROTOCOL_API int send_key_syn();
  LIBATGW_PROTOCOL_API int send_kickoff(int reason);
  LIBATGW_PROTOCOL_API int send_verify(const void *buf, size_t sz);

  ATFW_UTIL_FORCEINLINE const ping_data_t &get_last_ping() const { return ping_; }

  LIBATGW_PROTOCOL_API const crypt_session_ptr_t &get_crypt_read() const;
  LIBATGW_PROTOCOL_API const crypt_session_ptr_t &get_crypt_write() const;
  LIBATGW_PROTOCOL_API const crypt_session_ptr_t &get_crypt_handshake() const;

  ATFW_UTIL_FORCEINLINE uint64_t get_session_id() const { return session_id_; }

  LIBATGW_PROTOCOL_API void set_logger(atfw::util::log::log_wrapper::ptr_t logger);

 private:
  int encode_post(const void *in, size_t insz, const void *&out, size_t &outsz);
  int decode_post(const void *in, size_t insz, const void *&out, size_t &outsz);

  int encrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out, size_t &outsz);
  int decrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out, size_t &outsz);

 public:
  static LIBATGW_PROTOCOL_API int global_reload(crypt_conf_t &crypt_conf);

 private:
  uint64_t session_id_;
  ::atbus::detail::buffer_manager read_buffers_;
  /**
   * @brief 由于大多数数据包都比较小
   *        当数据包比较小时和动态直接放在动态int的数据包一起，这样可以减少内存拷贝次数
   */
  struct read_head_t {
    char buffer[ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE];  // 小数据包存储区
    size_t len;                                              // 小数据包存储区已使用长度
  };
  read_head_t read_head_;

  ::atbus::detail::buffer_manager write_buffers_;
  const void *last_write_ptr_;
  int close_reason_;

  // crypt option
  crypt_session_ptr_t crypt_read_;
  crypt_session_ptr_t crypt_write_;
  crypt_session_ptr_t crypt_handshake_;

  // ping data
  ping_data_t ping_;

  // used for handshake
  struct handshake_t {
    ATFRAMEWORK_GATEWAY_MACRO_ENUM_STORAGE_TYPE(::atframework::gw::v1, switch_secret_t) switch_secret_type;
    bool has_data;
    const void *ext_data;
    atfw::util::crypto::dh dh_ctx;
  };
  handshake_t handshake_;

  // logger
  atfw::util::log::log_wrapper::ptr_t logger_;
};
}  // namespace v1
}  // namespace gateway
}  // namespace atframework
