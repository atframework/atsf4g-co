#include <algorithm/hash.h>
#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/cs_msg_dispatcher.h>
#include <proto_base.h>

#include <utility/environment_helper.h>
#include <utility/protobuf_mini_dumper.h>

#include "player_cache.h"
#include "session.h"


session::key_t::key_t() : bus_id(0), session_id(0) {}
session::key_t::key_t(const std::pair<uint64_t, uint64_t> &p) : bus_id(p.first), session_id(p.second) {}

bool session::key_t::operator==(const key_t &r) const { return bus_id == r.bus_id && session_id == r.session_id; }

bool session::key_t::operator!=(const key_t &r) const { return !((*this) == r); }

bool session::key_t::operator<(const key_t &r) const {
    if (bus_id != r.bus_id) {
        return bus_id < r.bus_id;
    }
    return session_id < r.session_id;
}

bool session::key_t::operator<=(const key_t &r) const { return (*this) < r || (*this) == r; }

bool session::key_t::operator>(const key_t &r) const {
    if (bus_id != r.bus_id) {
        return bus_id > r.bus_id;
    }
    return session_id > r.session_id;
}

bool session::key_t::operator>=(const key_t &r) const { return (*this) > r || (*this) == r; }

session::session() : login_task_id_(0) {
    id_.bus_id     = 0;
    id_.session_id = 0;
}

session::~session() {
    WLOGDEBUG("session src_pd=0x%llx, idx=0x%llx destroyed", static_cast<unsigned long long>(id_.bus_id), static_cast<unsigned long long>(id_.session_id));
}

void session::set_player(std::shared_ptr<player_cache> u) { player_ = u; }

std::shared_ptr<player_cache> session::get_player() const { return player_.lock(); }

int32_t session::send_msg_to_client(hello::CSMsg &msg) {
    size_t msg_buf_len = msg.ByteSizeLong();
    size_t tls_buf_len = atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
    if (msg_buf_len > tls_buf_len) {
        WLOGERROR("send to gateway [0x%llx, 0x%llx] failed: require %llu, only have %llu", static_cast<unsigned long long>(id_.bus_id),
                  static_cast<unsigned long long>(id_.session_id), static_cast<unsigned long long>(msg_buf_len), static_cast<unsigned long long>(tls_buf_len));
        return hello::err::EN_SYS_BUFF_EXTEND;
    }

    ::google::protobuf::uint8 *buf_start =
        reinterpret_cast< ::google::protobuf::uint8 *>(atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
    msg.SerializeWithCachedSizesToArray(buf_start);
    WLOGDEBUG("send msg to client:[0x%llx, 0x%llx] %llu bytes\n%s", static_cast<unsigned long long>(id_.bus_id),
              static_cast<unsigned long long>(id_.session_id), static_cast<unsigned long long>(msg_buf_len), protobuf_mini_dumper_get_readable(msg));

    return send_msg_to_client(buf_start, msg_buf_len);
}

int32_t session::send_msg_to_client(const void *msg_data, size_t msg_size) {
    // send data using dispatcher
    return cs_msg_dispatcher::me()->send_data(get_key().bus_id, get_key().session_id, msg_data, msg_size);
}

int32_t session::broadcast_msg_to_client(uint64_t bus_id, const hello::CSMsg &msg) {
    size_t msg_buf_len = msg.ByteSizeLong();
    size_t tls_buf_len = atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
    if (msg_buf_len > tls_buf_len) {
        WLOGERROR("broadcast to gateway [0x%llx] failed: require %llu, only have %llu", static_cast<unsigned long long>(bus_id),
                  static_cast<unsigned long long>(msg_buf_len), static_cast<unsigned long long>(tls_buf_len));
        return hello::err::EN_SYS_BUFF_EXTEND;
    }

    ::google::protobuf::uint8 *buf_start =
        reinterpret_cast< ::google::protobuf::uint8 *>(atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
    msg.SerializeWithCachedSizesToArray(buf_start);
    WLOGDEBUG("broadcast msg to gateway [0x%llx] %llu bytes\n%s", static_cast<unsigned long long>(bus_id), static_cast<unsigned long long>(msg_buf_len),
              protobuf_mini_dumper_get_readable(msg));

    return broadcast_msg_to_client(bus_id, buf_start, msg_buf_len);
}

int32_t session::broadcast_msg_to_client(uint64_t bus_id, const void *msg_data, size_t msg_size) {
    // broadcast data using dispatcher
    return cs_msg_dispatcher::me()->broadcast_data(bus_id, msg_data, msg_size);
}

bool session::compare_callback::operator()(const key_t &l, const key_t &r) const {
    if (l.bus_id != r.bus_id) {
        return l.session_id < r.session_id;
    }
    return l.bus_id < r.bus_id;
}

size_t session::compare_callback::operator()(const key_t &hash_obj) const {
    // std::hash also use fnv1 hash algorithm, but fnv1a sometime has better random
    return util::hash::hash_fnv1a<size_t>(&hash_obj.bus_id, sizeof(hash_obj.bus_id)) ^
           util::hash::hash_fnv1<size_t>(&hash_obj.session_id, sizeof(hash_obj.session_id));
}

int32_t session::send_kickoff(int32_t reason) {
    // send kickoff using dispatcher
    return cs_msg_dispatcher::me()->send_kickoff(get_key().bus_id, get_key().session_id, reason);
}