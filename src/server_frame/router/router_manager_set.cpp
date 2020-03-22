//
// Created by owent on 2018/05/01.
//

#include <sstream>
#include <unordered_set>

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_manager.h>


#include "action/task_action_auto_save_objects.h"
#include "action/task_action_router_close_manager_set.h"
#include "action/task_action_router_transfer.h"
#include "action/task_action_router_update.h"

#include "router_manager_base.h"
#include "router_manager_set.h"
#include "router_object_base.h"

// #include "router_guild_manager.h"
// #include "router_player_group_manager.h"
// #include "router_player_manager.h"
// #include "router_team_manager.h"


router_manager_set::router_manager_set() : last_proc_time_(0), is_closing_(false), is_closed_(false) { memset(mgrs_, 0, sizeof(mgrs_)); }

int router_manager_set::init() {
    int ret = 0;

    // 注册路由系统的内部事件
    REG_TASK_MSG_HANDLE(ss_msg_dispatcher, ret, task_action_router_transfer, hello::SSMsgBody::kMssRouterTransferReq);
    REG_TASK_MSG_HANDLE(ss_msg_dispatcher, ret, task_action_router_update, hello::SSMsgBody::kMssRouterUpdateSync);

    is_closing_ = false;
    is_closed_  = false;
    return ret;
}

int router_manager_set::tick() {
    int ret = 0;

    // 如果不是正在关闭，则每秒只需要判定一次
    if (!is_closing() && last_proc_time_ == ::util::time::time_utility::get_now()) {
        return ret;
    }
    // 每分钟打印一次统计数据
    if (last_proc_time_ / util::time::time_utility::MINITE_SECONDS != ::util::time::time_utility::get_now() / util::time::time_utility::MINITE_SECONDS) {
        std::stringstream ss;
        ss << "[STAT] router manager set => now: " << ::util::time::time_utility::get_now() << std::endl;
        ss << "\tdefault timer count: " << timers_.default_timer_list.size() << ", next active timer: ";
        if (timers_.default_timer_list.empty()) {
            ss << 0 << std::endl;
        } else {
            ss << timers_.default_timer_list.front().timeout << std::endl;
        }
        ss << "\tfast timer count: " << timers_.fast_timer_list.size() << ", next active timer: ";
        if (timers_.fast_timer_list.empty()) {
            ss << 0 << std::endl;
        } else {
            ss << timers_.fast_timer_list.front().timeout << std::endl;
        }


        for (int i = 0; i < hello::EnRouterObjectType_ARRAYSIZE; ++i) {
            if (mgrs_[i]) {
                ss << "\t" << mgrs_[i]->name() << " has " << mgrs_[i]->size() << " cache(s)" << std::endl;
            }
        }

        WLOGINFO("%s", ss.str().c_str());
    }
    last_proc_time_ = ::util::time::time_utility::get_now();

    //  正在执行closing任务则不需要自动清理/保存了
    if (false == is_closing_task_running()) {
        time_t cache_expire  = logic_config::me()->get_cfg_logic().router.cache_free_timeout;
        time_t object_expire = logic_config::me()->get_cfg_logic().router.object_free_timeout;
        time_t object_save   = logic_config::me()->get_cfg_logic().router.object_save_interval;
        // 缓存失效定时器
        ret += tick_timer(cache_expire, object_expire, object_save, timers_.default_timer_list, false);
        ret += tick_timer(cache_expire, object_expire, object_save, timers_.fast_timer_list, true);
    }

    if (!save_list_.empty() && !is_closed() && false == is_save_task_running() && false == is_closing_task_running()) {
        task_manager::id_t tid = 0;
        task_manager::me()->create_task_with_timeout<task_action_auto_save_objects>(tid, logic_config::me()->get_cfg_logic().task_nomsg_timeout,
                                                                                    task_action_auto_save_objects::ctor_param_t());
        if (0 == tid) {
            WLOGERROR("create task_action_auto_save_objects failed");
        } else {
            dispatcher_start_data_t start_data;
            start_data.private_data     = NULL;
            start_data.message.msg_addr = NULL;
            start_data.message.msg_type = 0;

            if (0 == task_manager::me()->start_task(tid, start_data)) {
                save_task_ = task_manager::me()->get_task(tid);
            }
        }
    }

    if (is_closing() && !is_closing_task_running()) {
        is_closed_ = true;
    }

    return ret;
}

