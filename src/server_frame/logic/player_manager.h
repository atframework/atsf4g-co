#ifndef LOGIC_PLAYER_MANAGER_H
#define LOGIC_PLAYER_MANAGER_H

#pragma once

#include <list>

#include <design_pattern/singleton.h>
#include <std/smart_ptr.h>

#include <protocol/pbdesc/svr.table.pb.h>

#include <utility/environment_helper.h>

class player_cache;

class player_manager : public util::design_pattern::singleton<player_manager> {
public:
    typedef std::shared_ptr<player_cache> player_ptr_t;

public:
    /**
     * @brief 移除用户
     * @param user user指针
     * @param force_kickoff 强制移除，不进入离线缓存
     */
    bool remove(player_ptr_t user, bool force_kickoff = false);

    /**
     * @brief 移除用户
     * @param user_id user_id
     * @param zone_id zone_id
     * @param force_kickoff 强制移除，不进入离线缓存
     */
    bool remove(uint64_t user_id, uint32_t zone_id, bool force_kickoff = false);


    /**
     * @brief 保存用户数据
     * @param user_id user_id
     */
    bool save(uint64_t user_id, uint32_t zone_id);

    /**
     * @brief 加载指定玩家数据。
     * @note 注意这个函数只是读数据库做缓存。
     * @note gamesvr 请不要强制拉去数据 会冲掉玩家数据
     * @note 返回的 user 指针不能用于改写玩家数据，不做保存。
     * @param user_id
     * @return null 或者 user指针
     */
    player_ptr_t load(uint64_t user_id, uint32_t zone_id, bool force = false);

    size_t size() const;

    player_ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid, hello::table_login &login_tb, std::string &login_ver);
    template <typename TPLAYER>
    const std::shared_ptr<TPLAYER> create_as(uint64_t user_id, uint32_t zone_id, const std::string &openid, hello::table_login &login_tb,
                                             std::string &login_ver) {
        return std::static_pointer_cast<TPLAYER>(create(user_id, zone_id, openid, login_tb, login_ver));
    }

    player_ptr_t find(uint64_t user_id, uint32_t zone_id) const;

    template <typename TPLAYER>
    const std::shared_ptr<TPLAYER> find_as(uint64_t user_id, uint32_t zone_id) const {
        return std::static_pointer_cast<TPLAYER>(find(user_id, zone_id));
    }
};

#endif