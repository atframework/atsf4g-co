// Copyright 2025 atframework

#include "atgateway/protocols/v1/libatgw_protocol_sdk.h"

#include <gsl/select-gsl.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_set>

#include "algorithm/murmur_hash.h"
#include "common/string_oprs.h"
#include "lock/lock_holder.h"
#include "lock/seq_alloc.h"
#include "lock/spin_lock.h"

namespace atframework {
namespace gateway {
inline namespace v1 {
namespace detail {
template <class TCH = char>
static TCH tolower(TCH c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<TCH>(c - 'A' + 'a');
  }

  return c;
}

static std::string debug_to_hex(gsl::span<const uint8_t> s) {
  std::stringstream ss;
  atfw::util::string::dumphex(s.data(), s.size(), ss);
  return ss.str();
}

static uint64_t alloc_seq() {
  static atfw::util::lock::seq_alloc_u64 seq_alloc;
  uint64_t ret = seq_alloc.inc();
  while (0 == ret) {
    ret = seq_alloc.inc();
  }
  return ret;
}

struct crypt_global_configure_t {
  using ptr_t = std::shared_ptr<crypt_global_configure_t>;

  crypt_global_configure_t(const libatgw_protocol_sdk::crypt_conf_t &conf) : conf_(conf), inited_(false) {
    shared_dh_context_ = atfw::util::crypto::dh::shared_context::create();
  }
  ~crypt_global_configure_t() { close(); }

  int init() {
    int ret = 0;
    close();
    if (conf_.type.empty()) {
      inited_ = true;
      return ret;
    }

    switch (conf_.switch_secret_type) {
      case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH):
      case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH): {
        // do nothing in client mode
        if (conf_.client_mode || conf_.dh_param.empty()) {
          if (conf_.switch_secret_type ==
              ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH)) {
            shared_dh_context_->init(util::crypto::dh::method_t::EN_CDT_DH);
          } else {
            shared_dh_context_->init(util::crypto::dh::method_t::EN_CDT_ECDH);
          }
        } else {
          shared_dh_context_->init(conf_.dh_param.c_str());
        }

        break;
      }
      case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT): {
        break;
      }
      default: {
        return error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
        break;
      }
    }

    // init supported algorithms index
    if (!conf_.type.empty()) {
      std::pair<const char *, const char *> res;
      res.first = res.second = conf_.type.c_str();

      std::unordered_set<std::string> all_supported_type_set;
      const std::vector<std::string> &all_supported_type_list = atfw::util::crypto::cipher::get_all_cipher_names();
      for (size_t i = 0; i < all_supported_type_list.size(); ++i) {
        all_supported_type_set.insert(all_supported_type_list[i]);
      }

      while (nullptr != res.second) {
        res = atfw::util::crypto::cipher::ciphertok(res.second);

        if (nullptr != res.first && nullptr != res.second) {
          std::string cipher_type;
          cipher_type.assign(res.first, res.second);
          std::transform(cipher_type.begin(), cipher_type.end(), cipher_type.begin(), tolower<char>);
          if (all_supported_type_set.find(cipher_type) != all_supported_type_set.end()) {
            available_types_.insert(cipher_type);
          }
        }
      }
    }

