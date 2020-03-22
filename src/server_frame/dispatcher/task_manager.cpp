//
// Created by owt50 on 2016/9/26.
//

#include <log/log_wrapper.h>
#include <common/file_system.h>
#include <common/string_oprs.h>

#include <google/protobuf/stubs/logging.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/logic_config.h>

#include "task_manager.h"

static void log_wrapper_for_protobuf(::google::protobuf::LogLevel level, const char *filename, int line, const std::string &message) {
    util::log::log_wrapper::caller_info_t caller;
    caller.file_path    = filename;
    caller.line_number  = static_cast<uint32_t>(line);
    caller.func_name    = "protobuf";
    caller.rotate_index = 0;

    switch (level) {
    case ::google::protobuf::LOGLEVEL_INFO:
        caller.level_id   = util::log::log_wrapper::level_t::LOG_LW_INFO;
        caller.level_name = "Info";
        break;

    case ::google::protobuf::LOGLEVEL_WARNING:
        caller.level_id   = util::log::log_wrapper::level_t::LOG_LW_WARNING;
        caller.level_name = "Warn";
        break;

    case ::google::protobuf::LOGLEVEL_ERROR:
        caller.level_id   = util::log::log_wrapper::level_t::LOG_LW_ERROR;
        caller.level_name = "Error";
        break;

    case ::google::protobuf::LOGLEVEL_FATAL:
        caller.level_id   = util::log::log_wrapper::level_t::LOG_LW_FATAL;
        caller.level_name = "Fatal";
        break;

    default:
        caller.level_id   = util::log::log_wrapper::level_t::LOG_LW_DEBUG;
        caller.level_name = "Debug";
        break;
    }

    if (util::log::log_wrapper::check_level(WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT), caller.level_id)) {
        WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->log(caller, "%s", message.c_str());
    }
}

task_manager::task_manager(): stat_interval_(60), stat_last_checkpoint_(0), conf_busy_count_(0), conf_busy_warn_count_(0) {}

task_manager::~task_manager() {
    // free protobuf meta
    ::google::protobuf::ShutdownProtobufLibrary();
}

int task_manager::init() {
    native_mgr_ = mgr_t::create();
    stack_pool_ = stack_pool_t::create();

    // setup logger for protobuf
    ::google::protobuf::SetLogHandler(log_wrapper_for_protobuf);

    // reload is called before init when atapp started
    reload();

    if (!check_sys_config()) {
        return hello::err::EN_SYS_INIT;
    }

    return 0;
}

int task_manager::reload() {
    stat_interval_ = logic_config::me()->get_cfg_logic().task_stats_interval;
    if (stat_interval_ <= 0) {
        stat_interval_ = 60;
    }
    if (stack_pool_) {
        stack_pool_->set_gc_once_number(logic_config::me()->get_cfg_logic().task_stack_gc_once_number);
        stack_pool_->set_max_stack_number(logic_config::me()->get_cfg_logic().task_stack_pool_max_count);
        if (logic_config::me()->get_cfg_logic().task_stack_size > 0) {
            stack_pool_->set_stack_size(logic_config::me()->get_cfg_logic().task_stack_size);
        }
    }
    conf_busy_count_ = logic_config::me()->get_cfg_logic().task_stack_busy_count;
    conf_busy_warn_count_ = logic_config::me()->get_cfg_logic().task_stack_busy_warn_count;

    return 0;
}

int task_manager::start_task(id_t task_id, start_data_t &data) {
    int res = native_mgr_->start(task_id, &data);
    if (res < 0) {
        WLOGERROR("start task 0x%llx failed.", static_cast<unsigned long long>(task_id));

        // 错误码
        return hello::err::EN_SYS_NOTFOUND;
    }

    return 0;
}

int task_manager::resume_task(id_t task_id, resume_data_t &data) {
    int res = native_mgr_->resume(task_id, &data);
    if (res < 0) {
        WLOGERROR("resume task 0x%llx failed.", static_cast<unsigned long long>(task_id));

        // 错误码
        return hello::err::EN_SYS_NOTFOUND;
    }

    return 0;
}

