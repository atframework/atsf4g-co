//
// Created by owt50 on 2016/9/26.
//

#ifndef DISPATCHER_TASK_MANAGER_H
#define DISPATCHER_TASK_MANAGER_H

#pragma once

#include <design_pattern/singleton.h>
#include <libcopp/stack/stack_allocator.h>
#include <libcopp/stack/stack_pool.h>
#include <libcotask/task.h>
#include <libcotask/task_manager.h>


#include <utility/environment_helper.h>

#include "dispatcher_type_defines.h"

#include <protocol/pbdesc/com.const.pb.h>

class actor_action_base;

/**
 * @brief 协程任务和简单actor的管理创建manager类
 * @note 涉及异步处理的任务全部走协程任务，不涉及异步调用的模块可以直接使用actor。
 *       actor会比task少一次栈初始化开销（大约8us的CPU+栈所占用的内存）,在量大但是无异步调用的模块（比如地图同步行为）可以节省CPU和内存
 */
class task_manager : public ::util::design_pattern::singleton<task_manager> {
public:
    typedef dispatcher_msg_raw_t     msg_raw_t;
    typedef dispatcher_resume_data_t resume_data_t;
    typedef dispatcher_start_data_t  start_data_t;

    typedef copp::stack_pool<copp::allocator::default_statck_allocator> stack_pool_t;

    struct task_macro_coroutine {
        typedef copp::allocator::stack_allocator_pool<stack_pool_t>  stack_allocator_t;
        typedef copp::coroutine_context_container<stack_allocator_t> coroutine_t;
    };

    typedef cotask::task<task_macro_coroutine> task_t;
    typedef typename task_t::id_t              id_t;

    /// 协程任务创建器
    typedef std::function<int(task_manager::id_t &, start_data_t ctor_param)> task_action_creator_t;

    typedef std::shared_ptr<actor_action_base>                         actor_action_ptr_t;
    typedef std::function<actor_action_ptr_t(start_data_t ctor_param)> actor_action_creator_t;

    template <typename TAction>
    struct task_action_maker_t {
        int operator()(task_manager::id_t &task_id, start_data_t ctor_param) {
            return task_manager::me()->create_task<TAction>(task_id, COPP_MACRO_STD_MOVE(ctor_param));
        };
    };

    template <typename TAction>
    struct actor_action_maker_t {
        actor_action_ptr_t operator()(start_data_t ctor_param) { return task_manager::me()->create_actor<TAction>(COPP_MACRO_STD_MOVE(ctor_param)); };
    };

private:
    typedef UTIL_ENV_AUTO_MAP(id_t, cotask::detail::task_manager_node<task_t>) native_task_container_t;
    typedef cotask::task_manager<task_t, native_task_container_t> mgr_t;
    typedef typename mgr_t::ptr_t                                 mgr_ptr_t;

public:
    typedef typename mgr_t::task_ptr_t task_ptr_t;

protected:
    task_manager();
    ~task_manager();

public:
    int init();

    int reload();

    /**
     * 获取栈大小
     */
    size_t get_stack_size() const;

    /**
     * @brief 创建任务
     * @param task_id 协程任务的ID
     * @param args 传入构造函数的参数
     * @return 0或错误码
     */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    template <typename TAction, typename TParams>
    int create_task(id_t &task_id, TParams &&args) {
#else
    template <typename TAction, typename TParams>
    int create_task(id_t &task_id, const TParams &args) {
#endif
        return create_task_with_timeout<TAction>(task_id, 0, COPP_MACRO_STD_FORWARD(TParams, args));
    }

    /**
     * @brief 创建任务并指定超时时间
     * @param task_id 协程任务的ID
     * @param timeout 超时时间
     * @param args 传入构造函数的参数
     * @return 0或错误码
     */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    template <typename TAction, typename TParams>
    int create_task_with_timeout(id_t &task_id, time_t timeout, TParams &&args) {
#else
    template <typename TAction, typename TParams>
    int create_task_with_timeout(id_t &task_id, time_t timeout, const TParams &args) {
#endif

        if (!stack_pool_ || !native_mgr_) {
            task_id = 0;
            return hello::EN_ERR_SYSTEM;
        }

        task_macro_coroutine::stack_allocator_t alloc(stack_pool_);

        task_t::ptr_t res = task_t::create_with_delegate<TAction>(COPP_MACRO_STD_FORWARD(TParams, args), alloc, get_stack_size(), 0);
        if (!res) {
            task_id = 0;
            return report_create_error(__FUNCTION__);
        }

        task_id = res->get_id();
        return add_task(res, timeout);
    }

    /**
     * @brief 创建协程任务构造器
     * @return 任务构造器
     */
    template <typename TAction>
    task_action_creator_t make_task_creator() {
        return task_action_maker_t<TAction>();
    }

    /**
     * @brief 开始任务
     * @param task_id 协程任务的ID
     * @param data 启动数据，operator()(void* priv_data)的priv_data指向这个对象的地址
     * @return 0或错误码
     */
    int start_task(id_t task_id, start_data_t &data);

    /**
     * @brief 恢复任务
     * @param task_id 协程任务的ID
     * @param data 恢复时透传的数据，yield返回的指针指向这个对象的地址
     * @return 0或错误码
     */
    int resume_task(id_t task_id, resume_data_t &data);

    /**
     * @brief 创建Actor
     * @note 所有的actor必须使用组合的方式执行，不允许使用协程RPC操作
     * @param args 传入构造函数的参数
     * @return 0或错误码
     */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    template <typename TActor, typename... TParams>
    std::shared_ptr<TActor> create_actor(TParams &&... args) {
#else
    template <typename TActor, typename... TParams>
    std::shared_ptr<TActor> create_actor(const TParams &... args) {
#endif
        return std::make_shared<TActor>(COPP_MACRO_STD_FORWARD(TParams, args)...);
    }

    /**
     * @brief 创建Actor构造器
     * @return Actor构造器
     */
    template <typename TAction>
    actor_action_creator_t make_actor_creator() {
        return actor_action_maker_t<TAction>();
    }

    /**
     * @brief tick，可能会触发任务过期
     */
    int tick(time_t sec, int nsec);

    /**
     * @brief tick，可能会触发任务过期
     * @param task_id 任务id
     * @return 如果存在，返回协程任务的智能指针
     */
    task_ptr_t get_task(id_t task_id);

    inline const stack_pool_t::ptr_t &get_stack_pool() const { return stack_pool_; }
    inline const mgr_ptr_t &          get_native_manager() const { return native_mgr_; }

    bool is_busy() const;
private:
    bool check_sys_config() const;
    
    /**
     * @brief 创建任务
     * @param task 协程任务
     * @param timeout 超时时间
     * @return 0或错误码
     */
    int add_task(const task_t::ptr_t &task, time_t timeout = 0);

    int report_create_error(const char *fn_name);

private:
    time_t              stat_interval_;
    time_t              stat_last_checkpoint_;
    size_t              conf_busy_count_;
    size_t              conf_busy_warn_count_;
    mgr_ptr_t           native_mgr_;
    stack_pool_t::ptr_t stack_pool_;
};


#endif // ATF4G_CO_TASK_MANAGER_H