int router_manager_set::stop() {
    if (is_closing()) {
        return 0;
    }

    is_closing_ = true;

    // prepare to start cleanup task

    task_action_router_close_manager_set::ctor_param_t ctor_param;
    task_manager::id_t                                 tid = 0;

    ctor_param.pending_list = std::make_shared<task_action_router_close_manager_set::pending_list_t>();
    if (ctor_param.pending_list) {
        std::unordered_set<router_object_base::key_t> recheck_set;
        ctor_param.pending_list->reserve(timers_.default_timer_list.size() + timers_.fast_timer_list.size());

        std::list<timer_t> *timer_lists[2] = {&timers_.default_timer_list, &timers_.fast_timer_list};
        for (int i = 0; i < 2; ++i) {
            std::list<timer_t> *cur_list = timer_lists[i];
            for (std::list<timer_t>::iterator iter = cur_list->begin(); iter != cur_list->end(); ++iter) {
                // 已过期，直接清理掉
                if ((*iter).obj_watcher.expired()) {
                    continue;
                }
                std::shared_ptr<router_object_base> obj = (*iter).obj_watcher.lock();

                // 已过期，直接清理掉
                if (!obj) {
                    continue;
                }

                // 不是实体，不需要保存
                if (!obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
                    continue;
                }

                // 已经在其他队列处理过了则跳过
                if (recheck_set.end() != recheck_set.find(obj->get_key())) {
                    continue;
                }
                recheck_set.insert(obj->get_key());

                ctor_param.pending_list->push_back(obj);
            }
        }
    }

    // cleanup router manager now, it will also cleanup timers_
    for (int i = 0; i < hello::EnRouterObjectType_ARRAYSIZE; ++i) {
        if (NULL != mgrs_[i]) {
            mgrs_[i]->on_stop();
        }
    }

    task_manager::me()->create_task_with_timeout<task_action_router_close_manager_set>(tid, logic_config::me()->get_cfg_logic().task_nomsg_timeout,
                                                                                       COPP_MACRO_STD_MOVE(ctor_param));
    if (0 == tid) {
        WLOGERROR("create task_action_router_close_manager_set failed");
    } else {
        dispatcher_start_data_t start_data;
        start_data.private_data     = NULL;
        start_data.message.msg_addr = NULL;
        start_data.message.msg_type = 0;

        closing_task_ = task_manager::me()->get_task(tid);
        if (!closing_task_) {
            WLOGERROR("task_action_router_close_manager_set should not be not found");
            return 0;
        }

        if (is_save_task_running() && save_task_) {
            save_task_->then(closing_task_);
        } else {
            int res = task_manager::me()->start_task(tid, start_data);
            if (res < 0) {
                closing_task_.reset();
                WLOGERROR("start task_action_router_close_manager_set with task_id=%llu failed, res: %d", static_cast<unsigned long long>(tid), res);
            }
        }
    }

    return 0;
}

void router_manager_set::force_close() {
    if (!is_closing() || is_closed()) {
        return;
    }

    // 强制停止清理任务
    if (is_closing_task_running() && closing_task_) {
        closing_task_->kill();
    }

    if (closing_task_) {
        closing_task_.reset();
    }
}