    return ret;
  }

  void close() {
    if (!inited_) {
      return;
    }
    inited_ = false;
    available_types_.clear();
    shared_dh_context_->reset();
  }

  bool check_type(std::string &crypt_type) {
    if (crypt_type.empty()) {
      return true;
    }

    std::transform(crypt_type.begin(), crypt_type.end(), crypt_type.begin(), tolower<char>);
    return available_types_.find(crypt_type) != available_types_.end();
  }

  static void default_crypt_configure(libatgw_protocol_sdk::crypt_conf_t &dconf) {
    dconf.default_key = "atgw-key";
    dconf.dh_param.clear();
    dconf.switch_secret_type =
        ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT);
    dconf.type.clear();
    dconf.update_interval = 1200;
    dconf.client_mode = false;
  }

  libatgw_protocol_sdk::crypt_conf_t conf_;
  bool inited_;
  std::unordered_set<std::string> available_types_;
  atfw::util::crypto::dh::shared_context::ptr_t shared_dh_context_;

  static ptr_t &current() {
    static ptr_t ret;
    return ret;
  }
};
}  // namespace detail

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypt_session_t::crypt_session_t() : is_inited_(false) {}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::crypt_session_t::~crypt_session_t() { close(); }

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypt_session_t::setup(const std::string &t) {
  if (is_inited_) {
    return error_code_t::EN_ECT_CRYPT_ALREADY_INITED;
  }

  if (!t.empty()) {
    if (cipher.init(t.c_str()) < 0) {
      return error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
    }

    size_t kb = cipher.get_key_bits();
    if (secret.size() * 8 < kb) {
      secret.resize(kb / 8, 0);
    }

    if (secret.size() * 8 > kb) {
      secret.resize(kb / 8);
    }
  } else {
    secret.clear();
    cipher.close();
  }

  type = t;
  is_inited_ = true;
  return 0;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::crypt_session_t::close() {
  cipher.close();
  type.clear();
  is_inited_ = false;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypt_session_t::generate_secret(
    int &libres, const atfw::util::log::log_wrapper::ptr_t &logger) {
  uint32_t key_bits = cipher.get_key_bits();
  uint32_t iv_size = cipher.get_iv_size();
  libres = 0;
  if (!shared_conf) {
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  // generate a secret key
  size_t secret_len = key_bits / 8 + iv_size;
  secret.resize(secret_len);
  libres = shared_conf->shared_dh_context_->random(reinterpret_cast<void *>(&secret[0]), secret_len);
  if (0 != libres) {
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  if (secret.empty()) {
    secret.resize(shared_conf->conf_.default_key.size());
    memcpy(secret.data(), shared_conf->conf_.default_key.data(), shared_conf->conf_.default_key.size());
  }

  return swap_secret(secret, libres, logger);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::crypt_session_t::swap_secret(
    std::vector<unsigned char> &in, int &libres, const atfw::util::log::log_wrapper::ptr_t &logger) {
  uint32_t key_size = cipher.get_key_bits() / 8;
  uint32_t iv_size = cipher.get_iv_size();
  if (in.size() < key_size) {
    in.resize(key_size, 0);
  }

  int ret = 0;
  if (iv_size > 0 && in.size() >= iv_size + key_size) {
    // truncate to iv + key
    in.resize(iv_size + key_size, 0);
    // iv should be just after key
    ret = cipher.set_iv(&in[key_size], iv_size);
    if (ret < 0) {
      libres = static_cast<int>(cipher.get_last_errno());
      return ret;
    }
  } else {
    // truncate to key
    in.resize(key_size, 0);
  }

  ret = cipher.set_key(&in[0], key_size * 8);
  if (logger) {
    FWINSTLOGDEBUG(*logger, "{} => swap_secret: {} , res: ", reinterpret_cast<const void *>(&cipher),
                   detail::debug_to_hex(gsl::span<const uint8_t>{in.data(), in.size()}), cipher.get_last_errno());
  }
  if (ret < 0) {
    libres = static_cast<int>(cipher.get_last_errno());
    return ret;
  }

  if (&in != &secret) {
    secret.swap(in);
  }

  libres = 0;
  return 0;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::libatgw_protocol_sdk()
    : session_id_(0), last_write_ptr_(nullptr), close_reason_(0) {
  crypt_handshake_ = std::make_shared<crypt_session_t>();

  read_head_.len = 0;

  ping_.last_ping = ping_data_t::clk_t::from_time_t(0);
  ping_.last_delta = 0;

  handshake_.switch_secret_type = static_cast<decltype(handshake_.switch_secret_type)>(0);
  handshake_.has_data = false;
  handshake_.ext_data = nullptr;
}

LIBATGW_PROTOCOL_API libatgw_protocol_sdk::~libatgw_protocol_sdk() {
  close(close_reason_t::EN_CRT_UNKNOWN, false);
  close_handshake(error_code_t::EN_ECT_SESSION_EXPIRED);
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::alloc_recv_buffer(size_t /*suggested_size*/, char *&out_buf,
                                                                  size_t &out_len) {
  flag_guard_t flag_guard(flags_, flag_t::EN_PFT_IN_CALLBACK);

  // 如果正处于关闭阶段，忽略所有数据
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    out_buf = nullptr;
    out_len = 0;
    return;
  }

  void *data = nullptr;
  size_t sread = 0, swrite = 0;
  read_buffers_.back(data, sread, swrite);

  // reading length and hash code, use small buffer block
  if (nullptr == data || 0 == swrite) {
    out_len = sizeof(read_head_.buffer) - read_head_.len;

    if (0 == out_len) {
      // hash code and length shouldn't be greater than small buffer block
      out_buf = nullptr;
      assert(false);
    } else {
      out_buf = &read_head_.buffer[read_head_.len];
    }
    return;
  }

  // 否则指定为大内存块缓冲区
  out_buf = reinterpret_cast<char *>(data);
  out_len = swrite;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::read(int /*ssz*/, const char * /*buff*/, size_t nread_s, int &errcode) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    errcode = error_code_t::EN_ECT_CLOSING;
    return;
  }

  errcode = error_code_t::EN_ECT_SUCCESS;
  flag_guard_t flag_guard(flags_, flag_t::EN_PFT_IN_CALLBACK);

  void *data = nullptr;
  size_t sread = 0, swrite = 0;
  read_buffers_.back(data, sread, swrite);
  bool is_free = false;

  // first 32bits is hash code, and then 32bits length
  const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  if (nullptr == data || 0 == swrite) {
    // first, read from small buffer block
    // read header
    assert(nread_s <= sizeof(read_head_.buffer) - read_head_.len);
    read_head_.len += nread_s;  // 写数据计数

    // try to unpack all messages
    char *buff_start = read_head_.buffer;
    size_t buff_left_len = read_head_.len;

    // maybe there are more than one message
    while (buff_left_len > sizeof(uint32_t) + sizeof(uint32_t)) {
      uint32_t msg_len = 0;
      msg_len = flatbuffers::ReadScalar<uint32_t>(buff_start + sizeof(uint32_t));

      // directly dispatch small message
      if (buff_left_len >= msg_header_len + msg_len) {
        uint32_t check_hash =
            atfw::util::hash::murmur_hash3_x86_32(buff_start + msg_header_len, static_cast<int>(msg_len), 0);
        uint32_t expect_hash;
        memcpy(&expect_hash, buff_start, sizeof(uint32_t));

        if (check_hash != expect_hash) {
          errcode = error_code_t::EN_ECT_BAD_DATA;
          // } else if (channel->conf.recv_buffer_limit_size > 0 && msg_len > channel->conf.recv_buffer_limit_size) {
          //     errcode = EN_ATBUS_ERR_INVALID_SIZE;
        }

        // padding to 64bits
        dispatch_data(reinterpret_cast<char *>(buff_start) + msg_header_len, msg_len, errcode);

        // 32bits hash+vint+buffer
        buff_start += msg_header_len + msg_len;
        buff_left_len -= msg_header_len + msg_len;
      } else {
        // left data must be a big message
        // store 32bits hash code + msg length
        // keep padding
        if (0 == read_buffers_.push_back(data, msg_header_len + msg_len)) {
          memcpy(data, buff_start, buff_left_len);
          read_buffers_.pop_back(buff_left_len, false);

          buff_start += buff_left_len;
          buff_left_len = 0;  // exit the loop
        } else {
          // maybe message is too large
          is_free = true;
          buff_start += msg_header_len;
          buff_left_len -= msg_header_len;
          break;
        }
      }
    }

    // move left data to front
    if (buff_start != read_head_.buffer && buff_left_len > 0) {
      memmove(read_head_.buffer, buff_start, buff_left_len);
    }
    read_head_.len = buff_left_len;
  } else {
    // mark data written
    read_buffers_.pop_back(nread_s, false);
  }

  // if big message recv done, dispatch it
  read_buffers_.front(data, sread, swrite);
  if (nullptr != data && 0 == swrite) {
    data = reinterpret_cast<char *>(data) - sread;

    // 32bits hash code
    uint32_t check_hash = atfw::util::hash::murmur_hash3_x86_32(reinterpret_cast<char *>(data) + msg_header_len,
                                                                static_cast<int>(sread - msg_header_len), 0);
    uint32_t expect_hash;
    memcpy(&expect_hash, data, sizeof(uint32_t));
    size_t msg_len = sread - msg_header_len;

    if (check_hash != expect_hash) {
      errcode = error_code_t::EN_ECT_BAD_DATA;
      // } else if (channel->conf.recv_buffer_limit_size > 0 && msg_len > channel->conf.recv_buffer_limit_size) {
      //     errcode = EN_ATBUS_ERR_INVALID_SIZE;
    }

    dispatch_data(reinterpret_cast<char *>(data) + msg_header_len, msg_len, errcode);
    // free the buffer block
    read_buffers_.pop_front(0, true);
  }

  if (is_free) {
    errcode = error_code_t::EN_ECT_INVALID_SIZE;
    if (read_head_.len > 0) {
      dispatch_data(read_head_.buffer, read_head_.len, errcode);
    }
  }
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::dispatch_data(const char *buffer, size_t len, int errcode) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return;
  }

  // do nothing if any error
  if (errcode < 0 || nullptr == buffer) {
    return;
  }

  ::flatbuffers::Verifier cs_msg_verify(reinterpret_cast<const uint8_t *>(buffer), len);
  // verify
  if (false == atframework::gw::v1::Verifycs_msgBuffer(cs_msg_verify)) {
    close(close_reason_t::EN_CRT_INVALID_DATA);
    return;
  }

  // unpack
  const atframework::gw::v1::cs_msg *msg = atframework::gw::v1::Getcs_msg(buffer);
  if (nullptr == msg->head()) {
    return;
  }

  switch (msg->head()->type()) {
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_post) != msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA, false);
        break;
      }

      if (!check_flag(flag_t::EN_PFT_HANDSHAKE_DONE) || !crypt_read_) {
        close(close_reason_t::EN_CRT_HANDSHAKE, false);
        break;
      }

      const ::atframework::gw::v1::cs_body_post *msg_body =
          static_cast<const ::atframework::gw::v1::cs_body_post *>(msg->body());

      const void *out;
      size_t outsz = static_cast<size_t>(msg_body->length());
      int res = decode_post(msg_body->data()->data(), static_cast<size_t>(msg_body->data()->size()), out, outsz);
      if (0 == res) {
        // on_message
        if (nullptr != callbacks_ && callbacks_->message_fn) {
          callbacks_->message_fn(this, out, static_cast<size_t>(msg_body->length()));
        }
      } else {
        close(close_reason_t::EN_CRT_INVALID_DATA, false);
      }

      break;
    }
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST_KEY_SYN):
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST_KEY_ACK): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake) !=
          msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA, false);
        break;
      }
      const ::atframework::gw::v1::cs_body_handshake *msg_body =
          static_cast<const ::atframework::gw::v1::cs_body_handshake *>(msg->body());

      // start to update handshake
      if (!check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
        set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, true);
      }

      dispatch_handshake(*msg_body);
      break;
    }
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake) !=
          msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA, false);
        break;
      }
      const ::atframework::gw::v1::cs_body_handshake *msg_body =
          static_cast<const ::atframework::gw::v1::cs_body_handshake *>(msg->body());

      dispatch_handshake(*msg_body);
      break;
    }
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_PING): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_ping) != msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA);
        break;
      }

      const ::atframework::gw::v1::cs_body_ping *msg_body =
          static_cast<const ::atframework::gw::v1::cs_body_ping *>(msg->body());

      // response pong
      send_pong(msg_body->timepoint());
      break;
    }
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_PONG): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_ping) != msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA);
        break;
      }

      // update ping/pong duration
      if (0 != ping_data_t::clk_t::to_time_t(ping_.last_ping)) {
        ping_.last_delta = static_cast<time_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(ping_data_t::clk_t::now() - ping_.last_ping).count());
      }
      break;
    }
    case atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_KICKOFF): {
      if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_kickoff) !=
          msg->body_type()) {
        close(close_reason_t::EN_CRT_INVALID_DATA, false);
        break;
      }

      const ::atframework::gw::v1::cs_body_kickoff *msg_body =
          static_cast<const ::atframework::gw::v1::cs_body_kickoff *>(msg->body());
      close(msg_body->reason(), false);
      break;
    }
    default: {
      break;
    }
  }
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (check_flag(flag_t::EN_PFT_HANDSHAKE_DONE) && !check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  using namespace atframework::gw::v1;
  int ret = 0;
  switch (body_handshake.step()) {
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_REQ): {
      ret = dispatch_handshake_start_req(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_RSP): {
      ret = dispatch_handshake_start_rsp(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_RECONNECT_REQ): {
      ret = dispatch_handshake_reconn_req(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_RECONNECT_RSP): {
      ret = dispatch_handshake_reconn_rsp(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_DH_PUBKEY_REQ): {
      ret = dispatch_handshake_dh_pubkey_req(
          body_handshake, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_DH_PUBKEY_RSP));
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_DH_PUBKEY_RSP): {
      ret = dispatch_handshake_dh_pubkey_rsp(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_ECDH_PUBKEY_REQ): {
      ret = dispatch_handshake_dh_pubkey_req(
          body_handshake, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_ECDH_PUBKEY_RSP));
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_ECDH_PUBKEY_RSP): {
      ret = dispatch_handshake_dh_pubkey_rsp(body_handshake);
      break;
    }
    case ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_VERIFY): {
      ret = dispatch_handshake_verify_ntf(body_handshake);
      break;
    }
    default: {
      break;
    }
  }

  // handshake failed will close the connection
  if (ret < 0) {
    close_handshake(ret);
    close(close_reason_t::EN_CRT_HANDSHAKE, false);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_start_req(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !callbacks_->new_session_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  std::shared_ptr<detail::crypt_global_configure_t> global_cfg = detail::crypt_global_configure_t::current();
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  std::string crypt_type;
  if (nullptr != body_handshake.crypt_type()) {
    crypt_type = body_handshake.crypt_type()->str();
  }

  // select a available crypt type
  if (!crypt_type.empty()) {
    std::pair<const char *, const char *> res;
    res.first = res.second = crypt_type.c_str();
    while (nullptr != res.second) {
      res = atfw::util::crypto::cipher::ciphertok(res.second);

      if (nullptr != res.first && nullptr != res.second) {
        std::string cipher_type;
        cipher_type.assign(res.first, res.second);
        if (global_cfg->check_type(cipher_type)) {
          crypt_type.swap(cipher_type);
          break;
        }
      }
    }

    // can not find a available crypt method, disable crypt
    if (nullptr == res.second) {
      crypt_type.clear();
    }
  }

  callbacks_->new_session_fn(this, session_id_);

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());
  flatbuffers::Offset<cs_body_handshake> handshake_body;

  //
  ret = pack_handshake_start_rsp(builder, session_id_, crypt_type, handshake_body);
  if (ret < 0) {
    handshake_done(ret);
    return ret;
  }

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   handshake_body.Union()),
      cs_msgIdentifier());
  ret = write_msg(builder);
  if (ret < 0) {
    handshake_done(ret);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_start_rsp(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  // check switch type
  // check if start new session success
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_HANDSHAKE, "start new session refused.");
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  // assign session id,
  session_id_ = body_handshake.session_id();

  std::string crypt_type;
  // if is running handshake, can not handshake again
  if (!handshake_.has_data) {
    if (nullptr != body_handshake.crypt_type()) {
      crypt_type = body_handshake.crypt_type()->str();
    }

    // make a new crypt session for handshake
    if (crypt_handshake_->shared_conf) {
      crypt_handshake_ = std::make_shared<crypt_session_t>();
    }

    std::shared_ptr<detail::crypt_global_configure_t> global_cfg = detail::crypt_global_configure_t::current();
    // check if global configure changed
    if (!global_cfg || !global_cfg->check_type(crypt_type) ||
        global_cfg->conf_.switch_secret_type != body_handshake.switch_type()) {
      crypt_conf_t global_crypt_cfg;
      detail::crypt_global_configure_t::default_crypt_configure(global_crypt_cfg);
      global_crypt_cfg.type = crypt_type;
      global_crypt_cfg.switch_secret_type = body_handshake.switch_type();
      global_crypt_cfg.client_mode = true;
      ::atframework::gateway::libatgw_protocol_sdk::global_reload(global_crypt_cfg);
      global_cfg = detail::crypt_global_configure_t::current();
    }
    int ret = setup_handshake(global_cfg);
    if (ret < 0) {
      return ret;
    }
  } else {
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  // if not use crypt, assign crypt information and close_handshake(0)
  int ret = crypt_handshake_->setup(crypt_type);
  if (crypt_type.empty() || ret < 0) {
    crypt_read_ = crypt_handshake_;
    crypt_write_ = crypt_handshake_;
    close_handshake(ret);
    return ret;
  }

  handshake_.switch_secret_type = body_handshake.switch_type();
  if (nullptr == body_handshake.crypt_param()) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_HANDSHAKE, "has no secret");
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  switch (handshake_.switch_secret_type) {
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT): {
      crypt_handshake_->secret.resize(body_handshake.crypt_param()->size());
      memcpy(crypt_handshake_->secret.data(), body_handshake.crypt_param()->data(),
             body_handshake.crypt_param()->size());

      int libres = 0;
      ret = crypt_handshake_->swap_secret(crypt_handshake_->secret, libres, logger_);
      if (ret < 0) {
        ATFRAME_GATEWAY_ON_ERROR(libres, "set secret failed");
      }

      crypt_read_ = crypt_handshake_;
      crypt_write_ = crypt_handshake_;
      close_handshake(ret);

      // send verify
      ret = send_verify(nullptr, 0);
      break;
    }
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH):
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH): {
      // if in DH handshake, generate and send pubkey
      using namespace ::atframework::gw::v1;

      flatbuffers::FlatBufferBuilder builder;
      flatbuffers::Offset<cs_msg_head> header_data =
          Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                            ::atframework::gateway::detail::alloc_seq());
      flatbuffers::Offset<cs_body_handshake> handshake_body;

      ::atframework::gw::v1::handshake_step_t next_step =
          (handshake_.switch_secret_type ==
           ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH))
              ? ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_DH_PUBKEY_REQ)
              : ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_ECDH_PUBKEY_REQ);
      ret = pack_handshake_dh_pubkey_req(builder, body_handshake, handshake_body, next_step);
      if (ret < 0) {
        break;
      }

      builder.Finish(
          Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                       handshake_body.Union()),
          cs_msgIdentifier());
      ret = write_msg(builder);
      break;
    }
    default: {
      ATFRAME_GATEWAY_ON_ERROR(static_cast<int>(handshake_.switch_secret_type), "unsupported switch type");
      ret = error_code_t::EN_ECT_HANDSHAKE;
      break;
    }
  }

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_req(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  // try to reconnect
  if (nullptr == callbacks_ || !callbacks_->reconnect_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  // assign crypt options
  handshake_.ext_data = &body_handshake;

  int ret = callbacks_->reconnect_fn(this, body_handshake.session_id());
  // after this , can not failed any more, because session had already accepted.

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());
  flatbuffers::Offset<cs_body_handshake> reconn_body;

  uint64_t sess_id = 0;
  if (0 == ret) {
    sess_id = session_id_;
  }

  reconn_body = Createcs_body_handshake(
      builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_RECONNECT_RSP),
      static_cast< ::atframework::gw::v1::switch_secret_t>(handshake_.switch_secret_type),
      builder.CreateString(crypt_handshake_->type));

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   reconn_body.Union()),
      cs_msgIdentifier());

  if (0 != ret) {
    write_msg(builder);
    close_handshake(ret);
    close(ret, true);
  } else {
    ret = write_msg(builder);
    close_handshake(ret);

    // change key immediately, in case of Man-in-the-Middle Attack
    ret = handshake_update();
    if (0 != ret) {
      ATFRAME_GATEWAY_ON_ERROR(ret, "reconnect to old session refused.");
    }
  }

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_reconn_rsp(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  // if success, session id is not 0, and assign all data
  if (0 == body_handshake.session_id()) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_REFUSE_RECONNECT, "update handshake failed.");

    // force to trigger handshake done
    setup_handshake(crypt_handshake_->shared_conf);
    close_handshake(error_code_t::EN_ECT_REFUSE_RECONNECT);
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  std::shared_ptr<detail::crypt_global_configure_t> global_cfg = detail::crypt_global_configure_t::current();
  std::string crypt_type;
  if (nullptr != body_handshake.crypt_type()) {
    crypt_type = body_handshake.crypt_type()->str();
  }
  // check if global configure changed
  if (!global_cfg || !global_cfg->check_type(crypt_type) ||
      global_cfg->conf_.switch_secret_type != body_handshake.switch_type()) {
    crypt_conf_t global_crypt_cfg;
    detail::crypt_global_configure_t::default_crypt_configure(global_crypt_cfg);
    global_crypt_cfg.type = crypt_type;
    global_crypt_cfg.switch_secret_type = body_handshake.switch_type();
    global_crypt_cfg.client_mode = true;
    ::atframework::gateway::libatgw_protocol_sdk::global_reload(global_crypt_cfg);
    global_cfg = detail::crypt_global_configure_t::current();
  }
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  session_id_ = body_handshake.session_id();
  crypt_read_ = crypt_handshake_;
  crypt_write_ = crypt_handshake_;

  close_handshake(0);
  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_dh_pubkey_req(
    const ::atframework::gw::v1::cs_body_handshake &peer_body, ::atframework::gw::v1::handshake_step_t next_step) {
  // check
  int ret = 0;
  if (handshake_.switch_secret_type != peer_body.switch_type() || !crypt_handshake_->shared_conf) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_HANDSHAKE,
                             "crypt information between client and server not matched.");
    close(error_code_t::EN_ECT_HANDSHAKE, true);
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  using namespace ::atframework::gw::v1;
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());
  flatbuffers::Offset<cs_body_handshake> pubkey_rsp_body;

  crypt_handshake_->param.clear();

  do {
    if (false == handshake_.has_data) {
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      ATFRAME_GATEWAY_ON_ERROR(ret, "DH not loaded");
      break;
    }

    int res = handshake_.dh_ctx.read_public(reinterpret_cast<const unsigned char *>(peer_body.crypt_param()->data()),
                                            peer_body.crypt_param()->size());
    if (0 != res) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH read param failed");
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }

    // generate secret
    res = handshake_.dh_ctx.calc_secret(crypt_handshake_->secret);
    if (0 != res) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH compute key failed");
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }

    if (crypt_handshake_->swap_secret(crypt_handshake_->secret, res, logger_) < 0) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH set key failed");
      break;
    }

    crypt_read_ = crypt_handshake_;
    crypt_write_ = crypt_handshake_;
  } while (false);

  // send verify text prefix
  const void *outbuf = nullptr;
  size_t outsz = 0;
  if (0 == ret) {
    // generate verify data
    {
      size_t secret_len = crypt_handshake_->cipher.get_key_bits() / 8;
      // 3 * secret_len, 1 for binary data, 2 for hex data
      unsigned char *verify_text = (unsigned char *)malloc((secret_len << 1) + secret_len);
      if (nullptr != verify_text) {
        int res = crypt_handshake_->shared_conf->shared_dh_context_->random(reinterpret_cast<void *>(verify_text),
                                                                            secret_len);
        if (0 == res) {
          atfw::util::string::dumphex(verify_text, secret_len, verify_text + secret_len);
          ret = encrypt_data(*crypt_handshake_, verify_text + secret_len, secret_len << 1, outbuf, outsz);
        } else {
          ATFRAME_GATEWAY_ON_ERROR(res, "generate verify text failed");
        }
        crypt_handshake_->param.assign(verify_text + secret_len, verify_text + (secret_len << 1) + secret_len);
        free(verify_text);
      }
    }

    if (nullptr == outbuf || 0 == outsz) {
      ret = encrypt_data(*crypt_handshake_, crypt_handshake_->shared_conf->conf_.default_key.data(),
                         crypt_handshake_->shared_conf->conf_.default_key.size(), outbuf, outsz);
    }
  }

  if (0 == ret) {
    pubkey_rsp_body = Createcs_body_handshake(
        builder, session_id_, next_step, peer_body.switch_type(), builder.CreateString(std::string()),
        builder.CreateVector(reinterpret_cast<const int8_t *>(outbuf), nullptr == outbuf ? 0 : outsz));
  } else {
    pubkey_rsp_body =
        Createcs_body_handshake(builder, 0, next_step, peer_body.switch_type(), builder.CreateString(std::string()),
                                builder.CreateVector(static_cast<const int8_t *>(nullptr), 0));
  }

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   pubkey_rsp_body.Union()),
      cs_msgIdentifier());
  ret = write_msg(builder);
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_dh_pubkey_rsp(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  if (0 == body_handshake.session_id() || nullptr == body_handshake.crypt_param()) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_HANDSHAKE, "DH switch key failed.");
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  const void *outbuf = nullptr;
  size_t outsz = 0;
  // decrypt default key
  int ret = decrypt_data(*crypt_handshake_, body_handshake.crypt_param()->data(), body_handshake.crypt_param()->size(),
                         outbuf, outsz);
  if (0 == ret) {
    // secret already setuped when pack pubkey req
    crypt_read_ = crypt_handshake_;

    // add something and encrypt it again. and send verify message
    std::string verify_data;
    verify_data.resize(outsz, 0);
    if (crypt_handshake_->shared_conf && crypt_handshake_->shared_conf->shared_dh_context_) {
      crypt_handshake_->shared_conf->shared_dh_context_->random(reinterpret_cast<void *>(&verify_data[0]), outsz);
    }
    // copy all the checked data
    for (size_t i = 0; i < verify_data.size(); ++i) {
      if (i & 0x01) {
        verify_data[i] = *(reinterpret_cast<const char *>(outbuf) + i);
      }
    }

    ret = send_verify(verify_data.data(), verify_data.size());
    close_handshake(ret);
  }

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::dispatch_handshake_verify_ntf(
    const ::atframework::gw::v1::cs_body_handshake &body_handshake) {
  // if switch type is direct, read handle should be set here
  if (::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT) ==
      body_handshake.switch_type()) {
    crypt_read_ = crypt_handshake_;
  }

  // check crypt info
  int ret = 0;
  if (handshake_.switch_secret_type != body_handshake.switch_type() || !crypt_read_) {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_HANDSHAKE,
                             "crypt information between client and server not matched.");
    close(error_code_t::EN_ECT_HANDSHAKE, true);
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  // check hello message prefix
  if (nullptr != body_handshake.crypt_param() && !crypt_read_->param.empty() &&
      crypt_read_->param.size() <= body_handshake.crypt_param()->size()) {
    const void *outbuf = nullptr;
    size_t outsz = 0;
    ret = decrypt_data(*crypt_read_, body_handshake.crypt_param()->data(), body_handshake.crypt_param()->size(), outbuf,
                       outsz);
    if (0 != ret) {
      ATFRAME_GATEWAY_ON_ERROR(ret, "verify crypt information but decode failed.");
    } else if (outsz < crypt_read_->param.size()) {  // maybe has padding
      ret = error_code_t::EN_ECT_HANDSHAKE;
      ATFRAME_GATEWAY_ON_ERROR(ret, "verify data length error.");
    } else {
      const unsigned char *checked_ch = reinterpret_cast<const unsigned char *>(outbuf);
      for (size_t i = 0; checked_ch && *checked_ch && i < crypt_read_->param.size(); ++i, ++checked_ch) {
        // just check half data
        if ((i & 0x01) && *checked_ch != crypt_read_->param[i]) {
          ret = error_code_t::EN_ECT_CRYPT_VERIFY;
          break;
        }
      }
    }
  }

  if (0 == ret) {
    // then read key updated
    close_handshake(0);
  } else {
    ATFRAME_GATEWAY_ON_ERROR(error_code_t::EN_ECT_CRYPT_VERIFY, "verify failed.");
    close_handshake(error_code_t::EN_ECT_CRYPT_VERIFY);
    close(close_reason_t::EN_CRT_HANDSHAKE, true);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::pack_handshake_start_rsp(
    flatbuffers::FlatBufferBuilder &builder, uint64_t sess_id, std::string &crypt_type,
    flatbuffers::Offset< ::atframework::gw::v1::cs_body_handshake> &handshake_data) {
  using namespace ::atframework::gw::v1;

  int ret = crypt_handshake_->setup(crypt_type);
  // if not use crypt, assign crypt information and close_handshake(0)
  if (0 == sess_id || !crypt_handshake_->shared_conf || crypt_type.empty() || ret < 0) {
    // empty data
    handshake_data = Createcs_body_handshake(
        builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_RSP),
        ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT), builder.CreateString(crypt_type),
        builder.CreateVector(static_cast<const int8_t *>(nullptr), 0));

    crypt_read_ = crypt_handshake_;
    crypt_write_ = crypt_handshake_;
    close_handshake(ret);
    return ret;
  }

  // TODO using crypt_type
  crypt_handshake_->param.clear();
  switch (handshake_.switch_secret_type) {
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT): {
      int libres = 0;
      if (crypt_handshake_->generate_secret(libres, logger_) < 0) {
        // generate a secret key
        ATFRAME_GATEWAY_ON_ERROR(libres, "generate secret failed");
      }

      crypt_write_ = crypt_handshake_;

      handshake_data = Createcs_body_handshake(
          builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_RSP),
          static_cast< ::atframework::gw::v1::switch_secret_t>(handshake_.switch_secret_type),
          builder.CreateString(crypt_type),
          builder.CreateVector(reinterpret_cast<const int8_t *>(crypt_handshake_->secret.data()),
                               crypt_handshake_->secret.size()));

      break;
    }
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH):
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH): {
      do {
        if (false == handshake_.has_data) {
          ret = error_code_t::EN_ECT_HANDSHAKE;
          ATFRAME_GATEWAY_ON_ERROR(ret, "DH not loaded");
          break;
        }

        int res = handshake_.dh_ctx.make_params(crypt_handshake_->param);
        if (0 != res) {
          ATFRAME_GATEWAY_ON_ERROR(res, "DH generate check public key failed");
          ret = error_code_t::EN_ECT_CRYPT_OPERATION;
          break;
        }
      } while (false);
      // send send first parameter
      handshake_data = Createcs_body_handshake(
          builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_RSP),
          static_cast< ::atframework::gw::v1::switch_secret_t>(handshake_.switch_secret_type),
          builder.CreateString(crypt_type),
          builder.CreateVector(reinterpret_cast<const int8_t *>(crypt_handshake_->param.data()),
                               crypt_handshake_->param.size()));

      break;
    }
    default: {
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      ATFRAME_GATEWAY_ON_ERROR(ret, "Unknown switch type");
      break;
    }
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::pack_handshake_dh_pubkey_req(
    flatbuffers::FlatBufferBuilder &builder, const ::atframework::gw::v1::cs_body_handshake &peer_body,
    flatbuffers::Offset< ::atframework::gw::v1::cs_body_handshake> &handshake_data,
    ::atframework::gw::v1::handshake_step_t next_step) {
  using namespace ::atframework::gw::v1;

  int ret = 0;
  if (0 == peer_body.session_id() || nullptr == peer_body.crypt_param() || !crypt_handshake_->shared_conf) {
    // empty data
    handshake_data = Createcs_body_handshake(builder, peer_body.session_id(), next_step,
                                             ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT),
                                             builder.CreateString(std::string()),
                                             builder.CreateVector(static_cast<const int8_t *>(nullptr), 0));

    return error_code_t::EN_ECT_SESSION_NOT_FOUND;
  }

  handshake_.switch_secret_type = peer_body.switch_type();
  crypt_handshake_->param.clear();

  do {
    if (false == handshake_.has_data) {
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      ATFRAME_GATEWAY_ON_ERROR(ret, "DH not loaded");
      break;
    }

    int res = handshake_.dh_ctx.read_params(reinterpret_cast<const unsigned char *>(peer_body.crypt_param()->data()),
                                            peer_body.crypt_param()->size());
    if (0 != res) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH read param failed");
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }

    res = handshake_.dh_ctx.make_public(crypt_handshake_->param);
    if (0 != res) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH make public key failed");
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }

    // generate secret
    res = handshake_.dh_ctx.calc_secret(crypt_handshake_->secret);
    if (0 != res) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH compute key failed");
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }

    if (crypt_handshake_->swap_secret(crypt_handshake_->secret, res, logger_) < 0) {
      ATFRAME_GATEWAY_ON_ERROR(res, "DH set key failed");
      break;
    }
    crypt_write_ = crypt_handshake_;
  } while (false);

  // send send first parameter
  handshake_data =
      Createcs_body_handshake(builder, peer_body.session_id(), next_step,
                              static_cast< ::atframework::gw::v1::switch_secret_t>(handshake_.switch_secret_type),
                              builder.CreateString(std::string()),
                              builder.CreateVector(reinterpret_cast<const int8_t *>(crypt_handshake_->param.data()),
                                                   crypt_handshake_->param.size()));

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::try_write() {
  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  if (check_flag(flag_t::EN_PFT_WRITING)) {
    return 0;
  }

  // empty then skip write data
  if (write_buffers_.empty()) {
    return 0;
  }

  // first 32bits is hash code, and then 32bits length
  // const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  // closing or closed, cancle writing
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    while (!write_buffers_.empty()) {
      ::atbus::detail::buffer_block *bb = write_buffers_.front();
      size_t nwrite = bb->raw_size();
      // // nwrite = write_header_offset_ + [data block...]
      // // data block = 32bits hash+vint+data length
      // char *buff_start = reinterpret_cast<char *>(bb->raw_data()) + write_header_offset_;
      // size_t left_length = nwrite - write_header_offset_;
      // while (left_length >= msg_header_len) {
      //     // skip 32bits hash
      //     uint32_t msg_len = flatbuffers::ReadScalar<uint32_t>(buff_start + sizeof(uint32_t));

      //     // skip 32bits hash and 32bits length
      //     buff_start += msg_header_len;

      //     // data length should be enough to hold all data
      //     if (left_length < msg_header_len + static_cast<size_t>(msg_len)) {
      //         assert(false);
      //         left_length = 0;
      //     }

      //     callback(UV_ECANCELED, error_code_t::EN_ECT_CLOSING, buff_start, left_length - msg_header_len);

      //     buff_start += static_cast<size_t>(msg_len);
      //     // 32bits hash+vint+data length
      //     left_length -= msg_header_len + static_cast<size_t>(msg_len);
      // }

      // remove all cache buffer
      write_buffers_.pop_front(nwrite, true);
    }

    // no need to call write_done(status) to trigger on_close_fn here
    // because on_close_fn is triggered when close(reason) is called or write_done(status) is called ouside
    return error_code_t::EN_ECT_CLOSING;
  }

  int ret = 0;
  bool is_done = false;

  // if not in writing mode, try to merge and write data
  // merge only if message is smaller than read buffer
  if (write_buffers_.limit().cost_number_ > 1 &&
      write_buffers_.front()->raw_size() <= ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE) {
    // left write_header_offset_ size at front
    size_t available_bytes = get_tls_length(tls_buffer_t::EN_TBT_MERGE) - write_header_offset_;
    char *buffer_start = reinterpret_cast<char *>(get_tls_buffer(tls_buffer_t::EN_TBT_MERGE));
    char *free_buffer = buffer_start;

    assert(ATFRAMEWORK_GATEWAY_MACRO_DATA_SMALL_SIZE < available_bytes);

    ::atbus::detail::buffer_block *preview_bb = nullptr;
    while (!write_buffers_.empty() && available_bytes > 0) {
      ::atbus::detail::buffer_block *bb = write_buffers_.front();
      if (nullptr == bb || bb->raw_size() > available_bytes) {
        break;
      }

      // if write_buffers_ is a static circle buffer, can not merge the bound blocks
      if (write_buffers_.is_static_mode() && nullptr != preview_bb && preview_bb > bb) {
        break;
      }
      preview_bb = bb;

      // first write_header_offset_ should not be merged, the rest is 32bits hash+varint+len
      size_t bb_size = bb->raw_size() - write_header_offset_;
      memcpy(free_buffer, ::atbus::detail::fn::buffer_next(bb->raw_data(), write_header_offset_), bb_size);
      free_buffer += bb_size;
      available_bytes -= bb_size;

      write_buffers_.pop_front(bb->raw_size(), true);
    }

    void *data = nullptr;
    write_buffers_.push_front(data, write_header_offset_ + (free_buffer - buffer_start));

    // already pop more data than write_header_offset_ + (free_buffer - buffer_start)
    // so this push_front should always success
    assert(data);
    // at least merge one block
    assert(free_buffer > buffer_start);
    assert(static_cast<size_t>(free_buffer - buffer_start) <=
           (get_tls_length(tls_buffer_t::EN_TBT_MERGE) - write_header_offset_));

    data = ::atbus::detail::fn::buffer_next(data, write_header_offset_);
    // copy back merged data
    memcpy(data, buffer_start, free_buffer - buffer_start);
  }

  // prepare to writing
  ::atbus::detail::buffer_block *writing_block = write_buffers_.front();

  // should always exist, empty will cause return before
  if (nullptr == writing_block) {
    assert(writing_block);
    write_buffers_.pop_front(0, true);
    set_flag(flag_t::EN_PFT_WRITING, true);
    return write_done(error_code_t::EN_ECT_NO_DATA);
  }

  if (writing_block->raw_size() <= write_header_offset_) {
    write_buffers_.pop_front(writing_block->raw_size(), true);
    return try_write();
  }

  // call write
  set_flag(flag_t::EN_PFT_WRITING, true);
  last_write_ptr_ = writing_block->raw_data();
  ret = callbacks_->write_fn(this, writing_block->raw_data(), writing_block->raw_size(), &is_done);
  if (is_done) {
    return write_done(ret);
  }

  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write_msg(flatbuffers::FlatBufferBuilder &builder) {
  // first 32bits is hash code, and then 32bits length
  const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  const void *buf = reinterpret_cast<const void *>(builder.GetBufferPointer());
  size_t len = static_cast<size_t>(builder.GetSize());

  // push back message
  if (nullptr != buf && len > 0) {
    if (len >= std::numeric_limits<uint32_t>::max()) {
      return error_code_t::EN_ECT_INVALID_SIZE;
    }

    // get the write block size: write_header_offset_ + header + len）
    size_t total_buffer_size = write_header_offset_ + msg_header_len + len;

    // 判定内存限制
    void *data;
    int res = write_buffers_.push_back(data, total_buffer_size);
    if (res < 0) {
      return res;
    }

    // skip custom write_header_offset_
    char *buff_start = reinterpret_cast<char *>(data) + write_header_offset_;

    // 32bits hash
    uint32_t hash32 =
        atfw::util::hash::murmur_hash3_x86_32(reinterpret_cast<const char *>(buf), static_cast<int>(len), 0);
    memcpy(buff_start, &hash32, sizeof(uint32_t));

    // length
    flatbuffers::WriteScalar<uint32_t>(buff_start + sizeof(uint32_t), static_cast<uint32_t>(len));
    // buffer
    memcpy(buff_start + msg_header_len, buf, len);
  }

  return try_write();
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write(const void *buffer, size_t len) {
  return send_post(::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST), buffer,
                   len);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::write_done(int status) {
  if (!check_flag(flag_t::EN_PFT_WRITING)) {
    return status;
  }
  flag_guard_t flag_guard(flags_, flag_t::EN_PFT_IN_CALLBACK);

  void *data = nullptr;
  size_t nread, nwrite;

  // first 32bits is hash code, and then 32bits length
  // const size_t msg_header_len = sizeof(uint32_t) + sizeof(uint32_t);

  // popup the lost callback
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

    // nwrite = write_header_offset_ + [data block...]
    // data block = 32bits hash+vint+data length
    // char *buff_start = reinterpret_cast<char *>(data) + write_header_offset_;
    // size_t left_length = nwrite - write_header_offset_;
    // while (left_length >= msg_header_len) {
    //     uint32_t msg_len = flatbuffers::ReadScalar<uint32_t>(buff_start + sizeof(uint32_t));
    //     // skip 32bits hash and 32bits length
    //     buff_start += msg_header_len;

    //     // data length should be enough to hold all data
    //     if (left_length < msg_header_len + static_cast<size_t>(msg_len)) {
    //         assert(false);
    //         left_length = 0;
    //     }

    //     callback(status, last_write_ptr_ == data? 0: TIMEOUT, buff_start, msg_len);

    //     buff_start += static_cast<size_t>(msg_len);

    //     // 32bits hash+32bits length+data length
    //     left_length -= msg_header_len + static_cast<size_t>(msg_len);
    // }

    // remove all cache buffer
    write_buffers_.pop_front(nwrite, true);

    // the end
    if (last_write_ptr_ == data) {
      break;
    }
  };
  last_write_ptr_ = nullptr;

  // unset writing mode
  set_flag(flag_t::EN_PFT_WRITING, false);

  // write left data
  status = try_write();

  // if is disconnecting and there is no more data to write, close it
  if (check_flag(flag_t::EN_PFT_CLOSING) && !check_flag(flag_t::EN_PFT_CLOSED) && !check_flag(flag_t::EN_PFT_WRITING)) {
    set_flag(flag_t::EN_PFT_CLOSED, true);

    if (nullptr != callbacks_ && callbacks_->close_fn) {
      return callbacks_->close_fn(this, close_reason_);
    }
  }

  return status;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int reason) { return close(reason, true); }

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::close(int reason, bool is_send_kickoff) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return 0;
  }
  close_reason_ = reason;

  // send kickoff message
  if (is_send_kickoff) {
    send_kickoff(reason);
  }

  // must set flag after send_kickoff(reason), because it will still use resources
  set_flag(flag_t::EN_PFT_CLOSING, true);

  // wait writing to finished
  // close_fn may be called in send_kickoff/write_msg/write_done
  if (!check_flag(flag_t::EN_PFT_WRITING) && !check_flag(flag_t::EN_PFT_CLOSED)) {
    set_flag(flag_t::EN_PFT_CLOSED, true);

    if (nullptr != callbacks_ && callbacks_->close_fn) {
      return callbacks_->close_fn(this, close_reason_);
    }
  }

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::setup_handshake(
    std::shared_ptr<detail::crypt_global_configure_t> &shared_conf) {
  if (handshake_.has_data) {
    return 0;
  }

  if (crypt_handshake_->shared_conf != shared_conf) {
    crypt_handshake_->shared_conf = shared_conf;
  }

  int ret = 0;
  if (!crypt_handshake_->shared_conf || crypt_handshake_->shared_conf->conf_.type.empty()) {
    return ret;
  }

  handshake_.switch_secret_type = crypt_handshake_->shared_conf->conf_.switch_secret_type;
  switch (handshake_.switch_secret_type) {
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH):
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH): {
      handshake_.dh_ctx.init(shared_conf->shared_dh_context_);
      break;
    }
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT): {
      break;
    }
    default: {
      ret = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }
  }

  handshake_.has_data = 0 == ret;
  if (handshake_.has_data) {
    // ready to update handshake
    set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, true);
  }
  return ret;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::close_handshake(int status) {
  crypt_handshake_->param.clear();

  if (!handshake_.has_data) {
    handshake_done(status);

    // DEBUG CIPHER PROGRESS
    if (logger_) {
      FWINSTLOGDEBUG(*logger_, "{} => close_handshake - status: {}",
                     reinterpret_cast<const void *>(&crypt_handshake_->cipher), status);
    }
    return;
  }
  handshake_.has_data = false;

  switch (handshake_.switch_secret_type) {
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH):
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH): {
      handshake_.dh_ctx.close();
      break;
    }
    case ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT): {
      break;
    }
    default: {
      status = error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
      break;
    }
  }

  handshake_done(status);

  // DEBUG CIPHER PROGRESS
  if (logger_) {
    FWINSTLOGDEBUG(*logger_, "{} => close_handshake - status: {}",
                   reinterpret_cast<const void *>(&crypt_handshake_->cipher), status);
  }
}

