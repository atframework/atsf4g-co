
#include <std/functional.h>
#include <std/ref.h>
#include <std/smart_ptr.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include <uv.h>

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include "session_manager.h"

static int app_handle_on_forward_response(atapp::app &app, const atapp::app::message_sender_t &source,
                                          const atapp::app::message_t &m, int32_t error_code) {
  if (error_code >= 0) {
    return 0;
  }

  FWLOGERROR("send data from {:#x} to {:#x} failed, msg sequence: {}, code: {}", app.get_id(), source.id,
             m.msg_sequence, error_code);
  return 0;
}

class gateway_module : public ::atapp::module_impl {
 public:
  gateway_module() {}
  virtual ~gateway_module() {}

 public:
  virtual int init() UTIL_CONFIG_OVERRIDE {
    gw_mgr_.get_conf().version = 1;

    int res = 0;
    if ("inner" == gw_mgr_.get_conf().listen.type) {
      typedef std::unique_ptr<::atframe::gateway::proto_base> proto_ptr_t;
      gw_mgr_.init(get_app()->get_bus_node().get(), std::bind<proto_ptr_t>(&gateway_module::create_proto_inner, this));

      gw_mgr_.set_on_create_session(std::bind<int>(&gateway_module::proto_inner_callback_on_create_session, this,
                                                   std::placeholders::_1, std::placeholders::_2));

      // init callbacks
      proto_callbacks_.write_fn =
          std::bind<int>(&gateway_module::proto_inner_callback_on_write, this, std::placeholders::_1,
                         std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
      proto_callbacks_.message_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_message, this,
                                                   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
      proto_callbacks_.new_session_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_new_session, this,
                                                       std::placeholders::_1, std::placeholders::_2);
      proto_callbacks_.reconnect_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_reconnect, this,
                                                     std::placeholders::_1, std::placeholders::_2);
      proto_callbacks_.close_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_close, this,
                                                 std::placeholders::_1, std::placeholders::_2);
      proto_callbacks_.on_handshake_done_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_handshake_done,
                                                             this, std::placeholders::_1, std::placeholders::_2);

      proto_callbacks_.on_handshake_update_fn = std::bind<int>(&gateway_module::proto_inner_callback_on_update_done,
                                                               this, std::placeholders::_1, std::placeholders::_2);

      proto_callbacks_.on_error_fn =
          std::bind<int>(&gateway_module::proto_inner_callback_on_error, this, std::placeholders::_1,
                         std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);

    } else {
      PSTDERROR("listen type %s not supported.\n", gw_mgr_.get_conf().listen.type.c_str());
      return -1;
    }

    // init limits
    res = gw_mgr_.listen_all();
    if (res <= 0) {
      PSTDERROR("nothing listened for client, please see log for more details.\n");
      return -1;
    }

    return 0;
  }

  virtual int reload() UTIL_CONFIG_OVERRIDE {
    ++gw_mgr_.get_conf().version;

    // load init cluster member from configure
    gw_mgr_.get_conf().limits.total_recv_bytes = 0;
    gw_mgr_.get_conf().limits.total_send_bytes = 0;
    gw_mgr_.get_conf().limits.hour_recv_bytes = 0;
    gw_mgr_.get_conf().limits.hour_send_bytes = 0;
    gw_mgr_.get_conf().limits.minute_recv_bytes = 0;
    gw_mgr_.get_conf().limits.minute_send_bytes = 0;
    gw_mgr_.get_conf().limits.total_recv_times = 0;
    gw_mgr_.get_conf().limits.total_send_times = 0;
    gw_mgr_.get_conf().limits.hour_recv_times = 0;
    gw_mgr_.get_conf().limits.hour_send_times = 0;
    gw_mgr_.get_conf().limits.minute_recv_times = 0;
    gw_mgr_.get_conf().limits.minute_send_times = 0;
    gw_mgr_.get_conf().limits.max_client_number = 65536;

    gw_mgr_.get_conf().listen.address.clear();
    gw_mgr_.get_conf().listen.type.clear();
    gw_mgr_.get_conf().listen.backlog = 1024;

    gw_mgr_.get_conf().reconnect_timeout = 180;     // 60s
    gw_mgr_.get_conf().send_buffer_size = 1048576;  // 1MB
    gw_mgr_.get_conf().default_router = 0;
    gw_mgr_.get_conf().first_idle_timeout = 10;  // 10s

    util::config::ini_loader &cfg = get_app()->get_configure_loader();
    // listen configures
    cfg.dump_to("atgateway.listen.address", gw_mgr_.get_conf().listen.address);
    cfg.dump_to("atgateway.listen.type", gw_mgr_.get_conf().listen.type);
    cfg.dump_to("atgateway.listen.max_client", gw_mgr_.get_conf().limits.max_client_number);
    cfg.dump_to("atgateway.listen.backlog", gw_mgr_.get_conf().listen.backlog);

    // client session configure
    cfg.dump_to("atgateway.client.router.default", gw_mgr_.get_conf().default_router);
    cfg.dump_to("atgateway.client.send_buffer_size", gw_mgr_.get_conf().send_buffer_size);
    cfg.dump_to("atgateway.client.reconnect_timeout", gw_mgr_.get_conf().reconnect_timeout);
    cfg.dump_to("atgateway.client.first_idle_timeout", gw_mgr_.get_conf().first_idle_timeout);

    // client limit
    cfg.dump_to("atgateway.client.limit.total_send_bytes", gw_mgr_.get_conf().limits.total_send_bytes);
    cfg.dump_to("atgateway.client.limit.total_recv_bytes", gw_mgr_.get_conf().limits.total_recv_bytes);
    cfg.dump_to("atgateway.client.limit.hour_send_bytes", gw_mgr_.get_conf().limits.hour_send_bytes);
    cfg.dump_to("atgateway.client.limit.hour_recv_bytes", gw_mgr_.get_conf().limits.hour_recv_bytes);
    cfg.dump_to("atgateway.client.limit.minute_send_bytes", gw_mgr_.get_conf().limits.minute_send_bytes);
    cfg.dump_to("atgateway.client.limit.minute_recv_bytes", gw_mgr_.get_conf().limits.minute_recv_bytes);
    cfg.dump_to("atgateway.client.limit.total_send_times", gw_mgr_.get_conf().limits.total_send_times);
    cfg.dump_to("atgateway.client.limit.total_recv_times", gw_mgr_.get_conf().limits.total_recv_times);
    cfg.dump_to("atgateway.client.limit.hour_send_times", gw_mgr_.get_conf().limits.hour_send_times);
    cfg.dump_to("atgateway.client.limit.hour_recv_times", gw_mgr_.get_conf().limits.hour_recv_times);
    cfg.dump_to("atgateway.client.limit.minute_send_times", gw_mgr_.get_conf().limits.minute_send_times);
    cfg.dump_to("atgateway.client.limit.minute_recv_times", gw_mgr_.get_conf().limits.minute_recv_times);

    // crypt
    ::atframe::gateway::session_manager::crypt_conf_t &crypt_conf = gw_mgr_.get_conf().crypt;
    crypt_conf.default_key.clear();
    crypt_conf.update_interval = 300;  // 5min
    crypt_conf.type.clear();
    crypt_conf.switch_secret_type = ::atframe::gw::inner::v1::switch_secret_t_EN_SST_DIRECT;
    crypt_conf.client_mode = false;

    // crypt_conf.rsa_sign_type = ::atframe::gw::inner::v1::rsa_sign_t_EN_RST_PKCS1;
    // crypt_conf.hash_id =
    // crypt_conf.rsa_public_key.clear();
    // crypt_conf.rsa_private_key.clear();
    crypt_conf.dh_param.clear();
    do {
      std::string val;
      cfg.dump_to("atgateway.client.crypt.key", crypt_conf.default_key);
      cfg.dump_to("atgateway.client.crypt.update_interval", crypt_conf.update_interval);
      cfg.dump_to("atgateway.client.crypt.type", crypt_conf.type);

      // rsa
      // cfg.dump_to("atgateway.client.crypt.rsa.public_key", crypt_conf.rsa_public_key);
      // cfg.dump_to("atgateway.client.crypt.rsa.private_key", crypt_conf.rsa_private_key);
      // if (!crypt_conf.rsa_public_key.empty() && !crypt_conf.rsa_private_key.empty()) {
      //     crypt_conf.switch_secret_type = ::atframe::gw::inner::v1::switch_secret_t_EN_SST_RSA;
      //
      //     val.clear();
      //     cfg.dump_to("atgateway.client.crypt.rsa.sign", val);
      //     if (0 == UTIL_STRFUNC_STRNCASE_CMP("pkcs1_v15", val.c_str(), 9)) {
      //         crypt_conf.rsa_sign_type = ::atframe::gw::inner::v1::rsa_sign_t_EN_RST_PKCS1_V15;
      //     } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("pkcs1", val.c_str(), 5)) {
      //         crypt_conf.rsa_sign_type = ::atframe::gw::inner::v1::rsa_sign_t_EN_RST_PKCS1;
      //     } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("pss", val.c_str(), 3)) {
      //         crypt_conf.rsa_sign_type = ::atframe::gw::inner::v1::rsa_sign_t_EN_RST_PSS;
      //     }
      // }

      // dh
      cfg.dump_to("atgateway.client.crypt.dhparam", crypt_conf.dh_param);
      if (!crypt_conf.dh_param.empty()) {
        if (0 == UTIL_STRFUNC_STRNCASE_CMP("ecdh:", crypt_conf.dh_param.c_str(), 5)) {
          crypt_conf.switch_secret_type = ::atframe::gw::inner::v1::switch_secret_t_EN_SST_ECDH;
        } else {
          crypt_conf.switch_secret_type = ::atframe::gw::inner::v1::switch_secret_t_EN_SST_DH;
        }
      }

      // hash id
      // val.clear();
      // cfg.dump_to("atgateway.client.crypt.hash_id", crypt_conf.hash_id);
    } while (false);

    // protocol reload
    if ("inner" == gw_mgr_.get_conf().listen.type) {
      int res = ::atframe::gateway::libatgw_proto_inner_v1::global_reload(crypt_conf);
      if (res < 0) {
        WLOGERROR("reload inner protocol global configure failed, res: %d", res);
        return res;
      }
    }

    return 0;
  }

  virtual int stop() UTIL_CONFIG_OVERRIDE {
    gw_mgr_.reset();
    return 0;
  }

  virtual int timeout() UTIL_CONFIG_OVERRIDE { return 0; }

  virtual const char *name() const UTIL_CONFIG_OVERRIDE { return "gateway_module"; }

  virtual int tick() UTIL_CONFIG_OVERRIDE { return gw_mgr_.tick(); }

  inline ::atframe::gateway::session_manager &get_session_manager() { return gw_mgr_; }
  inline const ::atframe::gateway::session_manager &get_session_manager() const { return gw_mgr_; }

 private:
  std::unique_ptr<::atframe::gateway::proto_base> create_proto_inner() {
    ::atframe::gateway::libatgw_proto_inner_v1 *ret = new (std::nothrow)::atframe::gateway::libatgw_proto_inner_v1();
    if (NULL != ret) {
      ret->set_callbacks(&proto_callbacks_);
      ret->set_write_header_offset(sizeof(uv_write_t));
    }

    return std::unique_ptr<::atframe::gateway::proto_base>(ret);
  }

  static void proto_inner_callback_on_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    // alloc read buffer from session proto
    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(handle->data);
    assert(sess);

    if (NULL == sess) {
      if (NULL != buf) {
        buf->base = NULL;
        buf->len = 0;
      }
      return;
    }

