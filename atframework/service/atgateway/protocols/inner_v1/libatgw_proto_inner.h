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

#include "../proto_base.h"

// MSVC hack
#ifdef _MSC_VER
#  ifdef max
#    undef max
#  endif

#  ifdef min
#    undef min
#  endif
#endif

#include "libatgw_proto_inner_generated.h"

#ifndef ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE
#  define ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE 3072
#endif

namespace atframe {
namespace gateway {
namespace detail {
struct crypt_global_configure_t;
}

class libatgw_proto_inner_v1 : public proto_base {
 public:
  /**
   * @brief crypt configure
   * @note default reuse the definition of inner ptotocol, if it's useful for other protocol depends other protocol's
   * implement
   * @see protocols/inner_v1/libatgw_proto_inner.fbs
   */
  struct crypt_conf_t {
    std::string default_key; /** default key, different used for different crypt protocol **/
    time_t update_interval;  /** crypt key refresh interval **/
    std::string type;        /** crypt type. XXTEA, AES and etc. **/
    int switch_secret_type;  /** how to generate the secret key, dh, rsa or direct send. recommander to use DH **/

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

    crypt_session_t();
    ~crypt_session_t();

    int setup(const std::string &t);
    void close();
    int generate_secret(int &libres);
    int swap_secret(std::vector<unsigned char> &in, int &libres);

    atfw::util::crypto::cipher cipher;
    bool is_inited_;
  };
  using crypt_session_ptr_t = std::shared_ptr<crypt_session_t>;

  // ping/pong
  struct ping_data_t {
    using clk_t = std::chrono::system_clock;
    clk_t::time_point last_ping;
    time_t last_delta;
  };

 public:
  libatgw_proto_inner_v1();
  virtual ~libatgw_proto_inner_v1();

  virtual void alloc_recv_buffer(size_t suggested_size, char *&out_buf, size_t &out_len);
  virtual void read(int ssz, const char *buff, size_t len, int &errcode);

  void dispatch_data(const char *buff, size_t len, int errcode);
  int dispatch_handshake(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);

  int dispatch_handshake_start_req(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);
  int dispatch_handshake_start_rsp(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);
  int dispatch_handshake_reconn_req(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);
  int dispatch_handshake_reconn_rsp(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);
  int dispatch_handshake_dh_pubkey_req(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake,
                                       ::atframe::gw::inner::v1::handshake_step_t next_step);
  int dispatch_handshake_dh_pubkey_rsp(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);
  int dispatch_handshake_verify_ntf(const ::atframe::gw::inner::v1::cs_body_handshake &body_handshake);

  int pack_handshake_start_rsp(flatbuffers::FlatBufferBuilder &builder, uint64_t sess_id, std::string &crypt_type,
                               flatbuffers::Offset< ::atframe::gw::inner::v1::cs_body_handshake> &handshake_body);
  int pack_handshake_dh_pubkey_req(flatbuffers::FlatBufferBuilder &builder,
                                   const ::atframe::gw::inner::v1::cs_body_handshake &peer_body,
                                   flatbuffers::Offset< ::atframe::gw::inner::v1::cs_body_handshake> &handshake_body,
                                   ::atframe::gw::inner::v1::handshake_step_t next_step);

  int try_write();
  int write_msg(flatbuffers::FlatBufferBuilder &builder);
  virtual int write(const void *buffer, size_t len);
  virtual int write_done(int status);

  virtual int close(int reason);
  int close(int reason, bool is_send_kickoff);

  int setup_handshake(std::shared_ptr<detail::crypt_global_configure_t> &shared_conf);
  void close_handshake(int status);

  virtual bool check_reconnect(const proto_base *other);

  virtual void set_recv_buffer_limit(size_t max_size, size_t max_number);
  virtual void set_send_buffer_limit(size_t max_size, size_t max_number);

  virtual int handshake_update();

  virtual std::string get_info() const;

  int start_session(const std::string &crypt_type);
  int reconnect_session(uint64_t sess_id, const std::string &crypt_type, const std::vector<unsigned char> &secret);

  int send_post(::atframe::gw::inner::v1::cs_msg_type_t msg_type, const void *buffer, size_t len);
  int send_post(const void *buffer, size_t len);
  int send_ping();
  int send_pong(int64_t tp);
  int send_key_syn();
  int send_kickoff(int reason);
  int send_verify(const void *buf, size_t sz);

  const ping_data_t &get_last_ping() const { return ping_; }

  const crypt_session_ptr_t &get_crypt_read() const;
  const crypt_session_ptr_t &get_crypt_write() const;
  const crypt_session_ptr_t &get_crypt_handshake() const;

  inline uint64_t get_session_id() const { return session_id_; }

 private:
  int encode_post(const void *in, size_t insz, const void *&out, size_t &outsz);
  int decode_post(const void *in, size_t insz, const void *&out, size_t &outsz);

  int encrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out, size_t &outsz);
  int decrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out, size_t &outsz);

 public:
  static int global_reload(crypt_conf_t &crypt_conf);

 private:
  uint64_t session_id_;
  ::atbus::detail::buffer_manager read_buffers_;
  /**
   * @brief 由于大多数数据包都比较小
   *        当数据包比较小时和动态直接放在动态int的数据包一起，这样可以减少内存拷贝次数
   */
  struct read_head_t {
    char buffer[ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE];  // 小数据包存储区
    size_t len;                                          // 小数据包存储区已使用长度
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
    int switch_secret_type;
    bool has_data;
    const void *ext_data;
    atfw::util::crypto::dh dh_ctx;
  };
  handshake_t handshake_;
};
}  // namespace gateway
}  // namespace atframe