LIBATGW_PROTOCOL_API bool libatgw_protocol_sdk::check_reconnect(const libatgw_protocol_api *other) {
  bool ret = true;
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return false;
  }

  const libatgw_protocol_sdk *other_proto = static_cast<const libatgw_protocol_sdk *>(other);
  assert(other_proto);

  // use read handle first, maybe the new handshake not finished
  crypt_session_ptr_t other_crypt_handshake = other_proto->crypt_read_;
  if (!other_crypt_handshake) {
    other_crypt_handshake = other_proto->crypt_handshake_;
  }

  do {
    std::vector<unsigned char> handshake_secret;
    std::string crypt_type;
    if (nullptr == handshake_.ext_data) {
      ret = false;
      break;
    } else {
      const ::atframework::gw::v1::cs_body_handshake *body_handshake =
          reinterpret_cast<const ::atframework::gw::v1::cs_body_handshake *>(handshake_.ext_data);
      const flatbuffers::Vector<int8_t> *secret = body_handshake->crypt_param();
      if (nullptr != secret) {
        handshake_secret.resize(secret->size());
        memcpy(handshake_secret.data(), secret->data(), secret->size());
      }

      if (nullptr != body_handshake->crypt_type()) {
        crypt_type = body_handshake->crypt_type()->str();
      }
    }

    // check crypt type and keybits
    if (crypt_type != other_crypt_handshake->type) {
      ret = false;
      break;
    }

    // using new cipher, old iv or block will be ignored
    int res = crypt_handshake_->setup(crypt_type);
    if (res < 0) {
      ATFRAME_GATEWAY_ON_ERROR(res, "reconnect to old session failed on setup type.");
      ret = false;
      break;
    }

    if (crypt_type.empty()) {
      ret = true;
      break;
    }

    {
      std::vector<unsigned char> sec_swp = other_crypt_handshake->secret;
      int libres = 0;
      res = crypt_handshake_->swap_secret(sec_swp, libres, logger_);
      if (res < 0) {
        ATFRAME_GATEWAY_ON_ERROR(res, "reconnect to old session failed on setup secret.");
        ret = false;
        break;
      }
    }

    // decrypt secret
    const void *outbuf = nullptr;
    size_t outsz = 0;
    if (0 != decrypt_data(*crypt_handshake_, handshake_secret.data(), handshake_secret.size(), outbuf, outsz)) {
      ret = false;
      break;
    }

    // compare secret and encrypted secret
    // decrypt will padding data, so outsz should always equal or greater than secret.size()
    if (nullptr == outbuf || outsz < crypt_handshake_->secret.size() ||
        0 != memcmp(outbuf, crypt_handshake_->secret.data(), crypt_handshake_->secret.size())) {
      ret = false;
    }
  } while (false);

  // if success, copy crypt information
  if (ret) {
    session_id_ = other_proto->session_id_;
    // setup handshake
    setup_handshake(other_crypt_handshake->shared_conf);
    crypt_read_ = crypt_handshake_;
    crypt_write_ = crypt_handshake_;
  }
  return ret;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_recv_buffer_limit(size_t max_size, size_t max_number) {
  read_buffers_.set_mode(max_size, max_number);
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_send_buffer_limit(size_t max_size, size_t max_number) {
  write_buffers_.set_mode(max_size, max_number);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::handshake_update() { return send_key_syn(); }

LIBATGW_PROTOCOL_API std::string libatgw_protocol_sdk::get_info() const {
  using namespace ::atframework::gw::v1;

  const char *switch_secret_name = "Unknown";
  if (handshake_.switch_secret_type >= ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, MIN) &&
      handshake_.switch_secret_type <= ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, MAX)) {
    switch_secret_name = EnumNameswitch_secret_t(static_cast<switch_secret_t>(handshake_.switch_secret_type));
  }

  std::stringstream ss;
  size_t limit_sz = 0;
  ss << "atgateway inner protocol: session id=" << session_id_ << std::endl;
  ss << "    last ping delta=" << ping_.last_delta << std::endl;
  ss << "    handshake=" << (handshake_.has_data ? "running" : "not running") << ", switch type=" << switch_secret_name
     << std::endl;
  ss << "    status: writing=" << check_flag(flag_t::EN_PFT_WRITING)
     << ",closing=" << check_flag(flag_t::EN_PFT_CLOSING) << ",closed=" << check_flag(flag_t::EN_PFT_CLOSED)
     << ",handshake done=" << check_flag(flag_t::EN_PFT_HANDSHAKE_DONE)
     << ",handshake update=" << check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE) << std::endl;

  if (read_buffers_.limit().limit_size_ > 0) {
    limit_sz = read_buffers_.limit().limit_size_ + sizeof(read_head_.buffer) - read_head_.len -
               read_buffers_.limit().cost_size_;
    ss << "    read buffer: used size=" << (read_head_.len + read_buffers_.limit().cost_size_)
       << ", free size=" << limit_sz << std::endl;
  } else {
    ss << "    read buffer: used size=" << (read_head_.len + read_buffers_.limit().cost_size_)
       << ", free size=unlimited" << std::endl;
  }

  if (write_buffers_.limit().limit_size_ > 0) {
    limit_sz = write_buffers_.limit().limit_size_ - write_buffers_.limit().cost_size_;
    ss << "    write buffer: used size=" << write_buffers_.limit().cost_size_ << ", free size=" << limit_sz
       << std::endl;
  } else {
    ss << "    write buffer: used size=" << write_buffers_.limit().cost_size_ << ", free size=unlimited" << std::endl;
  }