#if _MSC_VER
    size_t len = 0;
    sess->on_alloc_read(suggested_size, buf->base, len);
    buf->len = static_cast<ULONG>(len);
#else
    size_t len = 0;
    sess->on_alloc_read(suggested_size, buf->base, len);
    buf->len = len;
#endif
  }

  static void proto_inner_callback_on_read_data(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    if (NULL == stream) {
      return;
    }

    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(stream->data);
    assert(sess);
    if (NULL == sess) {
      return;
    }

    // 如果正处于关闭阶段，忽略所有数据
    if (sess->check_flag(::atframe::gateway::session::flag_t::EN_FT_CLOSING)) {
      return;
    }

    if (NULL == sess->get_manager()) {
      return;
    }
    ::atframe::gateway::session_manager *mgr = sess->get_manager();

    // if no more data or EAGAIN or break by signal, just ignore
    if (0 == nread || UV_EAGAIN == nread || UV_EAI_AGAIN == nread || UV_EINTR == nread) {
      return;
    }

    // if network error or reset by peer, move session into reconnect queue
    if (nread < 0) {
      // notify to close fd
      mgr->close(sess->get_id(), ::atframe::gateway::close_reason_t::EN_CRT_RESET, true);
      return;
    }

    if (NULL != buf) {
      // in case of deallocator session in read callback.
      ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();
      sess_holder->on_read(static_cast<int>(nread), buf->base, static_cast<size_t>(nread));
    }
  }

  int proto_inner_callback_on_create_session(::atframe::gateway::session *sess, uv_stream_t *handle) {
    if (NULL == sess) {
      WLOGERROR("create session with inner proto without session object");
      return 0;
    }

    if (NULL == handle) {
      WLOGERROR("create session with inner proto without handle");
      return 0;
    }

    // start read
    handle->data = sess;
    uv_read_start(handle, proto_inner_callback_on_read_alloc, proto_inner_callback_on_read_data);

    return 0;
  }

  static void proto_inner_callback_on_written_fn(uv_write_t *req, int status) {
    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(req->data);
    assert(sess);

    if (NULL != sess) {
      sess->set_flag(::atframe::gateway::session::flag_t::EN_FT_WRITING_FD, false);
      sess->on_write_done(status);
    }
  }

  int proto_inner_callback_on_write(::atframe::gateway::proto_base *proto, void *buffer, size_t sz, bool *is_done) {
    if (NULL == proto || NULL == buffer) {
      if (NULL != is_done) {
        *is_done = true;
      }
      return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
    }

    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (NULL == sess) {
      if (NULL != is_done) {
        *is_done = true;
      }
      return -1;
    }

    assert(sz >= proto->get_write_header_offset());
    int ret = 0;
    do {
      // uv_write_t
      void *real_buffer = ::atbus::detail::fn::buffer_next(buffer, proto->get_write_header_offset());
      sz -= proto->get_write_header_offset();
      uv_write_t *req = reinterpret_cast<uv_write_t *>(buffer);
      req->data = proto->get_private_data();
      assert(sizeof(uv_write_t) <= proto->get_write_header_offset());

      uv_buf_t bufs[1] = {uv_buf_init(reinterpret_cast<char *>(real_buffer), static_cast<unsigned int>(sz))};
      sess->set_flag(::atframe::gateway::session::flag_t::EN_FT_WRITING_FD, true);

      ret = uv_write(req, sess->get_uv_stream(), bufs, 1, proto_inner_callback_on_written_fn);
      if (0 != ret) {
        sess->set_flag(::atframe::gateway::session::flag_t::EN_FT_WRITING_FD, false);
        WLOGERROR("send data to proto %p failed, res: %d", proto, ret);
      }

    } while (false);

    if (NULL != is_done) {
      // if not writting, notify write finished
      *is_done = !sess->check_flag(::atframe::gateway::session::flag_t::EN_FT_WRITING_FD);
    }
    return ret;
  }

  int proto_inner_callback_on_message(::atframe::gateway::proto_base *proto, const void *buffer, size_t sz) {
    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (NULL == sess) {
      WLOGERROR("recv message from proto object %p length, but has no session", proto);
      return -1;
    }
    ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    ::atframe::gw::ss_msg post_msg;
    post_msg.mutable_head()->set_session_id(sess_holder->get_id());

    ::atframe::gw::ss_body_post *post = post_msg.mutable_body()->mutable_post();
    if (NULL != post) {
      post->add_session_ids(sess_holder->get_id());
      post->set_content(buffer, sz);
    }

    // send to router
    WLOGDEBUG("session 0x%llx send %llu bytes data to server 0x%llx",
              static_cast<unsigned long long>(sess_holder->get_id()), static_cast<unsigned long long>(sz),
              static_cast<unsigned long long>(sess_holder->get_router()));

    return gw_mgr_.post_data(sess_holder->get_router(), post_msg);
  }

  int proto_inner_callback_on_new_session(::atframe::gateway::proto_base *proto, uint64_t &sess_id) {
    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (NULL == sess) {
      WLOGERROR("recv new session message from proto object %p length, but has no session", proto);
      return -1;
    }
    ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    int ret = sess_holder->init_new_session(gw_mgr_.get_conf().default_router);
    sess_id = sess_holder->get_id();
    if (0 != ret) {
      WLOGERROR("create new session failed, ret: %d", ret);
    }

    return ret;
  }

  int proto_inner_callback_on_reconnect(::atframe::gateway::proto_base *proto, uint64_t sess_id) {
    if (NULL == proto) {
      WLOGERROR("parameter error");
      return -1;
    }

    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (NULL == sess) {
      WLOGERROR("close session from proto object %p length, but has no session", proto);
      return -1;
    }
    ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    // check proto reconnect access
    if (sess_holder->check_flag(::atframe::gateway::session::flag_t::EN_FT_INITED)) {
      WLOGERROR("try to reconnect session 0x%llx(%p) from 0x%llx, but already inited",
                static_cast<unsigned long long>(sess_holder->get_id()), sess, static_cast<unsigned long long>(sess_id));
      return -1;
    }

    int res = gw_mgr_.reconnect(*sess_holder, sess_id);
    if (0 != res) {
      if (::atframe::gateway::error_code_t::EN_ECT_SESSION_NOT_FOUND != res &&
          ::atframe::gateway::error_code_t::EN_ECT_REFUSE_RECONNECT != res) {
        WLOGERROR("reconnect session 0x%llx(%p) from 0x%llx failed, res: %d",
                  static_cast<unsigned long long>(sess_holder->get_id()), sess,
                  static_cast<unsigned long long>(sess_id), res);
      } else {
        WLOGINFO("reconnect session 0x%llx(%p) from 0x%llx failed, res: %d",
                 static_cast<unsigned long long>(sess_holder->get_id()), sess, static_cast<unsigned long long>(sess_id),
                 res);
      }
    } else {
      WLOGINFO("reconnect session 0x%llx(%p) success", static_cast<unsigned long long>(sess_holder->get_id()), sess);
    }
    return res;
  }

  int proto_inner_callback_on_close(::atframe::gateway::proto_base *proto, int reason) {
    if (NULL == proto) {
      WLOGERROR("parameter error");
      return -1;
    }

    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (NULL == sess) {
      WLOGERROR("close session from proto object %p length, but has no session", proto);
      return -1;
    }
    ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    if (!sess_holder->check_flag(::atframe::gateway::session::flag_t::EN_FT_CLOSING)) {
      // if network EOF or network error, do not close session, but wait for reconnect
      bool enable_reconnect = reason <= ::atframe::gateway::close_reason_t::EN_CRT_RECONNECT_BOUND;
      if (enable_reconnect) {
        WLOGINFO("session 0x%llx(%p) closed", static_cast<unsigned long long>(sess_holder->get_id()), sess);
      } else {
        WLOGINFO("session 0x%llx(%p) closed disable reconnect", static_cast<unsigned long long>(sess_holder->get_id()),
                 sess);
      }
      if (NULL != sess_holder->get_manager()) {
        if (sess_holder->get_manager()->close(sess_holder->get_id(), reason, enable_reconnect) < 0) {
          sess_holder->close(reason);
        }
      } else {
        sess_holder->close(reason);
      }
    } else {
      if (sess_holder->check_flag(::atframe::gateway::session::flag_t::EN_FT_RECONNECTED)) {
        WLOGINFO("session 0x%llx(%p) reconnected and release old connection",
                 static_cast<unsigned long long>(sess_holder->get_id()), sess);
      } else {
        WLOGINFO("session 0x%llx(%p) closed", static_cast<unsigned long long>(sess_holder->get_id()), sess);
      }
    }

    // TODO if it's closed manually, remove it from manager
    return 0;
  }

  int proto_inner_callback_on_handshake_done(::atframe::gateway::proto_base *proto, int status) {
    if (0 == status) {
      ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
      if (NULL == sess) {
        WLOGERROR("handshake done from proto object %p length, but has no session", proto);
        return -1;
      }
      ::atframe::gateway::session::ptr_t sess_holder = sess->shared_from_this();

      WLOGINFO("session 0x%llx(%p) handshake done\n%s", static_cast<unsigned long long>(sess->get_id()), sess,
               proto->get_info().c_str());

      int res = gw_mgr_.active_session(sess_holder);
      if (0 != res) {
        WLOGERROR("session 0x%llx send new session to router server failed, res: %d",
                  static_cast<unsigned long long>(sess->get_id()), res);
        return -1;
      }
    } else {
      ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
      if (NULL == sess) {
        WLOGERROR("session NONE handshake failed,res: %d\n%s", status, proto->get_info().c_str());
      } else {
        WLOGERROR("session 0x%llx(%p) handshake failed,res: %d\n%s", static_cast<unsigned long long>(sess->get_id()),
                  sess, status, proto->get_info().c_str());
      }
    }
    return 0;
  }

  int proto_inner_callback_on_update_done(::atframe::gateway::proto_base *proto, int status) {
    ::atframe::gateway::session *sess = reinterpret_cast<::atframe::gateway::session *>(proto->get_private_data());
    if (0 == status) {
      if (NULL == sess) {
        WLOGDEBUG("session NONE handshake update success\n%s", proto->get_info().c_str());
      } else {
        WLOGDEBUG("session 0x%llx(%p) handshake update success\n%s", static_cast<unsigned long long>(sess->get_id()),
                  sess, proto->get_info().c_str());
      }
    } else {
      if (NULL == sess) {
        WLOGERROR("session NONE handshake update failed,res: %d\n%s", status, proto->get_info().c_str());
      } else {
        WLOGERROR("session 0x%llx(%p) handshake update failed,res: %d\n%s",
                  static_cast<unsigned long long>(sess->get_id()), sess, status, proto->get_info().c_str());
      }
    }
    return 0;
  }

  int proto_inner_callback_on_error(::atframe::gateway::proto_base *, const char *filename, int line, int errcode,
                                    const char *errmsg) {
    if (::util::log::log_wrapper::check_level(WDTLOGGETCAT(::util::log::log_wrapper::categorize_t::DEFAULT),
                                              ::util::log::log_wrapper::level_t::LOG_LW_ERROR)) {
      WDTLOGGETCAT(::util::log::log_wrapper::categorize_t::DEFAULT)
          ->log(::util::log::log_wrapper::caller_info_t(::util::log::log_wrapper::level_t::LOG_LW_ERROR, "Error",
                                                        filename, line, "anonymous"),
                "error code %d, msg: %s", errcode, errmsg);
    }
    return 0;
  }

 public:
  int cmd_on_kickoff(util::cli::callback_param params) {
    if (params.get_params_number() < 1) {
      WLOGERROR("kickoff command require session id");
      return 0;
    }

    ::atframe::gateway::session::id_t sess_id = 0;
    util::string::str2int(sess_id, params[0]->to_string());

    int reason = ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF;
    if (params.get_params_number() > 1) {
      util::string::str2int(reason, params[1]->to_string());
    }

    // do not allow reconnect
    int res = gw_mgr_.close(sess_id, reason, false);
    if (0 != res) {
      WLOGERROR("command kickoff session 0x%llx failed, res: %d", static_cast<unsigned long long>(sess_id), res);
    } else {
      WLOGINFO("command kickoff session 0x%llx success", static_cast<unsigned long long>(sess_id));
    }

    return 0;
  }

  int cmd_on_disconnect(util::cli::callback_param params) {
    if (params.get_params_number() < 1) {
      WLOGERROR("disconnect command require session id");
      return 0;
    }

    ::atframe::gateway::session::id_t sess_id = 0;
    util::string::str2int(sess_id, params[0]->to_string());

    int reason = ::atframe::gateway::close_reason_t::EN_CRT_RESET;
    if (params.get_params_number() > 1) {
      util::string::str2int(reason, params[1]->to_string());
    }

    // do not allow reconnect
    int res = gw_mgr_.close(sess_id, reason, true);
    if (0 != res) {
      WLOGERROR("command disconnect session 0x%llx failed, res: %d", static_cast<unsigned long long>(sess_id), res);
    } else {
      WLOGINFO("command disconnect session 0x%llx success", static_cast<unsigned long long>(sess_id));
    }

    return 0;
  }

 private:
  ::atframe::gateway::session_manager gw_mgr_;
  ::atframe::gateway::proto_base::proto_callbacks_t proto_callbacks_;
};

