//
// Created by owent on 2018/05/01.
//

#include <config/logic_config.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>


#include <protocol/pbdesc/svr.const.err.pb.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>

#include "router_object_base.h"

bool router_object_base::key_t::operator==(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    return object_id == r.object_id && zone_id == r.zone_id && type_id == r.type_id;
}

bool router_object_base::key_t::operator!=(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    return object_id != r.object_id || zone_id != r.zone_id || type_id != r.type_id;
}

bool router_object_base::key_t::operator<(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    if (type_id != r.type_id) {
        return type_id < r.type_id;
    }

    if (zone_id != r.zone_id) {
        return zone_id < r.zone_id;
    }

    return object_id < r.object_id;
}

bool router_object_base::key_t::operator<=(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    if (type_id != r.type_id) {
        return type_id < r.type_id;
    }

    if (zone_id != r.zone_id) {
        return zone_id < r.zone_id;
    }

    return object_id <= r.object_id;
}

bool router_object_base::key_t::operator>(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    if (type_id != r.type_id) {
        return type_id > r.type_id;
    }

    if (zone_id != r.zone_id) {
        return zone_id > r.zone_id;
    }

    return object_id > r.object_id;
}

bool router_object_base::key_t::operator>=(const key_t &r) const UTIL_CONFIG_NOEXCEPT {
    if (type_id != r.type_id) {
        return type_id > r.type_id;
    }

    if (zone_id != r.zone_id) {
        return zone_id > r.zone_id;
    }

    return object_id >= r.object_id;
}

router_object_base::flag_guard::flag_guard(router_object_base &owner, int f) : owner_(&owner), f_(f) {
    if (f_ & owner_->get_flags()) {
        f_ = 0;
    } else if (0 != f_) {
        owner_->set_flag(f_);
    }
}

router_object_base::flag_guard::~flag_guard() {
    if (NULL != owner_ && 0 != f_) {
        owner_->unset_flag(f_);
    }
}

router_object_base::router_object_base(const key_t &k)
    : key_(k), last_save_time_(0), last_visit_time_(0), router_svr_id_(0), router_svr_ver_(0), timer_sequence_(0), timer_list_(NULL), saving_sequence_(0),
      saved_sequence_(0), flags_(0) {

    // 创建时初始化最后访问时间
    refresh_visit_time();
}

router_object_base::router_object_base(key_t &&k)
    : key_(k), last_save_time_(0), last_visit_time_(0), router_svr_id_(0), router_svr_ver_(0), timer_sequence_(0), timer_list_(NULL), saving_sequence_(0),
      saved_sequence_(0), flags_(0) {

    // 创建时初始化最后访问时间
    refresh_visit_time();
}

router_object_base::~router_object_base() {
    // 清理掉timer
    unset_timer_ref();
}

void router_object_base::refresh_visit_time() {
    last_visit_time_ = util::time::time_utility::get_now();

    // 刷新访问事件要取消移除缓存的计划任务
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
}

void router_object_base::refresh_save_time() { last_save_time_ = util::time::time_utility::get_now(); }

int router_object_base::remove_object(void *priv_data, uint64_t transfer_to_svr_id) {
    if (!is_writable()) {
        return hello::EN_SUCCESS;
    }

    // 移除实体需要设置路由BUS ID为0并保存一次
    uint64_t old_router_server_id = get_router_server_id();
    uint64_t old_router_ver       = get_router_version();

    if (transfer_to_svr_id != get_router_server_id()) {
        set_router_server_id(transfer_to_svr_id, old_router_ver + 1);
    }
    refresh_visit_time();

    int ret = save(priv_data);
    if (ret < 0) {
        WLOGERROR("remove router object %u:%u:0x%llx, res:%d", get_key().type_id, get_key().zone_id, get_key().object_id_ull(), ret);

        // 保存失败则恢复原来的路由信息
        set_router_server_id(old_router_server_id, old_router_ver);
        return ret;
    }

    downgrade();
    return ret;
}

bool router_object_base::is_cache_available() const {
    if (is_pulling_cache()) {
        return false;
    }

    if (is_writable()) {
        return true;
    }

    if (last_save_time_ + logic_config::me()->get_cfg_logic().router.cache_update_interval < util::time::time_utility::get_now()) {
        return false;
    }

    return true;
}

bool router_object_base::is_object_available() const {
    if (is_pulling_object()) {
        return false;
    }

    return is_writable();
}

int router_object_base::pull_cache(void *priv_data) { return pull_object(priv_data); }

int router_object_base::upgrade() {
    if (check_flag(flag_t::EN_ROFT_IS_OBJECT)) {
        return 0;
    }

    refresh_visit_time();
    set_flag(flag_t::EN_ROFT_IS_OBJECT);
    unset_flag(flag_t::EN_ROFT_CACHE_REMOVED);

    // 升级操作要取消移除缓存和降级的计划任务
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
    return 0;
}

int router_object_base::downgrade() {
    if (!check_flag(flag_t::EN_ROFT_IS_OBJECT)) {
        return 0;
    }

    refresh_visit_time();
    unset_flag(flag_t::EN_ROFT_IS_OBJECT);
    return 0;
}

int router_object_base::send_transfer_msg_failed(
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    hello::SSMsg &&req
#else
    hello::SSMsg &req
#endif
) {
    task_action_ss_req_base::msg_type rsp_msg;
    uint64_t                          dst_pd = req.head().bus_id();
    if (req.head().has_router()) {
        dst_pd = req.head().router().router_src_bus_id();
    }

    task_action_ss_req_base::init_msg(rsp_msg, dst_pd, req);

    // 如果没有task_id则要不复制路由信息，防止触发路由转发
    if (0 == rsp_msg.head().dst_task_id()) {
        rsp_msg.mutable_head()->clear_router();
    }

    // 转移失败错误码
    rsp_msg.mutable_head()->set_error_code(hello::err::EN_ROUTER_TRANSFER);

    return ss_msg_dispatcher::me()->send_to_proc(dst_pd, rsp_msg);
}