int task_manager::tick(time_t sec, int nsec) {
    if (native_mgr_) {
        native_mgr_->tick(sec, nsec);
    }

    if (stack_pool_) {
        stack_pool_->gc();
    }

    if (stat_last_checkpoint_ != sec / stat_interval_) {
        stat_last_checkpoint_ = sec / stat_interval_;
        if (stack_pool_ && native_mgr_) {
            size_t first_checkpoint = 0;
            if (!native_mgr_->get_checkpoints().empty()) {
                first_checkpoint = native_mgr_->get_checkpoints().begin()->expired_time.tv_sec;
            }
            WLOGINFO(
                "[STATS] Coroutine stack stats:\n\tRuntime - Task Number: %llu\n\tRuntime - Checkpoint Number: %llu\n\tRuntime - Next Checkpoint: %llu\n\tConfigure - Max GC Number: %llu\n\tConfigure - Stack Max: number %llu, size %llu\n\tConfigure - Stack Min: number %llu, size %llu\n\tRuntime - Stack Used: number %llu, size %llu\n\tRuntime - Stack Free: number %llu, size %llu",
                static_cast<unsigned long long>(native_mgr_->get_task_size()),
                static_cast<unsigned long long>(native_mgr_->get_tick_checkpoint_size()),
                static_cast<unsigned long long>(first_checkpoint),
                static_cast<unsigned long long>(stack_pool_->get_gc_once_number()),
                static_cast<unsigned long long>(stack_pool_->get_max_stack_number()),
                static_cast<unsigned long long>(stack_pool_->get_max_stack_size()),
                static_cast<unsigned long long>(stack_pool_->get_min_stack_number()),
                static_cast<unsigned long long>(stack_pool_->get_min_stack_size()),
                static_cast<unsigned long long>(stack_pool_->get_limit().used_stack_number),
                static_cast<unsigned long long>(stack_pool_->get_limit().used_stack_size),
                static_cast<unsigned long long>(stack_pool_->get_limit().free_stack_number),
                static_cast<unsigned long long>(stack_pool_->get_limit().free_stack_size)
            );
        }
    }
    return 0;
}

task_manager::task_ptr_t task_manager::get_task(id_t task_id) {
    if (!native_mgr_) {
        return task_manager::task_ptr_t();
    }

    if (stack_pool_) {
        stack_pool_->gc();
    }

    return native_mgr_->find_task(task_id);
}

size_t task_manager::get_stack_size() const { return logic_config::me()->get_cfg_logic().task_stack_size; }

int task_manager::add_task(const task_t::ptr_t &task, time_t timeout) {
    if (!native_mgr_) {
        return ::hello::err::EN_SYS_INIT;
    }

    int res = 0;
    if (0 == timeout) {
        // read default timeout from configure
        res = native_mgr_->add_task(task, logic_config::me()->get_cfg_logic().task_csmsg_timeout, 0);
    } else {
        res = native_mgr_->add_task(task, timeout, 0);
    }

    if (res < 0) {
        WLOGERROR("add task failed, res: %d", res);
        return hello::err::EN_SYS_PARAM;
    }

    if (conf_busy_warn_count_ > 0 && native_mgr_->get_task_size() > conf_busy_warn_count_) {
        WLOGWARNING("task number %llu extend %llu", static_cast<unsigned long long>(native_mgr_->get_task_size()), static_cast<unsigned long long>(conf_busy_warn_count_));
    }

    return hello::err::EN_SUCCESS;
}

int task_manager::report_create_error(const char *fn_name) {
    WLOGERROR("[%s] create task failed. current task number=%u", fn_name, static_cast<uint32_t>(native_mgr_->get_task_size()));
    return hello::err::EN_SYS_MALLOC;
}

bool task_manager::is_busy() const {
    return conf_busy_count_ > 0 && native_mgr_->get_task_size() > conf_busy_count_;
}

bool task_manager::check_sys_config() const {
    const char* vm_map_count_file = "/proc/sys/vm/max_map_count";

    if (util::file_system::is_exist(vm_map_count_file)) {
        std::string content;
        util::file_system::get_file_content(content, vm_map_count_file);
        uint64_t sys_mmap_count = util::string::to_int<uint64_t>(content.c_str());
        if (logic_config::me()->get_cfg_logic().task_stack_mmap_count > sys_mmap_count) {
            WLOGERROR("task_stack_mmap_count %llu is greater than /proc/sys/vm/max_map_count %llu", 
                static_cast<unsigned long long>(logic_config::me()->get_cfg_logic().task_stack_mmap_count),
                static_cast<unsigned long long>(sys_mmap_count)
            );

            return false;
        }

        // 每个协程栈有一个栈的段和一个protect段，占两个
        uint64_t task_max_num = static_cast<uint64_t>(logic_config::me()->get_cfg_logic().task_stack_busy_count);
        if (task_max_num < logic_config::me()->get_cfg_logic().task_stack_pool_max_count) {
            task_max_num = static_cast<uint64_t>(logic_config::me()->get_cfg_logic().task_stack_pool_max_count);
        }
        uint64_t check_mmap = 2 * task_max_num + logic_config::me()->get_cfg_logic().task_stack_keep_count;
        if (check_mmap > logic_config::me()->get_cfg_logic().task_stack_mmap_count) {
            WLOGERROR("2 * max(task_stack_busy_count, task_stack_pool_max_count) + task_stack_keep_count %llu is greater than task_stack_mmap_count %llu", 
                static_cast<unsigned long long>(check_mmap),
                static_cast<unsigned long long>(logic_config::me()->get_cfg_logic().task_stack_mmap_count)
            );

            return false;
        }
    }

    return true;
}
