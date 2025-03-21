// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include <uv.h>

#include <libatbus_protocol.h>

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <libatbus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "session_manager.h"  // NOLINT: build/include_subdir

static int app_handle_on_forward_response(atapp::app &app, const atapp::app::message_sender_t &source,
                                          const atapp::app::message_t &m, int32_t error_code) {
  if (error_code >= 0) {
    return 0;
  }

  FWLOGERROR("send data from {:#x} to {:#x} failed, msg sequence: {}, code: {}", app.get_id(), source.id,
             m.message_sequence, error_code);
  return 0;
}

class gateway_module : public ::atapp::module_impl {
 public:
  gateway_module() {}
  virtual ~gateway_module() {}

 public:
  int init() override {
    gw_mgr_.get_conf().version = 1;

    int res = 0;
    if ("inner" == gw_mgr_.get_conf().origin_conf.listen().type()) {
      gw_mgr_.init(get_app(), [this]() { return this->gateway_module::create_proto_inner(); });

      gw_mgr_.set_on_create_session([this](::atframework::gateway::session *sess, uv_stream_t *handle) -> int {
        return this->proto_inner_callback_on_create_session(sess, handle);
      });

      // init callbacks
      proto_callbacks_.write_fn = [this](::atframework::gateway::libatgw_protocol_api *proto, void *buffer, size_t sz,
                                         bool *is_done) -> int {
        return this->proto_inner_callback_on_write(proto, buffer, sz, is_done);
      };

      proto_callbacks_.message_fn = [this](::atframework::gateway::libatgw_protocol_api *proto, const void *buffer,
                                           size_t sz) -> int {
        return this->proto_inner_callback_on_message(proto, buffer, sz);
      };
      proto_callbacks_.new_session_fn = [this](::atframework::gateway::libatgw_protocol_api *proto,
                                               uint64_t &sess_id) -> int {
        return this->proto_inner_callback_on_new_session(proto, sess_id);
      };
      proto_callbacks_.reconnect_fn = [this](::atframework::gateway::libatgw_protocol_api *proto,
                                             uint64_t sess_id) -> int {
        return this->proto_inner_callback_on_reconnect(proto, sess_id);
      };
      proto_callbacks_.close_fn = [this](::atframework::gateway::libatgw_protocol_api *proto, int reason) -> int {
        return this->proto_inner_callback_on_close(proto, reason);
      };
      proto_callbacks_.on_handshake_done_fn = [this](::atframework::gateway::libatgw_protocol_api *proto,
                                                     int status) -> int {
        return this->proto_inner_callback_on_handshake_done(proto, status);
      };

      proto_callbacks_.on_handshake_update_fn = [this](::atframework::gateway::libatgw_protocol_api *proto,
                                                       int status) -> int {
        return this->proto_inner_callback_on_update_done(proto, status);
      };

      proto_callbacks_.on_error_fn = [this](::atframework::gateway::libatgw_protocol_api *proto, const char *filename,
                                            int line, int errcode, const char *errmsg) -> int {
        return this->proto_inner_callback_on_error(proto, filename, line, errcode, errmsg);
      };

    } else {
      FWLOGERROR("listen type {} not supported.", gw_mgr_.get_conf().origin_conf.listen().type());
      return -1;
    }

    // init limits
    res = gw_mgr_.listen_all();
    if (res <= 0) {
      FWLOGERROR("nothing listened for client, please see log for more details.");
      return -1;
    }

    return 0;
  }

  int reload() override {
    ++gw_mgr_.get_conf().version;

    get_app()->parse_configures_into(gw_mgr_.get_conf().origin_conf, "atgateway");

    // crypt
    ::atframework::gateway::session_manager::crypt_conf_t &crypt_conf = gw_mgr_.get_conf().crypt;
    crypt_conf.default_key = gw_mgr_.get_conf().origin_conf.client().crypt().key();
    crypt_conf.update_interval = gw_mgr_.get_conf().origin_conf.client().crypt().update_interval().seconds();
    crypt_conf.type = gw_mgr_.get_conf().origin_conf.client().crypt().type();
    crypt_conf.switch_secret_type =
        ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DIRECT);
    crypt_conf.client_mode = false;

