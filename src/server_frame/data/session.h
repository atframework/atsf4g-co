#ifndef DATA_SESSION_H
#define DATA_SESSION_H

#pragma once

#include <std/smart_ptr.h>
#include <design_pattern/noncopyable.h>
#include <design_pattern/nomovable.h>

namespace hello {
    class CSMsg;
}

class player_cache;

class session {
public:
    typedef std::shared_ptr<session> ptr_t;
    struct key_t {
        uint64_t bus_id;
        uint64_t session_id;

        key_t();
        key_t(const std::pair<uint64_t, uint64_t> &p);

        bool operator==(const key_t &r) const;
        bool operator!=(const key_t &r) const;
        bool operator<(const key_t &r) const;
        bool operator<=(const key_t &r) const;
        bool operator>(const key_t &r) const;
        bool operator>=(const key_t &r) const;
    };

    struct flag_t {
        enum type {
            EN_SESSION_FLAG_NONE    = 0,
            EN_SESSION_FLAG_CLOSING = 0x0001,
            EN_SESSION_FLAG_CLOSED  = 0x0002,
        };
    };

    class flag_guard_t {
    public:
        flag_guard_t();
        ~flag_guard_t();

        void setup(session& owner, flag_t::type f);
        void reset();
        inline operator bool() const { return !!owner_ && !!flag_; }

        UTIL_DESIGN_PATTERN_NOCOPYABLE(flag_guard_t)
        UTIL_DESIGN_PATTERN_NOMOVABLE(flag_guard_t)

    private:
        flag_t::type flag_;
        session* owner_;
    };

public:
    session();
    ~session();

    inline void         set_key(const key_t &key) { id_ = key; };
    inline const key_t &get_key() const { return id_; };

    inline void     set_login_task_id(uint64_t id) { login_task_id_ = id; }
    inline uint64_t get_login_task_id() const { return login_task_id_; }

    inline bool check_flag(flag_t::type f) const {
        // 一次只能检查一个flag
        if (f & (f - 1)) {
            return false;
        }
        return !!(flags_ & f); 
    }

    inline void set_flag(flag_t::type f, bool v) {
        // 一次只能设置一个flag
        if (f & (f - 1)) {
            return;
        }

        if (v) {
            flags_ |= f;
        } else {
            flags_ &= ~static_cast<uint32_t>(f);
        }
    }


    /**
     * @brief 监视关联的player
     * @param 关联的player
     */
    void set_player(std::shared_ptr<player_cache> u);

    /**
     * @brief 获取关联的session
     * @return 关联的session
     */
    std::shared_ptr<player_cache> get_player() const;

    // 下行post包
    int32_t send_msg_to_client(hello::CSMsg &msg);
    int32_t send_msg_to_client(hello::CSMsg &msg, uint64_t session_sequence);

    int32_t send_msg_to_client(const void *msg_data, size_t msg_size);

    static int32_t broadcast_msg_to_client(uint64_t bus_id, const hello::CSMsg &msg);

    static int32_t broadcast_msg_to_client(uint64_t bus_id, const void *msg_data, size_t msg_size);

    struct compare_callback {
        bool   operator()(const key_t &l, const key_t &r) const;
        size_t operator()(const key_t &hash_obj) const;
    };

    int32_t send_kickoff(int32_t reason);

private:
    key_t                       id_;
    uint32_t                    flags_;
    std::weak_ptr<player_cache> player_;
    uint64_t                    login_task_id_;
};

#endif