#define DUMP_INFO(name, h)                                                       \
  if (h) {                                                                       \
    if (&h != &crypt_handshake_ && h == crypt_handshake_) {                      \
      ss << "    " << name << " handle: == handshake handle" << std::endl;       \
    } else {                                                                     \
      ss << "    " << name << " handle: crypt type=";                            \
      ss << (h->type.empty() ? "NONE" : h->type.c_str());                        \
      ss << ", crypt keybits=" << h->cipher.get_key_bits() << ", crypt secret="; \
      atfw::util::string::dumphex(h->secret.data(), h->secret.size(), ss);       \
      ss << std::endl;                                                           \
    }                                                                            \
  } else {                                                                       \
    ss << "    " << name << " handle: unset" << std::endl;                       \
  }

  DUMP_INFO("read", crypt_read_);
  DUMP_INFO("write", crypt_write_);
  DUMP_INFO("handshake", crypt_handshake_);

#undef DUMP_INFO

  return ss.str();
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::start_session(const std::string &crypt_type) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (0 != session_id_) {
    return error_code_t::EN_ECT_SESSION_ALREADY_EXIST;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());
  flatbuffers::Offset<cs_body_handshake> handshake_body;

  handshake_body = Createcs_body_handshake(
      builder, 0, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_START_REQ),
      ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT), builder.CreateString(crypt_type),
      builder.CreateVector(static_cast<const int8_t *>(nullptr), 0));

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   handshake_body.Union()),
      cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::reconnect_session(uint64_t sess_id, const std::string &crypt_type,
                                                                 const std::vector<unsigned char> &secret) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  // encrypt secrets
  int ret = crypt_handshake_->setup(crypt_type);
  if (ret < 0) {
    return ret;
  }
  if (!crypt_type.empty()) {
    std::vector<unsigned char> sec_swp = secret;
    int libres = 0;
    ret = crypt_handshake_->swap_secret(sec_swp, libres, logger_);
    if (ret < 0) {
      return ret;
    }
  }

  const void *secret_buffer = nullptr;
  size_t secret_length = secret.size();
  encrypt_data(*crypt_handshake_, secret.data(), secret.size(), secret_buffer, secret_length);

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());
  flatbuffers::Offset<cs_body_handshake> handshake_body;

  handshake_body = Createcs_body_handshake(
      builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_RECONNECT_REQ),
      static_cast<switch_secret_t>(handshake_.switch_secret_type), builder.CreateString(crypt_type),
      builder.CreateVector(reinterpret_cast<const int8_t *>(secret_buffer), secret_length));

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   handshake_body.Union()),
      cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(::atframework::gw::v1::cs_msg_type_t msg_type,
                                                         const void *buffer, size_t len) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !crypt_write_) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  // encrypt/zip
  size_t ori_len = len;
  int res = encode_post(buffer, len, buffer, len);
  if (0 != res) {
    return res;
  }

  // pack
  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, msg_type, ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_post> post_body = Createcs_body_post(
      builder, static_cast<uint64_t>(ori_len), builder.CreateVector(reinterpret_cast<const int8_t *>(buffer), len));

  builder.Finish(Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_post),
                              post_body.Union()),
                 cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_post(const void *buffer, size_t len) {
  return send_post(::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST), buffer,
                   len);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_ping() {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  ping_.last_ping = ping_data_t::clk_t::now();

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_PING),
                        ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body =
      Createcs_body_ping(builder, static_cast<int64_t>(ping_.last_ping.time_since_epoch().count()));

  builder.Finish(Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_ping),
                              ping_body.Union()),
                 cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_pong(int64_t tp) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_PONG),
                        ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_ping> ping_body = Createcs_body_ping(builder, tp);

  builder.Finish(Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_ping),
                              ping_body.Union()),
                 cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_key_syn() {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (crypt_handshake_->type.empty()) {
    return 0;
  }

  std::string crypt_type = crypt_handshake_->type;

  if (nullptr == callbacks_ || !callbacks_->write_fn || !crypt_write_) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  // if handshake is running, do not start handshake again.
  if (handshake_.has_data) {
    return 0;
  }

  // make a new crypt session for handshake
  crypt_handshake_ = std::make_shared<crypt_session_t>();

  // and then, just like start rsp
  std::shared_ptr<detail::crypt_global_configure_t> global_cfg = detail::crypt_global_configure_t::current();
  int ret = setup_handshake(global_cfg);
  if (ret < 0) {
    return ret;
  }

  if (!global_cfg || global_cfg->conf_.client_mode) {
    return ret;
  }

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_POST_KEY_SYN),
                        ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_handshake> handshake_body;
  ret = pack_handshake_start_rsp(builder, session_id_, crypt_type, handshake_body);
  if (ret < 0) {
    handshake_done(ret);
    return ret;
  }

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   handshake_body.Union()),
      cs_msgIdentifier());
  ret = write_msg(builder);
  if (ret < 0) {
    handshake_done(ret);
  }
  return ret;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_kickoff(int reason) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_KICKOFF),
                        ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_kickoff> kickoff_body = Createcs_body_kickoff(builder, reason);

  builder.Finish(Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_kickoff),
                              kickoff_body.Union()),
                 cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::send_verify(const void *buf, size_t sz) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (nullptr == callbacks_ || !callbacks_->write_fn || !crypt_write_) {
    return error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  // pack
  uint64_t sess_id = session_id_;

  const void *outbuf = nullptr;
  size_t outsz = 0;
  int ret = 0;
  if (nullptr != buf && sz > 0) {
    ret = encrypt_data(*crypt_write_, buf, sz, outbuf, outsz);
  }

  if (0 != ret) {
    sess_id = 0;
    outbuf = nullptr;
    outsz = 0;
  }

  using namespace ::atframework::gw::v1;

  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<cs_msg_head> header_data =
      Createcs_msg_head(builder, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_type_t, EN_MTT_HANDSHAKE),
                        ::atframework::gateway::detail::alloc_seq());

  flatbuffers::Offset<cs_body_handshake> verify_body =
      Createcs_body_handshake(builder, sess_id, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(handshake_step_t, EN_HST_VERIFY),
                              static_cast< ::atframework::gw::v1::switch_secret_t>(handshake_.switch_secret_type),
                              builder.CreateString(std::string()),
                              builder.CreateVector<int8_t>(reinterpret_cast<const int8_t *>(outbuf), outsz));

  builder.Finish(
      Createcs_msg(builder, header_data, ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(cs_msg_body, cs_body_handshake),
                   verify_body.Union()),
      cs_msgIdentifier());
  return write_msg(builder);
}

