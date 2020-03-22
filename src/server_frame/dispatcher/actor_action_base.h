//
// Created by owt50 on 2016/11/14.
//

#ifndef DISPATCHER_ACTOR_ACTION_BASE_H
#define DISPATCHER_ACTOR_ACTION_BASE_H

#pragma once

#include <list>

#include <std/smart_ptr.h>

#include <libcopp/utils/features.h>

class actor_action_base {
public:
    enum status_t { EN_AAS_CREATED = 0, EN_AAS_RUNNING, EN_AAS_FINISHED };

protected:
    actor_action_base();
    virtual ~actor_action_base();

public:
    virtual const char *name() const;

    virtual int hook_run();
    virtual int operator()() = 0;
    inline uint64_t get_player_id() const { return player_id_; }
    inline unsigned long long get_player_id_llu() const { return static_cast<unsigned long long>(get_player_id()); };

    virtual int on_success();
    virtual int on_failed();
    virtual int on_complete();

    int32_t run();

protected:
    inline void set_player_id(uint64_t player_id) { player_id_ = player_id; }
    virtual void send_rsp_msg() = 0;

public:
    /**
     * @brief 获取逻辑返回码
     * @note 默认值为 T_APP_SUCCESS
     * @see T_APP_SUCCESS
     * @return 返回码
     */
    inline int32_t get_ret_code() const { return ret_code_; }

    /**
     * @brief 获取回包返回码
     * @note 默认值为 Polar::EN_CS_SUCCESS
     * @see Polar::EN_CS_SUCCESS
     * @return 回包返回码
     */
    inline int32_t get_rsp_code() const { return rsp_code_; }

    /**
     * @brief 返回当前状态
     * @return 当前状态
     */
    inline status_t get_status() const { return status_; }

protected:
    /**
     * @brief 设置逻辑返回码
     * @note 用于临时存储逻辑操作错误码
     * @param iRetCode 返回码
     */
    inline void set_ret_code(int32_t ret_code) { ret_code_ = ret_code; }


    /**
     * @brief 设置回包返回码
     * @note 用于临时存储回包返回码
     * @param iRetCode 回包返回码
     */
    inline void set_rsp_code(int32_t rsp_code) { rsp_code_ = rsp_code; }

    /**
     * @brief 禁用结束事件响应
     */
    inline void disable_finish_evt() { evt_disabled_ = false; }

    /**
     * @brief 禁用自动回包
     */
    inline void disable_rsp_msg() { rsp_msg_disabled_ = false; }

private:
    uint64_t player_id_;
    int32_t ret_code_;
    int32_t rsp_code_;
    status_t status_;
    bool rsp_msg_disabled_;
    bool evt_disabled_;
};

template <typename TREQ>
class actor_action_req_base : public actor_action_base {
public:
    typedef TREQ msg_type;

protected:
    inline TREQ &get_request() { return request_msg_; }
    inline const TREQ &get_request() const { return request_msg_; }

private:
    TREQ request_msg_;
};


#endif //_DISPATCHER_ACTOR_ACTION_BASE_H
