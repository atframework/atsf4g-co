#ifndef DATA_SESSION_H
#define DATA_SESSION_H

#pragma once

#include <std/smart_ptr.h>

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

public:
    session();
    ~session();

    inline void         set_key(const key_t &key) { id_ = key; };
    inline const key_t &get_key() const { return id_; };

    inline void     set_login_task_id(uint64_t id) { login_task_id_ = id; }
    inline uint64_t get_login_task_id() const { return login_task_id_; }

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
    std::weak_ptr<player_cache> player_;
    uint64_t                    login_task_id_;
};

#endif