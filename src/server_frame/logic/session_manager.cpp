// Copyright 2021 atframework
// Created by owent

#include <log/log_wrapper.h>

#include <libatbus_protocol.h>
#include <proto_base.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>

#include <memory>

#include "data/player_cache.h"
#include "logic/player_manager.h"
#include "logic/session_manager.h"

session_manager::session_manager() : last_proc_timepoint_(util::time::time_utility::get_now()) {}

session_manager::~session_manager() {}

int session_manager::init() { return 0; }

int session_manager::proc() {
  // 写入时间可配,实时在线统计
  time_t proc_interval = logic_config::me()->get_logic().session().tick_sec().seconds();

  // disabled
  if (proc_interval <= 0) {
    return 0;
  }

  time_t cur_time = util::time::time_utility::get_now();
  cur_time = cur_time - cur_time % proc_interval;
  if (cur_time > last_proc_timepoint_) {
    last_proc_timepoint_ = cur_time;
    FWLOGINFO("online number: {} clients on {} atgateway", all_sessions_.size(), session_counter_.size());
    // TODO send online stats
  }

  return 0;
}

const session_manager::sess_ptr_t session_manager::find(const session::key_t &key) const {
  session_index_t::const_iterator iter = all_sessions_.find(key);
  if (all_sessions_.end() == iter) {
    return sess_ptr_t();
  }

  return iter->second;
}

session_manager::sess_ptr_t session_manager::find(const session::key_t &key) {
  session_index_t::iterator iter = all_sessions_.find(key);
  if (all_sessions_.end() == iter) {
    return sess_ptr_t();
  }

  return iter->second;
}

session_manager::sess_ptr_t session_manager::create(const session::key_t &key) {
  if (find(key)) {
    FWLOGERROR("session registered, failed, bus id: {:#x}, session id: {}\n", key.bus_id, key.session_id);

    return sess_ptr_t();
  }

  sess_ptr_t &sess = all_sessions_[key];
  bool is_new = !sess;
  sess = std::make_shared<session>();
  if (!sess) {
    FWLOGERROR("malloc failed");
    return sess;
  }

  sess->set_key(key);

  if (is_new) {
    // gateway 统计
    session_counter_t::iterator iter_counter = session_counter_.find(key.bus_id);
    if (session_counter_.end() == iter_counter) {
      FWLOGINFO("new gateway registered, bus id: {:#x}", key.bus_id);
      session_counter_[key.bus_id] = 1;
    } else {
      ++iter_counter->second;
    }
  }
  return sess;
}

void session_manager::remove(const session::key_t &key, int reason) { remove(find(key), reason); }

void session_manager::remove(sess_ptr_t sess, int reason) {
  if (!sess) {
    return;
  }

  if (sess->check_flag(session::flag_t::EN_SESSION_FLAG_CLOSED)) {
    return;
  }

  if (0 != reason) {
    sess->send_kickoff(reason);
  }

  session::key_t key = sess->get_key();
  FWLOGINFO("session {}({:#x}:{:#x}) removed", reinterpret_cast<const void *>(sess.get()), key.bus_id, key.session_id);

  {
    session_index_t::iterator iter = all_sessions_.find(key);
    if (all_sessions_.end() != iter && iter->second == sess) {
      // gateway 统计
      do {
        session_counter_t::iterator iter_counter = session_counter_.find(key.bus_id);
        if (session_counter_.end() == iter_counter) {
          FWLOGERROR("gateway session removed, but gateway not found, bus id: {:#x}", key.bus_id);
          break;
        }

        --iter_counter->second;
        if (iter_counter->second <= 0) {
          FWLOGINFO("gateway unregistered, bus id: {:#x}", key.bus_id);
          session_counter_.erase(iter_counter);
        }
      } while (false);

      // 移除session
      all_sessions_.erase(iter);
    }
  }

  if (sess) {
    sess->set_flag(session::flag_t::EN_SESSION_FLAG_CLOSED, true);
  }

  // 移除绑定的player
  player_cache::ptr_t u = sess->get_player();
  if (u) {
    sess->set_player(nullptr);
    sess_ptr_t check_session = u->get_session();
    if (!check_session || check_session == sess) {
      rpc::context ctx;
      u->set_session(ctx, nullptr);
      // TODO 统计日志
      // 如果是踢下线，则需要强制保存并移除GameUser对象
      auto remove_player_task = rpc::async_invoke(ctx, "session_manager.remove", [u, reason](rpc::context &ctx) {
        return RPC_AWAIT_CODE_RESULT(player_manager::me()->remove(ctx, u, 0 != reason));
      });
      if (remove_player_task.is_error()) {
        FWLOGERROR("async_invoke task to remove player {}:{} failed, res: {}({})", u->get_zone_id(), u->get_user_id(),
                   *remove_player_task.get_error(),
                   protobuf_mini_dumper_get_error_msg(*remove_player_task.get_error()));
      }
    }
  }
}

