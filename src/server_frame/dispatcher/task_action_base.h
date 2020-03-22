//
// Created by owt50 on 2016/9/26.
//

#ifndef DISPATCHER_TASK_ACTION_BASE_H
#define DISPATCHER_TASK_ACTION_BASE_H

#pragma once

#include <list>

#include <std/smart_ptr.h>

#include <libcopp/utils/features.h>
#include <libcotask/task.h>

/**
 * action 默认结构
 * action rpc 接口
 * action rpc.1 记录+Dispatcher发送接口（出错则直接返回）
 * action rpc.2 检查回包+填充rsp包+返回调用者
 *
 * action rpc 启动（填充初始包+ operator()(void*) => operator()() ）
 */

class player_cache;

class task_action_base : public ::cotask::impl::task_action_impl {
protected:
    task_action_base();
    virtual ~task_action_base();

public:
    virtual const char *name() const;

    int         operator()(void *priv_data);
    virtual int hook_run();

    virtual int               operator()() = 0;
    inline uint64_t           get_player_id() const { return player_id_; }
    inline unsigned long long get_player_id_llu() const { return static_cast<unsigned long long>(get_player_id()); };

    virtual int on_success();
    virtual int on_failed();
    virtual int on_timeout();
    virtual int on_complete();

    uint64_t           get_task_id() const;
    unsigned long long get_task_id_llu() const;

protected:
    inline void  set_player_id(uint64_t player_id) { player_id_ = player_id; }
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
    uint64_t task_id_;
    int32_t  ret_code_;
    int32_t  rsp_code_;
    bool     rsp_msg_disabled_;
    bool     evt_disabled_;
};

template <typename TREQ>
class task_action_req_base : public task_action_base {
public:
    typedef TREQ msg_type;

protected:
    inline TREQ &      get_request() { return request_msg_; }
    inline const TREQ &get_request() const { return request_msg_; }

private:
    TREQ request_msg_;
};


#endif // ATF4G_CO_TASK_ACTION_BASE_H
