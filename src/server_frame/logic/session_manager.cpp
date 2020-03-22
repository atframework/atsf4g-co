#include <log/log_wrapper.h>

#include <libatbus_protocol.h>
#include <proto_base.h>
#include <time/time_utility.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include "data/player_cache.h"
#include "player_manager.h"
#include "session_manager.h"
#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

session_manager::session_manager() : last_proc_timepoint_(util::time::time_utility::get_now()) {}

session_manager::~session_manager() {}

int session_manager::init() { return 0; }

int session_manager::proc() {
    // 写入时间可配,实时在线统计
    time_t proc_interval = logic_config::me()->get_cfg_logic().session_tick_sec;

    // disabled
    if (proc_interval <= 0) {
        return 0;
    }

    time_t cur_time = util::time::time_utility::get_now();
    cur_time        = cur_time - cur_time % proc_interval;
    if (cur_time > last_proc_timepoint_) {
        last_proc_timepoint_ = cur_time;
        WLOGINFO("online number: %llu clients on %llu atgateway", static_cast<unsigned long long>(all_sessions_.size()),
                 static_cast<unsigned long long>(session_counter_.size()));
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
        WLOGERROR("session registered, failed, bus id: 0x%llx, session id: 0x%llx\n", static_cast<unsigned long long>(key.bus_id),
                  static_cast<unsigned long long>(key.session_id));

        return sess_ptr_t();
    }

    sess_ptr_t &sess   = all_sessions_[key];
    bool        is_new = !sess;
    sess               = std::make_shared<session>();
    if (!sess) {
        WLOGERROR("malloc failed");
        return sess;
    }

    sess->set_key(key);

    if (is_new) {
        // gateway 统计
        session_counter_t::iterator iter_counter = session_counter_.find(key.bus_id);
        if (session_counter_.end() == iter_counter) {
            WLOGINFO("new gateway registered, bus id: 0x%llx", static_cast<unsigned long long>(key.bus_id));
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

    if (0 != reason) {
        sess->send_kickoff(reason);
    }

    session::key_t key = sess->get_key();
    WLOGINFO("session (0x%llx:0x%llx) removed", static_cast<unsigned long long>(key.bus_id), static_cast<unsigned long long>(key.session_id));

    session_index_t::iterator iter = all_sessions_.find(key);
    if (all_sessions_.end() != iter) {
        // gateway 统计
        do {
            session_counter_t::iterator iter_counter = session_counter_.find(key.bus_id);
            if (session_counter_.end() == iter_counter) {
                WLOGERROR("gateway session removed, but gateway not found, bus id: 0x%llx", static_cast<unsigned long long>(key.bus_id));
                break;
            }

            --iter_counter->second;
            if (iter_counter->second <= 0) {
                WLOGINFO("gateway unregistered, bus id: 0x%llx", static_cast<unsigned long long>(key.bus_id));
                session_counter_.erase(iter_counter);
            }
        } while (false);

        // 移除session
        all_sessions_.erase(key);
    }

    // 移除绑定的player
    player_cache::ptr_t u = sess->get_player();
    if (u) {
        sess->set_player(NULL);
        u->set_session(NULL);

        // TODO 统计日志
        // u->GetLogMgr().WLOGLogout();

        // 如果是踢下线，则需要强制保存并移除GameUser对象
        player_manager::me()->remove(u, 0 != reason);
    }
}

void session_manager::remove_all() {
    for (session_index_t::iterator iter = all_sessions_.begin(); iter != all_sessions_.end(); ++iter) {
        if (iter->second) {
            iter->second->send_kickoff(::atframe::gateway::close_reason_t::EN_CRT_SERVER_CLOSED);
        }
    }

    all_sessions_.clear();
    session_counter_.clear();
}

size_t session_manager::size() const { return all_sessions_.size(); }

int32_t session_manager::broadcast_msg_to_client(const hello::CSMsg &msg) {
    size_t msg_buf_len = msg.ByteSizeLong();
    size_t tls_buf_len = atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
    if (msg_buf_len > tls_buf_len) {
        WLOGERROR("broadcast to all gateway failed: require %llu, only have %llu", static_cast<unsigned long long>(msg_buf_len),
                  static_cast<unsigned long long>(tls_buf_len));
        return hello::err::EN_SYS_BUFF_EXTEND;
    }

    ::google::protobuf::uint8 *buf_start =
        reinterpret_cast< ::google::protobuf::uint8 *>(atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
    msg.SerializeWithCachedSizesToArray(buf_start);
    WLOGDEBUG("broadcast msg to all gateway %llu bytes\n%s", static_cast<unsigned long long>(msg_buf_len), protobuf_mini_dumper_get_readable(msg));

    int32_t ret = 0;
    if (all_sessions_.empty()) {
        return ret;
    }

    for (session_counter_t::iterator iter = session_counter_.begin(); session_counter_.end() != iter; ++iter) {
        uint64_t gateway_id = iter->first;
        int32_t  res        = session::broadcast_msg_to_client(gateway_id, buf_start, msg_buf_len);
        if (res < 0) {
            ret = res;
            WLOGERROR("broadcast msg to gateway [0x%llx] failed, res: %d", static_cast<unsigned long long>(gateway_id), res);
        }
    }

    return ret;
}