void session_manager::remove_all(int32_t reason) {
  for (session_index_t::iterator iter = all_sessions_.begin(); iter != all_sessions_.end(); ++iter) {
    if (iter->second) {
      iter->second->set_flag(session::flag_t::EN_SESSION_FLAG_CLOSED, true);
      iter->second->send_kickoff(reason);
    }
  }

  std::shared_ptr<session_index_t> all_sessions = std::make_shared<session_index_t>();
  all_sessions_.swap(*all_sessions);
  session_counter_.clear();

  if (!all_sessions->empty()) {
    rpc::context ctx;
    auto remove_player_task = rpc::async_invoke(ctx, "session_manager.remove_all", [all_sessions](rpc::context &ctx) {
      for (auto &session : *all_sessions) {
        if (!session.second) {
          continue;
        }
        player_cache::ptr_t u = session.second->get_player();
        if (u) {
          session.second->set_player(nullptr);
          sess_ptr_t check_session = u->get_session();
          if (!check_session || check_session == session.second) {
            u->set_session(ctx, nullptr);
            // 不能直接保存，不然如果玩家数很多依次保存会超时
            player_manager::me()->add_save_schedule(u->get_user_id(), u->get_zone_id(), true);
          }
        }
      }
      return 0;
    });
    if (remove_player_task.is_error()) {
      FWLOGERROR("async_invoke task to remove player failed, res: {}({})", *remove_player_task.get_error(),
                 protobuf_mini_dumper_get_error_msg(*remove_player_task.get_error()));
    }
  }
}

size_t session_manager::size() const { return all_sessions_.size(); }

int32_t session_manager::broadcast_msg_to_client(const atframework::CSMsg &msg) {
  size_t msg_buf_len = msg.ByteSizeLong();
  size_t tls_buf_len =
      atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
  if (msg_buf_len > tls_buf_len) {
    FWLOGERROR("broadcast to all gateway failed: require {}, only have {}", msg_buf_len, tls_buf_len);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUFF_EXTEND;
  }

  ::google::protobuf::uint8 *buf_start = reinterpret_cast< ::google::protobuf::uint8 *>(
      atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
  msg.SerializeWithCachedSizesToArray(buf_start);
  FWLOGDEBUG("broadcast msg to all gateway {} bytes\n{}", msg_buf_len, protobuf_mini_dumper_get_readable(msg));

  int32_t ret = 0;
  if (all_sessions_.empty()) {
    return ret;
  }

  for (session_counter_t::iterator iter = session_counter_.begin(); session_counter_.end() != iter; ++iter) {
    uint64_t gateway_id = iter->first;
    int32_t res = session::broadcast_msg_to_client(gateway_id, buf_start, msg_buf_len);
    if (res < 0) {
      ret = res;
      FWLOGERROR("broadcast msg to gateway [{:#x}] failed, res: {}", gateway_id, res);
    }
  }

  return ret;
}