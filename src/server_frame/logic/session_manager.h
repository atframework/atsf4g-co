#ifndef LOGIC_SESSION_MANAGER_H
#define LOGIC_SESSION_MANAGER_H

#pragma once

#include <design_pattern/singleton.h>

#include <utility/environment_helper.h>
#include <data/session.h>

namespace hello{
    class CSMsg;
}

class session_manager : public util::design_pattern::singleton<session_manager> {
public:
    typedef session::ptr_t sess_ptr_t;
    typedef UTIL_ENV_AUTO_MAP(session::key_t, sess_ptr_t, session::compare_callback) session_index_t;
    typedef std::map<uint64_t, size_t> session_counter_t;

protected:
    session_manager();
    ~session_manager();

public:
    int init();

    int proc();

    const sess_ptr_t find(const session::key_t& key) const ;
    sess_ptr_t find(const session::key_t& key);

    sess_ptr_t create(const session::key_t& key);

    void remove(const session::key_t& key, int reason = 0);
    void remove(sess_ptr_t sess, int reason = 0);

    void remove_all();

    size_t size() const;

    int32_t broadcast_msg_to_client(const hello::CSMsg &msg);
private:
    session_counter_t session_counter_;
    session_index_t all_sessions_;
    time_t last_proc_timepoint_;
};

#endif