LIBATGW_PROTOCOL_API const libatgw_protocol_sdk::crypt_session_ptr_t &libatgw_protocol_sdk::get_crypt_read() const {
  return crypt_read_;
}

LIBATGW_PROTOCOL_API const libatgw_protocol_sdk::crypt_session_ptr_t &libatgw_protocol_sdk::get_crypt_write() const {
  return crypt_write_;
}

LIBATGW_PROTOCOL_API const libatgw_protocol_sdk::crypt_session_ptr_t &libatgw_protocol_sdk::get_crypt_handshake()
    const {
  return crypt_handshake_;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_sdk::set_logger(atfw::util::log::log_wrapper::ptr_t logger) {
  logger_ = logger;
}

int libatgw_protocol_sdk::encode_post(const void *in, size_t insz, const void *&out, size_t &outsz) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    outsz = insz;
    out = in;
    return error_code_t::EN_ECT_CLOSING;
  }

  // TODO compression
  // we should compressed data first, because encrypted data will decrease compression rate.

  // encrypt
  if (!crypt_write_) {
    outsz = insz;
    out = in;
    return error_code_t::EN_ECT_HANDSHAKE;
  }
  int ret = encrypt_data(*crypt_write_, in, insz, out, outsz);
  // if (0 != ret) {
  //     return ret;
  // }

  return ret;
}