int router_manager_set::tick_timer(time_t cache_expire, time_t object_expire, time_t object_save, std::list<timer_t> &timer_list, bool is_fast) {
    int ret = 0;
    // 缓存失效定时器
    do {
        if (timer_list.empty()) {
            break;
        }

        std::list<timer_t>::iterator timer_iter = timer_list.begin();
        timer_t &                    cache      = *timer_iter;

        // 如果没到时间，后面的全没到时间
        if (last_proc_time_ <= cache.timeout) {
            break;
        }

        // 如果已下线并且缓存失效则跳过
        std::shared_ptr<router_object_base> obj = cache.obj_watcher.lock();
        if (!obj) {
            timer_list.erase(timer_iter);
            continue;
        }

        // 如果操作序列失效则跳过
        if (false == obj->check_timer_sequence(cache.timer_sequence)) {
            obj->check_and_remove_timer_ref(&timer_list, timer_iter);
            timer_list.erase(timer_iter);
            continue;
        }

        // 已销毁则跳过
        router_manager_base *mgr = get_manager(cache.type_id);
        if (NULL == mgr) {
            obj->check_and_remove_timer_ref(&timer_list, timer_iter);
            timer_list.erase(timer_iter);
            continue;
        }

        // 管理器中的对象已被替换或移除则跳过
        if (mgr->get_base_cache(obj->get_key()) != obj) {
            obj->check_and_remove_timer_ref(&timer_list, timer_iter);
            timer_list.erase(timer_iter);
            continue;
        }

        bool is_next_timer_fast = is_fast; // 快队列定时器只能进入快队列
        // 正在执行IO任务则不需要任何流程,因为IO任务结束后可能改变状态
        if (false == obj->is_io_running()) {
            if (obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
                // 实体过期
                if (obj->get_last_visit_time() + object_expire < last_proc_time_) {
                    save_list_.push_back(auto_save_data_t());
                    auto_save_data_t &auto_save = save_list_.back();
                    auto_save.object            = obj;
                    auto_save.type_id           = cache.type_id;
                    auto_save.action            = EN_ASA_REMOVE_OBJECT;

                    obj->set_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
                } else if (obj->get_last_save_time() + object_save < last_proc_time_) { // 实体保存
                    save_list_.push_back(auto_save_data_t());
                    auto_save_data_t &auto_save = save_list_.back();
                    auto_save.object            = obj;
                    auto_save.type_id           = cache.type_id;
                    auto_save.action            = EN_ASA_SAVE;
                    obj->refresh_save_time();
                }
            } else {
                // 缓存过期,和下面主动回收缓存的逻辑保持一致
                if (obj->get_last_visit_time() + cache_expire < last_proc_time_) {
                    save_list_.push_back(auto_save_data_t());
                    auto_save_data_t &auto_save = save_list_.back();
                    auto_save.object            = obj;
                    auto_save.type_id           = cache.type_id;
                    auto_save.action            = EN_ASA_REMOVE_CACHE;

                    obj->set_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

                    // 移除任务可以在快队列里复查
                    is_next_timer_fast = true;
                }
            }
        } else {
            // 如果IO任务正在执行，则下次进入快队列
            is_next_timer_fast = true;
        }

        // 无论什么事件，都需要插入下一个定时器做检查，以防异步流程异常结束
        // 先移除定时器引用，否则insert_timer里会移除一次
        obj->check_and_remove_timer_ref(&timer_list, timer_iter);
        insert_timer(mgr, obj, is_next_timer_fast);
        timer_list.erase(timer_iter);
        ++ret;
    } while (true);

    return ret;
}

bool router_manager_set::insert_timer(router_manager_base *mgr, const std::shared_ptr<router_object_base> &obj, bool is_fast) {
    if (last_proc_time_ <= 0) {
        WLOGERROR("router_manager_set not actived");
    }
    assert(last_proc_time_ > 0);
    if (!obj || !mgr) {
        return false;
    }

    if (is_closing()) {
        return false;
    }

    router_manager_base *checked_mgr = get_manager(mgr->get_type_id());
    if (checked_mgr != mgr) {
        WLOGERROR("router_manager_set has registered %u to %s, but try to setup timer of %s", mgr->get_type_id(),
                  (NULL == checked_mgr ? "None" : checked_mgr->name()), mgr->name());
        return false;
    }

    std::list<timer_t> *tm_timer;
    if (!is_fast) {
        tm_timer = &timers_.default_timer_list;
    } else {
        tm_timer = &timers_.fast_timer_list;
    }

    std::list<timer_t>::iterator tm_iter = tm_timer->insert(tm_timer->end(), timer_t());
    timer_t *                    tm_inst = &(*tm_iter);

    tm_inst->obj_watcher = obj;
    tm_inst->type_id     = mgr->get_type_id();
    if (!is_fast) {
        tm_inst->timeout = util::time::time_utility::get_now() + logic_config::me()->get_cfg_logic().router.default_timer_interval;
    } else {
        tm_inst->timeout = util::time::time_utility::get_now() + logic_config::me()->get_cfg_logic().router.fast_timer_interval;
    }
    tm_inst->timer_sequence = obj->alloc_timer_sequence();
    obj->reset_timer_ref(tm_timer, tm_iter);

    return true;
}

router_manager_base *router_manager_set::get_manager(uint32_t type) {
    if (type >= hello::EnRouterObjectType_ARRAYSIZE) {
        return nullptr;
    }

    return mgrs_[type];
}

int router_manager_set::register_manager(router_manager_base *b) {
    if (NULL == b) {
        return hello::err::EN_SYS_PARAM;
    }

    uint32_t type = b->get_type_id();
    if (type >= hello::EnRouterObjectType_ARRAYSIZE) {
        WLOGERROR("router %s has invalid type id %u", b->name(), type);
        return hello::err::EN_ROUTER_TYPE_INVALID;
    }

    if (mgrs_[type]) {
        WLOGERROR("router %s has type conflicy with %s", mgrs_[type]->name(), b->name());
        return hello::err::EN_ROUTER_TYPE_CONFLICT;
    }

    mgrs_[type] = b;

    // copy some flags
    if (is_closing()) {
        b->on_stop();
    }
    return hello::err::EN_SUCCESS;
}