int router_object_base::await_io_task() {
    if (!io_task_) {
        return 0;
    }

    if (io_task_->is_exiting()) {
        io_task_.reset();
        return 0;
    }

    task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
    if (!self_task) {
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    return await_io_task(self_task);
}

int router_object_base::await_io_task(task_manager::task_ptr_t &self_task) {
    if (!self_task) {
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    int ret = 0;
    while (true) {
        if (self_task->is_timeout()) {
            ret = hello::err::EN_SYS_TIMEOUT;
            break;
        } else if (self_task->is_faulted()) {
            ret = hello::err::EN_SYS_RPC_TASK_KILLED;
            break;
        } else if (self_task->is_canceled()) {
            ret = hello::err::EN_SYS_RPC_TASK_CANCELLED;
            break;
        } else if (self_task->is_exiting()) {
            ret = hello::err::EN_SYS_RPC_TASK_EXITING;
            break;
        }

        if (!io_task_ || io_task_ == self_task) {
            break;
        }

        if (io_task_->is_exiting()) {
            io_task_.reset();
            continue;
        }

        io_task_->next(self_task);
        self_task->yield(NULL);
    }

    // 超出重试次数限制
    return ret;
}

int router_object_base::pull_cache_inner(void *priv_data) {
    // 触发拉取缓存时要取消移除缓存的计划任务
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

    task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
    if (!self_task) {
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    int ret = await_io_task(self_task);
    if (ret < 0) {
        return ret;
    }

    // 先等待之前的任务完成再设置flag
    flag_guard fg(*this, flag_t::EN_ROFT_PULLING_CACHE);

    // 执行读任务
    io_task_.swap(self_task);
    ret = pull_cache(priv_data);
    io_task_.reset();

    if (ret < 0) {
        return ret;
    }

    // 拉取成功要refresh_save_time
    refresh_save_time();

    return ret;
}

int router_object_base::pull_object_inner(void *priv_data) {
    // 触发拉取实体时要取消移除缓存和降级的计划任务
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
    unset_flag(flag_t::EN_ROFT_SCHED_REMOVE_CACHE);

    task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
    if (!self_task) {
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    int ret = await_io_task(self_task);
    if (ret < 0) {
        return ret;
    }

    // 先等待之前的任务完成再设置flag
    flag_guard fg(*this, flag_t::EN_ROFT_PULLING_OBJECT);

    unset_flag(flag_t::EN_ROFT_CACHE_REMOVED);
    unset_flag(flag_t::EN_ROFT_FORCE_PULL_OBJECT);

    // 执行读任务
    io_task_.swap(self_task);
    ret = pull_object(priv_data);
    io_task_.reset();

    if (ret < 0) {
        return ret;
    }

    // 拉取成功要refresh_save_time
    refresh_save_time();

    if (0 != get_router_server_id()) {
        if (logic_config::me()->get_self_bus_id() != get_router_server_id()) {
            // 可能某处的缓存过老，这是正常流程，返回错误码即可，不用打错误日志
            return hello::err::EN_ROUTER_NOT_WRITABLE;
        }
    }

    // 升级为实体
    upgrade();
    return ret;
}

int router_object_base::save_object_inner(void *priv_data) {

    // 排队写任务和并发写merge
    uint64_t this_saving_seq = ++saving_sequence_;

    task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
    if (!self_task) {
        return hello::err::EN_SYS_RPC_NO_TASK;
    }

    // 如果有其他任务正在保存，需要等待那个任务结束
    int ret = await_io_task(self_task);
    if (ret < 0) {
        return ret;
    }

    if (!is_writable()) {
        return hello::err::EN_ROUTER_NOT_WRITABLE;
    }

    // 因为可能叠加和被其他任务抢占，所以这里需要核查一遍保存序号
    // 如果其他任务的保存涵盖了自己的数据变化，则直接成功返回
    if (saved_sequence_ >= this_saving_seq) {
        return 0;
    }

    // 先等待之前的任务完成再设置flag
    flag_guard fg(*this, flag_t::EN_ROFT_SAVING);

    uint64_t real_saving_seq = saving_sequence_;
    io_task_.swap(self_task);
    ret = save_object(priv_data);
    io_task_.reset();

    if (ret >= 0 && real_saving_seq > saved_sequence_) {
        saved_sequence_ = real_saving_seq;
    } else if (ret < 0) {
        // 保存失败
        return ret;
    }

    refresh_save_time();
    return ret;
}

void router_object_base::reset_timer_ref(std::list<router_system_timer_t> *timer_list, const std::list<router_system_timer_t>::iterator &it) {
    if (timer_list_ == timer_list && timer_iter_ == it) {
        return;
    }

    unset_timer_ref();

    timer_list_ = timer_list;
    timer_iter_ = it;
}

void router_object_base::check_and_remove_timer_ref(std::list<router_system_timer_t> *timer_list, const std::list<router_system_timer_t>::iterator &it) {
    if (timer_list_ != timer_list || timer_iter_ != it) {
        return;
    }

    if (timer_list_ != NULL && timer_iter_ != timer_list_->end()) {
        timer_iter_ = timer_list_->end();
    }
    timer_list_ = NULL;
}

void router_object_base::unset_timer_ref() {
    // 清理掉timer
    if (timer_list_ != NULL && timer_iter_ != timer_list_->end()) {
        timer_list_->erase(timer_iter_);
        timer_iter_ = timer_list_->end();
    }
    timer_list_ = NULL;
}