int libatgw_protocol_sdk::decode_post(const void *in, size_t insz, const void *&out, size_t &outsz) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    outsz = insz;
    out = in;
    return error_code_t::EN_ECT_CLOSING;
  }

  // decrypt
  if (!crypt_read_) {
    outsz = insz;
    out = in;
    return error_code_t::EN_ECT_HANDSHAKE;
  }
  int ret = decrypt_data(*crypt_read_, in, insz, out, outsz);
  if (ret < 0) {
    out = in;
    outsz = insz;
    return ret;
  }

  // TODO decompression
  return ret;
}

int libatgw_protocol_sdk::encrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out,
                                       size_t &outsz) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (false == crypt_info.is_inited_) {
    return error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return error_code_t::EN_ECT_PARAM;
  }

  if (crypt_info.type.empty()) {
    out = in;
    outsz = insz;
    return error_code_t::EN_ECT_SUCCESS;
  }

  void *buffer = get_tls_buffer(tls_buffer_t::EN_TBT_CRYPT);
  size_t len = get_tls_length(tls_buffer_t::EN_TBT_CRYPT);

  std::unique_ptr<unsigned char[]> unique_buffer;
  if (len < insz + crypt_info.cipher.get_block_size() + crypt_info.cipher.get_iv_size()) {
    len = insz + crypt_info.cipher.get_block_size() + crypt_info.cipher.get_iv_size();
    unique_buffer.reset(new unsigned char[len]);
    buffer = reinterpret_cast<void *>(unique_buffer.get());
  }

  int res = crypt_info.cipher.encrypt(reinterpret_cast<const unsigned char *>(in), insz,
                                      reinterpret_cast<unsigned char *>(buffer), &len);

  // DEBUG CIPHER PROGRESS
  if (logger_) {
    FWINSTLOGDEBUG(*logger_, "{} => encrypt_data - before: {}",
                   reinterpret_cast<const void *>(&crypt_handshake_->cipher),
                   detail::debug_to_hex(gsl::span<const uint8_t>{reinterpret_cast<const uint8_t *>(in), insz}));
    FWINSTLOGDEBUG(*logger_, "{} => encrypt_data - after: {}",
                   reinterpret_cast<const void *>(&crypt_handshake_->cipher),
                   detail::debug_to_hex(gsl::span<const uint8_t>{reinterpret_cast<const uint8_t *>(buffer), len}));
  }
  if (res < 0) {
    out = nullptr;
    outsz = 0;
    ATFRAME_GATEWAY_ON_ERROR(res, "encrypt data failed");
    return error_code_t::EN_ECT_CRYPT_OPERATION;
  }

  out = buffer;
  outsz = len;
  return error_code_t::EN_ECT_SUCCESS;
}