int router_manager_set::unregister_manager(router_manager_base *b) {
    if (NULL == b) {
        return hello::err::EN_SYS_PARAM;
    }

    uint32_t type = b->get_type_id();
    if (type >= hello::EnRouterObjectType_ARRAYSIZE) {
        WLOGERROR("router %s has invalid type id %u", b->name(), type);
        return hello::err::EN_ROUTER_TYPE_INVALID;
    }

    if (mgrs_[type] == b) {
        mgrs_[type] = NULL;
    }

    return hello::err::EN_SUCCESS;
}

size_t router_manager_set::size() const {
    size_t ret = 0;
    for (int i = 0; i < hello::EnRouterObjectType_ARRAYSIZE; ++i) {
        if (NULL != mgrs_[i]) {
            ret += mgrs_[i]->size();
        }
    }

    return ret;
}

int router_manager_set::recycle_caches(int max_count) {
    if (max_count <= 0) {
        return 0;
    }

    int ret = 0;

    std::list<timer_t>::iterator default_timer_iter = timers_.default_timer_list.begin();
    std::list<timer_t>::iterator fast_timer_iter    = timers_.fast_timer_list.begin();

    typedef std::list<std::pair<std::shared_ptr<router_object_base>, router_manager_base *> > recheck_list_t;

    recheck_list_t                                recheck_list;
    std::unordered_set<router_object_base::key_t> recheck_set;

    while (ret < max_count) {
        if (default_timer_iter == timers_.default_timer_list.end() && fast_timer_iter == timers_.fast_timer_list.end()) {
            break;
        }

        std::list<timer_t> *         selected_list;
        std::list<timer_t>::iterator selected_iter;
        if (default_timer_iter == timers_.default_timer_list.end()) {
            selected_list = &timers_.fast_timer_list;
            selected_iter = fast_timer_iter;
            ++fast_timer_iter;
        } else if (fast_timer_iter == timers_.fast_timer_list.end()) {
            selected_list = &timers_.default_timer_list;
            selected_iter = default_timer_iter;
            ++default_timer_iter;
        } else if ((*default_timer_iter).timeout <= (*fast_timer_iter).timeout) {
            selected_list = &timers_.default_timer_list;
            selected_iter = default_timer_iter;
            ++default_timer_iter;
        } else {
            selected_list = &timers_.fast_timer_list;
            selected_iter = fast_timer_iter;
            ++fast_timer_iter;
        }

        // 已过期，直接清理掉
        if ((*selected_iter).obj_watcher.expired()) {
            selected_list->erase(selected_iter);
            continue;
        }
        std::shared_ptr<router_object_base> obj = (*selected_iter).obj_watcher.lock();

        // 已过期，直接清理掉
        if (!obj) {
            selected_list->erase(selected_iter);
            continue;
        }

        // 如果操作序列失效则跳过
        if (false == obj->check_timer_sequence((*selected_iter).timer_sequence)) {
            selected_list->erase(selected_iter);
            continue;
        }

        // 不是缓存，不能清理
        if (obj->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
            continue;
        }

        // manager 错误
        router_manager_base *mgr = get_manager((*selected_iter).type_id);
        if (NULL == mgr) {
            WLOGERROR("invalid manager %u", (*selected_iter).type_id);
            selected_list->erase(selected_iter);
            continue;
        }

        // 管理器中的对象已被替换或移除则跳过
        if (mgr->get_base_cache(obj->get_key()) != obj) {
            selected_list->erase(selected_iter);
            continue;
        }

        // 已经在其他队列处理过了则跳过
        if (recheck_set.end() != recheck_set.find(obj->get_key())) {
            selected_list->erase(selected_iter);
            continue;
        }
        recheck_set.insert(obj->get_key());

        // 缓存过期,和上面定时回收缓存的逻辑保持一致
        save_list_.push_back(auto_save_data_t());
        auto_save_data_t &auto_save = save_list_.back();
        auto_save.object            = obj;
        auto_save.type_id           = (*selected_iter).type_id;
        auto_save.action            = EN_ASA_REMOVE_CACHE;

        obj->set_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

        // 无论什么事件，都需要插入下一个定时器做检查，以防异步流程异常结束
        recheck_list.push_back(std::make_pair(obj, mgr));
        selected_list->erase(selected_iter);
        ++ret;
    }

    // 重新加入到快队列，因为实际执行前可能被mutable而导致缓存对象被重新激活
    for (recheck_list_t::iterator iter = recheck_list.begin(); iter != recheck_list.end(); ++iter) {
        insert_timer(iter->second, iter->first, true);
    }

    return ret;
}

bool router_manager_set::is_save_task_running() const { return save_task_ && !save_task_->is_exiting(); }

bool router_manager_set::is_closing_task_running() const { return closing_task_ && !closing_task_->is_exiting(); }