    crypt_conf.dh_param = gw_mgr_.get_conf().origin_conf.client().crypt().dhparam();
    if (!crypt_conf.dh_param.empty()) {
      if (0 == UTIL_STRFUNC_STRNCASE_CMP("ecdh:", crypt_conf.dh_param.c_str(), 5)) {
        crypt_conf.switch_secret_type =
            ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_ECDH);
      } else {
        crypt_conf.switch_secret_type =
            ::atframework::gw::v1::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(switch_secret_t, EN_SST_DH);
      }
    }

    // protocol reload
    if ("inner" == gw_mgr_.get_conf().origin_conf.listen().type()) {
      int res = ::atframework::gateway::libatgw_protocol_sdk::global_reload(crypt_conf);
      if (res < 0) {
        FWLOGERROR("reload inner protocol global configure failed, res: {}", res);
        return res;
      }
    }

    return 0;
  }

  int stop() override {
    gw_mgr_.reset();
    return 0;
  }

  int timeout() override { return 0; }

  const char *name() const override { return "gateway_module"; }

  int tick() override { return gw_mgr_.tick(); }

  void cleanup() override { get_session_manager().cleanup(); }

  inline ::atframework::gateway::session_manager &get_session_manager() { return gw_mgr_; }
  inline const ::atframework::gateway::session_manager &get_session_manager() const { return gw_mgr_; }

 private:
  std::unique_ptr<::atframework::gateway::libatgw_protocol_api> create_proto_inner() {
    ::atframework::gateway::libatgw_protocol_sdk *ret =
        new (std::nothrow)::atframework::gateway::libatgw_protocol_sdk();
    if (nullptr != ret) {
      ret->set_callbacks(&proto_callbacks_);
      ret->set_write_header_offset(sizeof(uv_write_t));
    }

    return std::unique_ptr<::atframework::gateway::libatgw_protocol_api>(ret);
  }

  static void proto_inner_callback_on_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    // alloc read buffer from session proto
    ::atframework::gateway::session *sess = reinterpret_cast<::atframework::gateway::session *>(handle->data);
    assert(sess);

    if (nullptr == sess) {
      if (nullptr != buf) {
        buf->base = nullptr;
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
    if (nullptr == stream) {
      return;
    }

    ::atframework::gateway::session *sess = reinterpret_cast<::atframework::gateway::session *>(stream->data);
    assert(sess);
    if (nullptr == sess) {
      return;
    }

    // 如果正处于关闭阶段，忽略所有数据
    if (sess->check_flag(::atframework::gateway::session::flag_t::EN_FT_CLOSING)) {
      return;
    }

    if (nullptr == sess->get_manager()) {
      return;
    }
    ::atframework::gateway::session_manager *mgr = sess->get_manager();

    // if no more data or EAGAIN or break by signal, just ignore
    if (0 == nread || UV_EAGAIN == nread || UV_EAI_AGAIN == nread || UV_EINTR == nread) {
      return;
    }

    // if network error or reset by peer, move session into reconnect queue
    if (nread < 0) {
      // notify to close fd
      mgr->close(sess->get_id(), ::atframework::gateway::close_reason_t::EN_CRT_RESET, true);
      return;
    }

    if (nullptr != buf) {
      // in case of deallocator session in read callback.
      ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();
      sess_holder->on_read(static_cast<int>(nread), buf->base, static_cast<size_t>(nread));
    }
  }

  int proto_inner_callback_on_create_session(::atframework::gateway::session *sess, uv_stream_t *handle) {
    if (nullptr == sess) {
      FWLOGERROR("{}", "create session with inner proto without session object");
      return 0;
    }

    if (nullptr == handle) {
      FWLOGERROR("{}", "create session with inner proto without handle");
      return 0;
    }

    // start read
    handle->data = sess;
    uv_read_start(handle, proto_inner_callback_on_read_alloc, proto_inner_callback_on_read_data);

    return 0;
  }

  static void proto_inner_callback_on_written_fn(uv_write_t *req, int status) {
    ::atframework::gateway::session *sess = reinterpret_cast<::atframework::gateway::session *>(req->data);
    assert(sess);

    if (nullptr != sess) {
      sess->set_flag(::atframework::gateway::session::flag_t::EN_FT_WRITING_FD, false);
      sess->on_write_done(status);
    }
  }

  int proto_inner_callback_on_write(::atframework::gateway::libatgw_protocol_api *proto, void *buffer, size_t sz,
                                    bool *is_done) {
    if (nullptr == proto || nullptr == buffer) {
      if (nullptr != is_done) {
        *is_done = true;
      }
      return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
    }

    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (nullptr == sess) {
      if (nullptr != is_done) {
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
      sess->set_flag(::atframework::gateway::session::flag_t::EN_FT_WRITING_FD, true);

      ret = uv_write(req, sess->get_uv_stream(), bufs, 1, proto_inner_callback_on_written_fn);
      if (0 != ret) {
        sess->set_flag(::atframework::gateway::session::flag_t::EN_FT_WRITING_FD, false);
        FWLOGERROR("send data to proto {} failed, res: ", reinterpret_cast<const void *>(proto), ret);
      }
    } while (false);

    if (nullptr != is_done) {
      // if not writting, notify write finished
      *is_done = !sess->check_flag(::atframework::gateway::session::flag_t::EN_FT_WRITING_FD);
    }
    return ret;
  }

  int proto_inner_callback_on_message(::atframework::gateway::libatgw_protocol_api *proto, const void *buffer,
                                      size_t sz) {
    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (nullptr == sess) {
      FWLOGERROR("recv message from proto object {} length, but has no session", reinterpret_cast<const void *>(proto));
      return -1;
    }
    ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    ::atframework::gw::ss_msg post_msg;
    post_msg.mutable_head()->set_session_id(sess_holder->get_id());

    ::atframework::gw::ss_body_post *post = post_msg.mutable_body()->mutable_post();
    if (nullptr != post) {
      post->add_session_ids(sess_holder->get_id());
      post->set_content(buffer, sz);
    }

    // send to router
    if (0 != sess_holder->get_router_id()) {
      FWLOGDEBUG("session {} send {} bytes data to server {}({})", sess_holder->get_id(), sz,
                 sess_holder->get_router_id(), sess_holder->get_router_name());

      return gw_mgr_.post_data(sess_holder->get_router_id(), post_msg);
    } else if (!sess_holder->get_router_name().empty()) {
      FWLOGDEBUG("session {} send {} bytes data to server {}({})", sess_holder->get_id(), sz,
                 sess_holder->get_router_id(), sess_holder->get_router_name());

      return gw_mgr_.post_data(sess_holder->get_router_name(), post_msg);
    }

    FWLOGERROR("session {} send {} bytes data failed, not router", sess_holder->get_id(), sz);
    return -1;
  }

  int proto_inner_callback_on_new_session(::atframework::gateway::libatgw_protocol_api *proto, uint64_t &sess_id) {
    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (nullptr == sess) {
      FWLOGERROR("recv new session message from proto object {} length, but has no session",
                 reinterpret_cast<const void *>(proto));
      return -1;
    }
    ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    int ret = sess_holder->init_new_session();
    sess_id = sess_holder->get_id();
    if (0 != ret) {
      FWLOGERROR("create new session failed, ret: {}", ret);
    }
    gw_mgr_.assign_default_router(*sess_holder);

    return ret;
  }

  int proto_inner_callback_on_reconnect(::atframework::gateway::libatgw_protocol_api *proto, uint64_t sess_id) {
    if (nullptr == proto) {
      FWLOGERROR("{}", "parameter error");
      return -1;
    }

    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (nullptr == sess) {
      FWLOGERROR("close session from proto object {} length, but has no session",
                 reinterpret_cast<const void *>(proto));
      return -1;
    }
    ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    // check proto reconnect access
    if (sess_holder->check_flag(::atframework::gateway::session::flag_t::EN_FT_INITED)) {
      FWLOGERROR("try to reconnect session {}({}) from {}, but already inited", sess_holder->get_id(),
                 reinterpret_cast<const void *>(sess), sess_id);
      return -1;
    }

    int res = gw_mgr_.reconnect(*sess_holder, sess_id);
    if (0 != res) {
      if (::atframework::gateway::error_code_t::EN_ECT_SESSION_NOT_FOUND != res &&
          ::atframework::gateway::error_code_t::EN_ECT_REFUSE_RECONNECT != res) {
        FWLOGERROR("reconnect session {}({}) from {} failed, res: {}", sess_holder->get_id(),
                   reinterpret_cast<const void *>(sess), sess_id, res);
      } else {
        FWLOGINFO("reconnect session {}({})  from {} failed, res: {}", sess_holder->get_id(),
                  reinterpret_cast<const void *>(sess), sess_id, res);
      }
    } else {
      FWLOGINFO("reconnect session {}({})  success", sess_holder->get_id(), reinterpret_cast<const void *>(sess));
    }
    return res;
  }

  int proto_inner_callback_on_close(::atframework::gateway::libatgw_protocol_api *proto, int reason) {
    if (nullptr == proto) {
      FWLOGERROR("{}", "parameter error");
      return -1;
    }

    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (nullptr == sess) {
      FWLOGERROR("close session from proto object {} length, but has no session",
                 reinterpret_cast<const void *>(proto));
      return -1;
    }
    ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();

    if (!sess_holder->check_flag(::atframework::gateway::session::flag_t::EN_FT_CLOSING)) {
      // if network EOF or network error, do not close session, but wait for reconnect
      bool enable_reconnect = reason <= ::atframework::gateway::close_reason_t::EN_CRT_RECONNECT_BOUND;
      if (enable_reconnect) {
        FWLOGINFO("session {}({}) closed", sess_holder->get_id(), reinterpret_cast<const void *>(sess));
      } else {
        FWLOGINFO("session {}({}) closed disable reconnect", sess_holder->get_id(),
                  reinterpret_cast<const void *>(sess));
      }
      if (nullptr != sess_holder->get_manager()) {
        if (sess_holder->get_manager()->close(sess_holder->get_id(), reason, enable_reconnect) < 0) {
          sess_holder->close(reason);
        }
      } else {
        sess_holder->close(reason);
      }
    } else {
      if (sess_holder->check_flag(::atframework::gateway::session::flag_t::EN_FT_RECONNECTED)) {
        FWLOGINFO("session {}({}) reconnected and release old connection", sess_holder->get_id(),
                  reinterpret_cast<const void *>(sess));
      } else {
        FWLOGINFO("session {}({}) closed", sess_holder->get_id(), reinterpret_cast<const void *>(sess));
      }
    }

    // TODO if it's closed manually, remove it from manager
    return 0;
  }

  int proto_inner_callback_on_handshake_done(::atframework::gateway::libatgw_protocol_api *proto, int status) {
    if (0 == status) {
      ::atframework::gateway::session *sess =
          reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
      if (nullptr == sess) {
        FWLOGERROR("handshake done from proto object {} length, but has no session",
                   reinterpret_cast<const void *>(proto));
        return -1;
      }
      ::atframework::gateway::session::ptr_t sess_holder = sess->shared_from_this();

      FWLOGINFO("session {}({}) handshake done\n{}", sess_holder->get_id(), reinterpret_cast<const void *>(sess),
                proto->get_info());

      int res = gw_mgr_.active_session(sess_holder);
      if (0 != res) {
        FWLOGERROR("session {} send new session to router server failed, res: {}", sess->get_id(), res);
        return -1;
      }
    } else {
      ::atframework::gateway::session *sess =
          reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
      if (nullptr == sess) {
        FWLOGERROR("session NONE handshake failed,res: {}\n{}", status, proto->get_info());
      } else {
        FWLOGERROR("session {}({}) handshake failed,res: {},\n{}", sess->get_id(), reinterpret_cast<const void *>(sess),
                   status, proto->get_info());
      }
    }
    return 0;
  }

  int proto_inner_callback_on_update_done(::atframework::gateway::libatgw_protocol_api *proto, int status) {
    ::atframework::gateway::session *sess =
        reinterpret_cast<::atframework::gateway::session *>(proto->get_private_data());
    if (0 == status) {
      if (nullptr == sess) {
        FWLOGDEBUG("session NONE handshake update success\n{}", proto->get_info());
      } else {
        FWLOGDEBUG("session {}({}) handshake update success\n{}", sess->get_id(), reinterpret_cast<const void *>(sess),
                   proto->get_info());
      }
    } else {
      if (nullptr == sess) {
        FWLOGERROR("session NONE handshake update failed,res: {}\n{}", status, proto->get_info());
      } else {
        FWLOGERROR("session {}({}) handshake update failed,res: {},\n{}", sess->get_id(),
                   reinterpret_cast<const void *>(sess), status, proto->get_info());
      }
    }
    return 0;
  }

  int proto_inner_callback_on_error(::atframework::gateway::libatgw_protocol_api *, const char *filename, int line,
                                    int errcode, const char *errmsg) {
    if (atfw::util::log::log_wrapper::check_level(WDTLOGGETCAT(atfw::util::log::log_wrapper::categorize_t::DEFAULT),
                                                  atfw::util::log::log_wrapper::level_t::LOG_LW_ERROR)) {
      WDTLOGGETCAT(atfw::util::log::log_wrapper::categorize_t::DEFAULT)
          ->log(atfw::util::log::log_wrapper::caller_info_t(atfw::util::log::log_wrapper::level_t::LOG_LW_ERROR,
                                                            "Error", filename, line, "anonymous"),
                "error code %d, msg: %s", errcode, errmsg);
    }
    return 0;
  }

 public:
  int cmd_on_kickoff(util::cli::callback_param params) {
    if (params.get_params_number() < 1) {
      FWLOGERROR("{}", "kickoff command require session id");
      return 0;
    }

    ::atframework::gateway::session::id_t sess_id = 0;
    atfw::util::string::str2int(sess_id, params[0]->to_string());

    int reason = ::atframework::gateway::close_reason_t::EN_CRT_KICKOFF;
    if (params.get_params_number() > 1) {
      atfw::util::string::str2int(reason, params[1]->to_string());
    }

    // do not allow reconnect
    int res = gw_mgr_.close(sess_id, reason, false);
    if (0 != res) {
      FWLOGERROR("command kickoff session {} failed, res: {}", sess_id, res);
    } else {
      FWLOGINFO("command kickoff session {} success", sess_id);
    }

    return 0;
  }

  int cmd_on_disconnect(util::cli::callback_param params) {
    if (params.get_params_number() < 1) {
      FWLOGERROR("{}", "disconnect command require session id");
      return 0;
    }

    ::atframework::gateway::session::id_t sess_id = 0;
    atfw::util::string::str2int(sess_id, params[0]->to_string());

    int reason = ::atframework::gateway::close_reason_t::EN_CRT_RESET;
    if (params.get_params_number() > 1) {
      atfw::util::string::str2int(reason, params[1]->to_string());
    }

    // do not allow reconnect
    int res = gw_mgr_.close(sess_id, reason, true);
    if (0 != res) {
      FWLOGERROR("command disconnect session {} failed, res: {}", sess_id, res);
    } else {
      FWLOGINFO("command disconnect session {} success", sess_id);
    }

    return 0;
  }

 private:
  ::atframework::gateway::session_manager gw_mgr_;
  ::atframework::gateway::libatgw_protocol_api::proto_callbacks_t proto_callbacks_;
};

struct app_handle_on_recv {
  std::reference_wrapper<gateway_module> mod_;
  app_handle_on_recv(gateway_module &mod) : mod_(mod) {}

  int operator()(::atapp::app &, const atapp::app::message_sender_t &source, const atapp::app::message_t &message) {
    if (nullptr == message.data || 0 == message.data_size) {
      return 0;
    }

    ::google::protobuf::ArenaOptions arena_options;
    arena_options.initial_block_size = (message.data_size + 256) & 255;
    ::google::protobuf::Arena arena(arena_options);
#if defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000
    ::atframework::gw::ss_msg *msg =
        ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::Arena::Create<::atframework::gw::ss_msg>(&arena);
#else
    ::atframework::gw::ss_msg *msg =
        ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::Arena::CreateMessage<::atframework::gw::ss_msg>(&arena);
#endif
    assert(msg);

    if (false == msg->ParseFromArray(message.data, static_cast<int>(message.data_size))) {
      FWLOGDEBUG("from server {}: session {} parse {} bytes data failed: {}", source.id, msg->head().session_id(),
                 message.data_size, msg->InitializationErrorString());
      return 0;
    }

    switch (msg->body().cmd_case()) {
      case ::atframework::gw::ss_msg_body::kPost: {
        // post to single client
        if (0 != msg->head().session_id() && 0 == msg->body().post().session_ids_size()) {
          FWLOGDEBUG("from server {}: session {} send {} bytes data to client", source.id, msg->head().session_id(),
                     msg->body().post().content().size());

          int res = mod_.get().get_session_manager().push_data(
              msg->head().session_id(), msg->body().post().content().data(), msg->body().post().content().size());
          if (0 != res) {
            FWLOGERROR("from server {}: session {} push data failed, res: {}", source.id, msg->head().session_id(),
                       res);

            // session not found, maybe gateway has restarted or server cache expired without remove
            // notify to remove the expired session
            if (::atframework::gateway::error_code_t::EN_ECT_SESSION_NOT_FOUND == res) {
              ::atframework::gw::ss_msg rsp;
              rsp.mutable_head()->set_session_id(msg->head().session_id());
              rsp.mutable_body()->mutable_remove_session();
              res = mod_.get().get_session_manager().post_data(source.id, rsp);
              if (0 != res) {
                FWLOGERROR("send remove notify to server {} failed, res: {}", source.id, res);
              }
            }
          }
        } else if (0 == msg->body().post().session_ids_size()) {  // broadcast to all actived session
          int res = mod_.get().get_session_manager().broadcast_data(msg->body().post().content().data(),
                                                                    msg->body().post().content().size());
          if (0 != res) {
            FWLOGERROR("from server {}: broadcast data failed, res: {}", source.id, res);
          }
        } else {  // multicast to more than one client
          for (int i = 0; i < msg->body().post().session_ids_size(); ++i) {
            int res = mod_.get().get_session_manager().push_data(msg->body().post().session_ids(i),
                                                                 msg->body().post().content().data(),
                                                                 msg->body().post().content().size());
            if (0 != res) {
              FWLOGERROR("from server {}: session {} push data failed, res: {}", source.id,
                         msg->body().post().session_ids(i), res);
            }
          }
        }
        break;
      }
      case ::atframework::gw::ss_msg_body::kKickoffSession: {
        FWLOGINFO("from server {}: session {} kickoff by server", source.id, msg->head().session_id());
        if (0 == msg->head().error_code()) {
          mod_.get().get_session_manager().close(msg->head().session_id(),
                                                 ::atframework::gateway::close_reason_t::EN_CRT_KICKOFF);
        } else {
          mod_.get().get_session_manager().close(
              msg->head().session_id(), msg->head().error_code(),
              msg->head().error_code() > 0 &&
                  msg->head().error_code() < ::atframework::gateway::close_reason_t::EN_CRT_RECONNECT_BOUND);
        }
        break;
      }
      case ::atframework::gw::ss_msg_body::kSetRouterReq: {
        int res = mod_.get().get_session_manager().set_session_router(
            msg->head().session_id(), msg->body().set_router_req().target_service_id(),
            msg->body().set_router_req().target_service_name());
        FWLOGINFO("from server {}: session {} set router to {}({}) by server, res: {}", source.id,
                  msg->head().session_id(), msg->body().set_router_req().target_service_id(),
                  msg->body().set_router_req().target_service_name(), res);

        ::atframework::gw::ss_msg rsp;
        rsp.mutable_head()->set_session_id(msg->head().session_id());
        rsp.mutable_head()->set_error_code(res);
        *rsp.mutable_body()->mutable_set_router_rsp() = msg->body().set_router_req();

        res = mod_.get().get_session_manager().post_data(source.id, rsp);
        if (0 != res) {
          FWLOGERROR("send set router response to server {} failed, res: {}", source.id, res);
        }
        break;
      }
      default: {
        FWLOGERROR("from server {}: session {} recv invalid cmd {}", source.id, msg->head().session_id(),
                   static_cast<int>(msg->body().cmd_case()));
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
    atfw::util::file_system::dirname(__FILE__, 0, proj_dir, 4);
    atfw::util::log::log_formatter::set_project_directory(proj_dir.c_str(), proj_dir.size());
  }

  // setup crypt algorithms
  atfw::util::crypto::cipher::init_global_algorithm();

  // setup module
  app.add_module(gw_mod);

  // setup cmd
  atfw::util::cli::cmd_option_ci::ptr_type cmgr = app.get_command_manager();
  cmgr->bind_cmd("kickoff", &gateway_module::cmd_on_kickoff, gw_mod.get())
      ->set_help_msg(
          "kickoff <session id> [reason]          kickoff a session, session can not be reconnected anymore.");

  cmgr->bind_cmd("disconnect", &gateway_module::cmd_on_disconnect, gw_mod.get())
      ->set_help_msg("disconnect <session id> [reason]       disconnect a session, session can be reconnected later.");

  // setup message handle
  app.set_evt_on_forward_response(app_handle_on_forward_response);
  app.set_evt_on_forward_request(app_handle_on_recv(*gw_mod));

  // run
  int ret = app.run(uv_default_loop(), argc, (const char **)argv, nullptr);

  // cleanup crypt algorithms
  atfw::util::crypto::cipher::cleanup_global_algorithm();

  return ret;
}