int libatgw_protocol_sdk::decrypt_data(crypt_session_t &crypt_info, const void *in, size_t insz, const void *&out,
                                       size_t &outsz) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return error_code_t::EN_ECT_CLOSING;
  }

  if (false == crypt_info.is_inited_) {
    return error_code_t::EN_ECT_CRYPT_NOT_SUPPORTED;
  }

  if (0 == insz || nullptr == in) {
    out = in;
    outsz = insz;
    return error_code_t::EN_ECT_PARAM;
  }

  if (crypt_info.type.empty()) {
    out = in;
    outsz = insz;
    return error_code_t::EN_ECT_SUCCESS;
  }

  void *buffer = get_tls_buffer(tls_buffer_t::EN_TBT_CRYPT);
  size_t len = get_tls_length(tls_buffer_t::EN_TBT_CRYPT);

  std::unique_ptr<unsigned char[]> unique_buffer;
  if (len < insz + crypt_info.cipher.get_block_size() + crypt_info.cipher.get_iv_size()) {
    len = insz + crypt_info.cipher.get_block_size() + crypt_info.cipher.get_iv_size();
    unique_buffer.reset(new unsigned char[len]);
    buffer = reinterpret_cast<void *>(unique_buffer.get());
  }

  int res = crypt_info.cipher.decrypt(reinterpret_cast<const unsigned char *>(in), insz,
                                      reinterpret_cast<unsigned char *>(buffer), &len);

  // DEBUG CIPHER PROGRESS
  if (logger_) {
    FWINSTLOGDEBUG(*logger_, "{} => decrypt_data - before: {}", reinterpret_cast<const void *>(&crypt_info.cipher),
                   detail::debug_to_hex(gsl::span<const uint8_t>{reinterpret_cast<const uint8_t *>(in), insz}));
    FWINSTLOGDEBUG(*logger_, "{} => decrypt_data - after: {}", reinterpret_cast<const void *>(&crypt_info.cipher),
                   detail::debug_to_hex(gsl::span<const uint8_t>{reinterpret_cast<const uint8_t *>(buffer), len}));
  }
  if (res < 0) {
    out = nullptr;
    outsz = 0;
    ATFRAME_GATEWAY_ON_ERROR(res, "decrypt data failed");
    return error_code_t::EN_ECT_CRYPT_OPERATION;
  }

  out = buffer;
  outsz = len;
  return error_code_t::EN_ECT_SUCCESS;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_sdk::global_reload(crypt_conf_t &crypt_conf) {
  // spin_lock
  static atfw::util::lock::spin_lock global_proto_lock;
  atfw::util::lock::lock_holder<atfw::util::lock::spin_lock> lh(global_proto_lock);

  detail::crypt_global_configure_t::ptr_t inst = std::make_shared<detail::crypt_global_configure_t>(crypt_conf);
  if (!inst) {
    return error_code_t::EN_ECT_MALLOC;
  }

  int ret = inst->init();
  if (0 == ret) {
    detail::crypt_global_configure_t::current().swap(inst);
  }

  return ret;
}
}  // namespace v1
}  // namespace gateway
}  // namespace atframework