struct app_handle_on_recv {
  std::reference_wrapper<gateway_module> mod_;
  app_handle_on_recv(gateway_module &mod) : mod_(mod) {}

  int operator()(::atapp::app &, const atapp::app::message_sender_t &source, const atapp::app::message_t &message) {
    if (NULL == message.data || 0 == message.data_size) {
      return 0;
    }

    ::google::protobuf::ArenaOptions arena_options;
    arena_options.initial_block_size = (message.data_size + 256) & 255;
    ::google::protobuf::Arena arena(arena_options);
    ::atframe::gw::ss_msg *msg =
        ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::Arena::CreateMessage<::atframe::gw::ss_msg>(&arena);
    assert(msg);

    if (false == msg->ParseFromArray(message.data, static_cast<int>(message.data_size))) {
      WLOGDEBUG("from server 0x%llx: session 0x%llx parse %llu bytes data failed: %s",
                static_cast<unsigned long long>(source.id), static_cast<unsigned long long>(msg->head().session_id()),
                static_cast<unsigned long long>(message.data_size), msg->InitializationErrorString().c_str());
      return 0;
    }

    switch (msg->body().cmd_case()) {
      case ::atframe::gw::ss_msg_body::kPost: {
        // post to single client
        if (0 != msg->head().session_id() && 0 == msg->body().post().session_ids_size()) {
          WLOGDEBUG("from server 0x%llx: session 0x%llx send %llu bytes data to client",
                    static_cast<unsigned long long>(source.id),
                    static_cast<unsigned long long>(msg->head().session_id()),
                    static_cast<unsigned long long>(msg->body().post().content().size()));

          int res = mod_.get().get_session_manager().push_data(
              msg->head().session_id(), msg->body().post().content().data(), msg->body().post().content().size());
          if (0 != res) {
            WLOGERROR("from server 0x%llx: session 0x%llx push data failed, res: %d ",
                      static_cast<unsigned long long>(source.id),
                      static_cast<unsigned long long>(msg->head().session_id()), res);

            // session not found, maybe gateway has restarted or server cache expired without remove
            // notify to remove the expired session
            if (::atframe::gateway::error_code_t::EN_ECT_SESSION_NOT_FOUND == res) {
              ::atframe::gw::ss_msg rsp;
              rsp.mutable_head()->set_session_id(msg->head().session_id());
              rsp.mutable_body()->mutable_remove_session();
              res = mod_.get().get_session_manager().post_data(source.id, rsp);
              if (0 != res) {
                WLOGERROR("send remove notify to server 0x%llx failed, res: %d",
                          static_cast<unsigned long long>(source.id), res);
              }
            }
          }
        } else if (0 == msg->body().post().session_ids_size()) {  // broadcast to all actived session
          int res = mod_.get().get_session_manager().broadcast_data(msg->body().post().content().data(),
                                                                    msg->body().post().content().size());
          if (0 != res) {
            WLOGERROR("from server 0x%llx: broadcast data failed, res: %d ", static_cast<unsigned long long>(source.id),
                      res);
          }
        } else {  // multicast to more than one client
          for (int i = 0; i < msg->body().post().session_ids_size(); ++i) {
            int res = mod_.get().get_session_manager().push_data(msg->body().post().session_ids(i),
                                                                 msg->body().post().content().data(),
                                                                 msg->body().post().content().size());
            if (0 != res) {
              WLOGERROR("from server 0x%llx: session 0x%llx push data failed, res: %d ",
                        static_cast<unsigned long long>(source.id),
                        static_cast<unsigned long long>(msg->body().post().session_ids(i)), res);
            }
          }
        }
        break;
      }
      case ::atframe::gw::ss_msg_body::kKickoffSession: {
        WLOGINFO("from server 0x%llx: session 0x%llx kickoff by server", static_cast<unsigned long long>(source.id),
                 static_cast<unsigned long long>(msg->head().session_id()));
        if (0 == msg->head().error_code()) {
          mod_.get().get_session_manager().close(msg->head().session_id(),
                                                 ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
        } else {
          mod_.get().get_session_manager().close(
              msg->head().session_id(), msg->head().error_code(),
              msg->head().error_code() > 0 &&
                  msg->head().error_code() < ::atframe::gateway::close_reason_t::EN_CRT_RECONNECT_BOUND);
        }
        break;
      }
      case ::atframe::gw::ss_msg_body::kSetRouterReq: {
        int res =
            mod_.get().get_session_manager().set_session_router(msg->head().session_id(), msg->body().set_router_req());
        WLOGINFO("from server 0x%llx: session 0x%llx set router to 0x%llx by server, res: %d",
                 static_cast<unsigned long long>(source.id), static_cast<unsigned long long>(msg->head().session_id()),
                 static_cast<unsigned long long>(msg->body().set_router_req()), res);

        ::atframe::gw::ss_msg rsp;
        rsp.mutable_head()->set_session_id(msg->head().session_id());
        rsp.mutable_head()->set_error_code(res);
        rsp.mutable_body()->set_set_router_rsp(msg->body().set_router_req());

        res = mod_.get().get_session_manager().post_data(source.id, rsp);
        if (0 != res) {
          WLOGERROR("send set router response to server 0x%llx failed, res: %d",
                    static_cast<unsigned long long>(source.id), res);
        }
        break;
      }
      default: {
        WLOGERROR("from server 0x%llx: session 0x%llx recv invalid cmd %d", static_cast<unsigned long long>(source.id),
                  static_cast<unsigned long long>(msg->head().session_id()), static_cast<int>(msg->body().cmd_case()));
        break;
      }
    }
    return 0;
  }
};

int main(int argc, char *argv[]) {
  atapp::app app;
  std::shared_ptr<gateway_module> gw_mod = std::make_shared<gateway_module>();
  if (!gw_mod) {
    fprintf(stderr, "create gateway module failed\n");
    return -1;
  }

  // project directory
  {
    std::string proj_dir;
    util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // setup crypt algorithms
  util::crypto::cipher::init_global_algorithm();

  // setup module
  app.add_module(gw_mod);

  // setup cmd
  util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
  cmgr->bind_cmd("kickoff", &gateway_module::cmd_on_kickoff, gw_mod.get())
      ->set_help_msg(
          "kickoff <session id> [reason]          kickoff a session, session can not be reconnected anymore.");

  cmgr->bind_cmd("disconnect", &gateway_module::cmd_on_disconnect, gw_mod.get())
      ->set_help_msg("disconnect <session id> [reason]       disconnect a session, session can be reconnected later.");

  // setup message handle
  app.set_evt_on_forward_response(app_handle_on_forward_response);
  app.set_evt_on_forward_request(app_handle_on_recv(*gw_mod));

  // run
  int ret = app.run(uv_default_loop(), argc, (const char **)argv, NULL);

  // cleanup crypt algorithms
  util::crypto::cipher::cleanup_global_algorithm();

  return ret;
}
