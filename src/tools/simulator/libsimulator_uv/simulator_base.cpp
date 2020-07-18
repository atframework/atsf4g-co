//
// Created by owt50 on 2016/10/9.
//

#include <signal.h>

#include <cli/cmd_option.h>
#include <cli/cmd_option_phoenix.h>
#include <common/file_system.h>
#include <common/string_oprs.h>
#include <lock/lock_holder.h>
#include <std/foreach.h>
#include <time/time_utility.h>

#include <config/ini_loader.h>

#include "simulator_active.h"

#include "simulator_base.h"

#include "linenoise.h"

static simulator_base *g_last_simulator = NULL;


namespace detail {
    // 绑定的输出函数
    static void help_func(util::cli::callback_param stParams, simulator_base *self) {
        util::cli::shell_stream(std::cout)() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW << util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD
                                             << "Usage: " << self->get_exec() << " [options...]" << std::endl;
        self->get_option_manager()->set_help_cmd_style(util::cli::shell_font_style::SHELL_FONT_COLOR_MAGENTA |
                                                       util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD);

        std::cout << *self->get_option_manager() << std::endl;

        // 立即退出
        self->stop();
    }

    struct on_sys_cmd_help {
        simulator_base *owner;
        on_sys_cmd_help(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            // 指令
            std::cout << "Usage:" << std::endl;
            std::cout << "Commands:" << std::endl;
            owner->get_cmd_manager()->set_help_cmd_style(util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW |
                                                         util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD);
            std::cout << *owner->get_cmd_manager() << std::endl;
        }
    };

    struct on_sys_cmd_exec {
        simulator_base *owner;
        on_sys_cmd_exec(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            std::stringstream ss;
            for (size_t i = 0; i < params.get_params_number(); ++i) {
                ss << params[i]->to_cpp_string() << " ";
            }

            int res = system(ss.str().c_str());
            if (res != 0) {
                std::cout << "$? = " << res << std::endl;
            }
        }
    };

    struct on_sys_cmd_exit {
        simulator_base *owner;
        on_sys_cmd_exit(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) { owner->stop(); }
    };

    struct on_sys_cmd_set_player {
        simulator_base *owner;
        on_sys_cmd_set_player(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            SIMULATOR_CHECK_PARAMNUM(params, 1)

            simulator_base::player_ptr_t player = owner->get_player_by_id(params[0]->to_cpp_string());
            if (!player) {
                SIMULATOR_ERR_MSG() << "player " << params[0]->to_cpp_string() << " not found" << std::endl;
                return;
            }

            owner->set_current_player(player);
        }
    };

    struct on_sys_cmd_sleep {
        simulator_base *owner;
        on_sys_cmd_sleep(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            time_t msec = 1000;
            if (params.get_params_number() > 0) {
                util::config::ini_value val;
                val.add(params[0]->to_cpp_string());

                util::config::duration_value dur = val.as_duration();
                msec                             = dur.sec * 1000 + dur.nsec / 1000000;
            }

            if (msec > 0) {
                owner->sleep(msec);
            }
        }
    };

    struct on_sys_cmd_unknown {
        simulator_base *owner;
        on_sys_cmd_unknown(simulator_base *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            SIMULATOR_ERR_MSG() << "Invalid command";
            for (size_t i = 0; i < params.get_params_number(); ++i) {
                SIMULATOR_ERR_MSG() << " " << params[i]->to_cpp_string();
            }
            SIMULATOR_ERR_MSG() << std::endl;
        }
    };

    struct on_default_cmd_error {
        util::cli::cmd_option_ci *owner;
        on_default_cmd_error(util::cli::cmd_option_ci *s) : owner(s) {}

        void operator()(util::cli::callback_param params) {
            util::cli::shell_stream(std::cout)() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "Cmd Error." << std::endl;
            const typename util::cli::cmd_option_list::cmd_array_type &cmd_arr = params.get_cmd_array();
            size_t                                                     arr_sz  = cmd_arr.size();
            if (arr_sz < 2) {
                owner->dump(std::cout, "") << std::endl;
                return;
            }

            std::stringstream ss;
            for (size_t i = 1; i < arr_sz - 1; ++i) {
                if (!cmd_arr[i].first.empty()) {
                    ss << cmd_arr[i].first << " ";
                }
            }

            owner->dump(std::cout, ss.str()) << std::endl;
        }
    };
} // namespace detail


simulator_base::cmd_wrapper_t::cmd_wrapper_t(const std::string &n) : parent(NULL), name(n) {}

// create a child node
simulator_base::cmd_wrapper_t &simulator_base::cmd_wrapper_t::operator[](const std::string &nm) {
    std::vector<std::string> cmds = util::cli::cmd_option_ci::split_cmd(nm.c_str());
    if (cmds.empty()) {
        return *this;
    }

    ptr_t child;
    owent_foreach(std::string & cmd_name, cmds) {
        value_type::iterator iter = children.find(cmd_name.c_str());
        if (children.end() != iter) {
            child = iter->second;
            break;
        }
    }

    if (child && 1 == cmds.size()) {
        return *child;
    }

    if (!child) {
        child           = std::make_shared<cmd_wrapper_t>(nm);
        child->cmd_node = util::cli::cmd_option_ci::create();
        assert(child->cmd_node->empty());
        child->parent = this;
    }

    owent_foreach(std::string & cmd_name, cmds) { children[cmd_name.c_str()] = child; }

    return *child;
}

std::shared_ptr<util::cli::cmd_option_ci> simulator_base::cmd_wrapper_t::parent_node() {
    if (NULL == parent) {
        assert(cmd_node.get());
        return cmd_node;
    }

    if (!parent->cmd_node->empty()) {
        return parent->cmd_node;
    } else {
        // initialize parent binder
        std::shared_ptr<util::cli::cmd_option_ci> ppmgr = parent->parent_node();

        parent->cmd_node->bind_cmd("@OnError", detail::on_default_cmd_error(parent->cmd_node.get()));
        assert(!parent->cmd_node->empty());
        ppmgr->bind_child_cmd(parent->name, parent->cmd_node);
    }

    return parent->cmd_node;
}

// bind a cmd handle
simulator_base::cmd_wrapper_t &simulator_base::cmd_wrapper_t::bind(cmd_fn_t fn, const std::string &description) {
    parent_node()->bind_cmd(name, fn)->set_help_msg(description.c_str());
    return (*this);
}

simulator_base::cmd_wrapper_t &simulator_base::cmd_wrapper_t::hint(std::string h) {
    hint_ = h;
    return (*this);
}

simulator_base::simulator_base() : is_closing_(false), exec_path_(NULL) {
    uv_loop_init(&loop_);
    uv_async_init(&loop_, &async_cmd_, libuv_on_async_cmd);
    uv_mutex_init(&async_cmd_lock_);
    async_cmd_.data = this;

    cmd_mgr_        = util::cli::cmd_option_ci::create();
    args_mgr_       = util::cli::cmd_option::create();
    root_           = std::make_shared<cmd_wrapper_t>(std::string());
    root_->cmd_node = cmd_mgr_;

    shell_opts_.history_file   = ".simulator_history";
    shell_opts_.protocol_log   = "protocol.log";
    shell_opts_.no_interactive = false;
    shell_opts_.buffer_.resize(262144);
    shell_opts_.tick_timer_interval = 200; // 200 ms for default

    signals_.is_used     = false;
    tick_timer_.is_used  = false;
    sleep_timer_.is_used = false;

    g_last_simulator = this;
}

simulator_base::~simulator_base() {
    uv_mutex_destroy(&async_cmd_lock_);

    if (this == g_last_simulator) {
        g_last_simulator = NULL;
    }
}

// graceful Exits
static void simulator_setup_signal_func(uv_signal_t *handle, int signum) {
    switch (signum) {
    case SIGINT: {
        break;
    }

    case SIGTERM:
#ifndef WIN32
    case SIGQUIT:
#endif
    {
        simulator_base *base = (simulator_base *)handle->data;
        base->insert_cmd(base->get_current_player(), "quit");
        break;
    }

    default: { break; }
    }
}

int simulator_base::init() {
    // register inner cmd and helper msg
    args_mgr_->bind_cmd("?, -h, --help, help", detail::help_func, this)->set_help_msg("show help message and exit");
    args_mgr_->bind_cmd("--history, --history-file", util::cli::phoenix::assign(shell_opts_.history_file))
        ->set_help_msg("<file path> set command history file");
    args_mgr_->bind_cmd("--protocol, --protocol-log", util::cli::phoenix::assign(shell_opts_.protocol_log))->set_help_msg("<file path> set protocol log file");
    args_mgr_->bind_cmd("-ni, --no-interactive", util::cli::phoenix::set_const(shell_opts_.no_interactive, true))->set_help_msg("disable interactive mode");
    args_mgr_->bind_cmd("-f, --rf, --read-file", util::cli::phoenix::assign<std::string>(shell_opts_.read_file))->set_help_msg("read from file");
    args_mgr_->bind_cmd("-c, --cmd", util::cli::phoenix::push_back(shell_opts_.cmds))->set_help_msg("[cmd ...] add cmd to run");
    args_mgr_->bind_cmd("-t, --timer-interval", util::cli::phoenix::assign(shell_opts_.tick_timer_interval))
        ->set_help_msg("<timer interval> set timer interval in miliseconds");

    reg_req()["!, sh"]
        .bind(detail::on_sys_cmd_exec(this), "<command> [parameters...] execute a external command")
        .autocomplete_.set(cmd_autocomplete_flag_t::EN_CACF_FILES, true);
    reg_req()["?, help"].bind(detail::on_sys_cmd_help(this), "show help message");
    reg_req()["exit, quit"].bind(detail::on_sys_cmd_exit(this), "exit");
    reg_req()["set_player"].bind(detail::on_sys_cmd_set_player(this), "<player id> set current player");
    reg_req()["sleep"].bind(detail::on_sys_cmd_sleep(this),
                            "<NUMBER[s/ms/m/h/d/w]> sleep timeout, the unit can be s(second), ms(millisecond), m(minute), h(hour), d(day), w(week)");
    cmd_mgr_->bind_cmd("@OnError", detail::on_sys_cmd_unknown(this));

    // register all protocol callbacks
    ::proto::detail::simulator_activitor::active_all(this);

    return 0;
}

void simulator_base::setup_signal() {
    if (signals_.is_used) {
        return;
    }
    signals_.is_used = true;

    uv_signal_init(&loop_, &signals_.sigint);
    uv_signal_init(&loop_, &signals_.sigquit);
    uv_signal_init(&loop_, &signals_.sigterm);
    signals_.sigint.data  = this;
    signals_.sigquit.data = this;
    signals_.sigterm.data = this;

    // block signals
    uv_signal_start(&signals_.sigint, simulator_setup_signal_func, SIGINT);
    uv_signal_start(&signals_.sigterm, simulator_setup_signal_func, SIGTERM);

#ifndef WIN32
    uv_signal_start(&signals_.sigquit, simulator_setup_signal_func, SIGQUIT);
    signal(SIGHUP, SIG_IGN);  // lost parent process
    signal(SIGPIPE, SIG_IGN); // close stdin, stdout or stderr
    signal(SIGTSTP, SIG_IGN); // close tty
    signal(SIGTTIN, SIG_IGN); // tty input
    signal(SIGTTOU, SIG_IGN); // tty output
#endif
}

static void simulator_base_timer_cb(uv_timer_t *handle) {
    if (NULL == handle) {
        return;
    }

    simulator_base *self = reinterpret_cast<simulator_base *>(handle->data);
    if (NULL == self) {
        return;
    }

    self->tick();
}

void simulator_base::setup_timer() {
    if (tick_timer_.is_used || 0 == shell_opts_.tick_timer_interval) {
        return;
    }
    tick_timer_.is_used = true;

    uv_timer_init(&loop_, &tick_timer_.timer);
    tick_timer_.timer.data = this;
    uv_timer_start(&tick_timer_.timer, simulator_base_timer_cb, 1000, shell_opts_.tick_timer_interval);
}

int simulator_base::run(int argc, const char *argv[]) {
    util::time::time_utility::update(NULL);
    if (argc > 0) {
        exec_path_ = argv[0];
    }
    args_mgr_->start(argc, argv, false, NULL);
    if (is_closing_) {
        return 0;
    }

    setup_signal();
    setup_timer();

    // startup interactive thread
    uv_thread_create(&thd_cmd_, linenoise_thd_main, this);

    uv_timer_init(&loop_, &sleep_timer_.timer);
    sleep_timer_.timer.data = this;

    int ret = uv_run(&loop_, UV_RUN_DEFAULT);
    uv_close((uv_handle_t *)&async_cmd_, NULL);

    if (signals_.is_used) {
        signals_.is_used = false;

        uv_signal_stop(&signals_.sigint);
        uv_close((uv_handle_t *)&signals_.sigint, NULL);

        uv_signal_stop(&signals_.sigquit);
        uv_close((uv_handle_t *)&signals_.sigquit, NULL);

        uv_signal_stop(&signals_.sigterm);
        uv_close((uv_handle_t *)&signals_.sigterm, NULL);
    }

    if (tick_timer_.is_used) {
        tick_timer_.is_used = false;

        tick_timer_.timer.data = NULL;
        uv_timer_stop(&tick_timer_.timer);
        uv_close((uv_handle_t *)&tick_timer_.timer, NULL);
    }


    sleep_timer_.timer.data = NULL;
    if (sleep_timer_.is_used) {
        sleep_timer_.is_used = false;

        // reactive async cmd, in case of cmd breaking
        uv_async_send(&async_cmd_);
    }
    uv_timer_stop(&sleep_timer_.timer);
    uv_close((uv_handle_t *)&sleep_timer_.timer, NULL);

    uv_thread_join(&thd_cmd_);
    while (UV_EBUSY == uv_loop_close(&loop_)) {
        uv_run(&loop_, UV_RUN_ONCE);
    }

    return ret;
}

int simulator_base::stop() {
    is_closing_ = true;

    for (std::map<std::string, player_ptr_t>::iterator iter = players_.begin(); iter != players_.end(); ++iter) {
        iter->second->close();
    }
    players_.clear();

    for (std::set<player_ptr_t>::iterator iter = connecting_players_.begin(); iter != connecting_players_.end(); ++iter) {
        (*iter)->close();
    }
    connecting_players_.clear();

    uv_stop(&loop_);
    return 0;
}

int simulator_base::tick() { return 0; }

bool simulator_base::insert_player(player_ptr_t player) {
    util::time::time_utility::update(NULL);
    if (is_closing_) {
        return false;
    }

    if (!player) {
        return false;
    }

    if (this == player->owner_) {
        return true;
    }

    if (NULL != player->owner_) {
        return false;
    }

    if (players_.end() != players_.find(player->get_id())) {
        return false;
    }

    players_[player->get_id()] = player;
    connecting_players_.erase(player);
    player->owner_ = this;
    return true;
}

void simulator_base::remove_player(const std::string &id, bool is_close) {
    util::time::time_utility::update(NULL);
    // will do it in stop function
    if (is_closing_) {
        return;
    }

    std::map<std::string, player_ptr_t>::iterator iter = players_.find(id);
    if (players_.end() == iter) {
        return;
    }

    if (is_close) {
        iter->second->close();
    }

    if (iter->second == cmd_player_) {
        cmd_player_.reset();
    }
    iter->second->owner_ = NULL;
    players_.erase(iter);
}

void simulator_base::remove_player(player_ptr_t player) {
    util::time::time_utility::update(NULL);
    // will do it in stop function
    if (is_closing_ || !player) {
        return;
    }

    remove_player(player->get_id());
    connecting_players_.erase(player);
}

simulator_base::player_ptr_t simulator_base::get_player_by_id(const std::string &id) {
    std::map<std::string, player_ptr_t>::iterator iter = players_.find(id);
    if (players_.end() == iter) {
        return NULL;
    }

    return iter->second;
}

void simulator_base::libuv_on_sleep_timeout(uv_timer_t *handle) {
    if (NULL == handle) {
        return;
    }

    simulator_base *self = reinterpret_cast<simulator_base *>(handle->data);
    if (NULL == self) {
        return;
    }

    // uv_timer_stop(handle);
    self->sleep_timer_.is_used = false;
    uv_async_send(&self->async_cmd_);
}

bool simulator_base::sleep(time_t msec) {
    if (msec <= 0) {
        return false;
    }

    if (sleep_timer_.is_used) {
        return false;
    }

    if (0 != uv_timer_start(&sleep_timer_.timer, libuv_on_sleep_timeout, msec, 0)) {
        uv_close((uv_handle_t *)&sleep_timer_.timer, NULL);
        sleep_timer_.timer.data = NULL;
        return false;
    }

    sleep_timer_.is_used = true;
    return true;
}

int simulator_base::insert_cmd(player_ptr_t player, const std::string &cmd) {
    if (is_closing()) {
        return -1;
    }

    // must be thread-safe
    util::lock::lock_holder<util::lock::spin_lock> holder(shell_cmd_manager_.lock);
    shell_cmd_manager_.cmds.push_back(std::pair<player_ptr_t, std::string>(player, cmd));
    uv_async_send(&async_cmd_);
    return 0;
}

std::vector<simulator_base::player_ptr_t> simulator_base::get_all_players_list() {
    std::vector<player_ptr_t> ret;
    ret.reserve(players_.size());
    for (std::map<std::string, player_ptr_t>::iterator iter = players_.begin(); iter != players_.end(); ++iter) {
        ret.push_back(iter->second);
    }

    return ret;
}

std::vector<simulator_base::player_ptr_t> simulator_base::get_all_connecting_players() {
    std::vector<player_ptr_t> ret;
    ret.reserve(connecting_players_.size());
    for (std::set<player_ptr_t>::iterator iter = connecting_players_.begin(); iter != connecting_players_.end(); ++iter) {
        ret.push_back(*iter);
    }

    return ret;
}

void simulator_base::libuv_on_async_cmd(uv_async_t *handle) {
    simulator_base *self = reinterpret_cast<simulator_base *>(handle->data);
    assert(self);

    while (true) {
        util::time::time_utility::update(NULL);
        // sleeping skip running
        if (self->sleep_timer_.is_used) {
            return;
        }

        std::pair<player_ptr_t, std::string> cmd;
        {
            util::lock::lock_holder<util::lock::spin_lock> holder(self->shell_cmd_manager_.lock);
            if (self->shell_cmd_manager_.cmds.empty()) {
                break;
            }
            cmd = self->shell_cmd_manager_.cmds.front();
            self->shell_cmd_manager_.cmds.pop_front();
        }

        self->exec_cmd(cmd.first, cmd.second);
    }

    if (!self->shell_opts_.no_interactive) {
        uv_mutex_trylock(&self->async_cmd_lock_);
        uv_mutex_unlock(&self->async_cmd_lock_);
    }

    if (!self->is_closing() && self->shell_cmd_manager_.read_file_ios.is_open() && !self->shell_cmd_manager_.read_file_ios.eof()) {
        std::string cmd;
        while (!self->is_closing() && std::getline(self->shell_cmd_manager_.read_file_ios, cmd)) {
            size_t space_cnt = 0;
            for (size_t i = 0; i < cmd.size(); ++i) {
                if (' ' == cmd[i] || '\t' == cmd[i] || '\r' == cmd[i] || '\n' == cmd[i]) {
                    ++space_cnt;
                }
            }

            if (space_cnt != cmd.size()) {
                self->insert_cmd(self->cmd_player_, cmd);
                break;
            }
        }
    }
}


void simulator_base::linenoise_completion(const char *buf, linenoiseCompletions *lc) {
    linenoise_helper_t &res = linenoise_build_complete(buf, true, false);

    owent_foreach(std::string & cmd, res.complete) { linenoiseAddCompletion(lc, cmd.c_str()); }
}

char *simulator_base::linenoise_hint(const char *buf, int *color, int *bold) {
    linenoise_helper_t &res = linenoise_build_complete(buf, false, true);

    if (!res.hint.empty()) {
        *color = 33;
        *bold  = 1;
        return &res.hint[0];
    }

    return NULL;
}

simulator_base::linenoise_helper_t &simulator_base::linenoise_build_complete(const char *line, bool complete, bool hint) {
    static linenoise_helper_t ret;
    if (complete) {
        ret.complete.clear();
    }

    if (hint) {
        ret.hint.clear();
    }

    if (NULL == g_last_simulator) {
        return ret;
    }

    // text 记录的是当前单词， rl_line_buffer 记录的是完整行
    // =========================
    std::stringstream ss;
    ss.str(line);

    cmd_wrapper_t *parent       = &g_last_simulator->reg_req();
    const char *   last_matched = line;
    const char *   curr_matched = line;
    std::string    ident;
    while (curr_matched && *curr_matched) {
        curr_matched = util::cli::cmd_option_ci::get_segment(curr_matched, ident);
        if (ident.empty()) {
            break;
        }

        simulator_base::cmd_wrapper_t::value_type::iterator iter = parent->children.find(ident.c_str());
        if (iter == parent->children.end()) {
            break;
        }

        parent       = iter->second.get();
        last_matched = curr_matched;
        ident.clear();
    }

    // 查找不完整词
    if (ident.size() > 0) {
        cmd_wrapper_t::value_type::iterator iter = parent->children.lower_bound(ident.c_str());
        std::string                         rule;

        for (; iter != parent->children.end(); ++iter) {
            if (iter->first.size() < ident.size()) {
                continue;
            }

            if (iter->first.empty()) {
                continue;
            }

            if (0 != UTIL_STRFUNC_STRNCASE_CMP(iter->first.c_str(), ident.c_str(), ident.size())) {
                break;
            }

            if (static_cast<size_t>(curr_matched - last_matched) == ident.size()) {
                if (hint && ret.hint.empty()) {
                    ret.hint += iter->first.c_str() + ident.size();

                    if (!complete) {
                        break;
                    }
                }

                if (complete) {
                    rule.reserve(curr_matched - line + 1 + iter->first.size());
                    rule.assign(line, curr_matched);
                    rule += iter->first.c_str() + ident.size();
                    ret.complete.push_back(rule);
                }
            }
        }

        // file system complete
        if (complete && ret.complete.empty() && parent->autocomplete_.test(cmd_autocomplete_flag_t::EN_CACF_FILES)) {
            std::list<std::string> fls;
            std::string            prefix, dir, next_ident;
            while (curr_matched && *curr_matched) {
                const char *prev_matched = curr_matched;
                curr_matched             = util::cli::cmd_option_ci::get_segment(curr_matched, next_ident);
                if (!next_ident.empty()) {
                    ident        = next_ident;
                    last_matched = prev_matched;
                }
            }

            if (curr_matched && ' ' != *(curr_matched - 1) && '\t' != *(curr_matched - 1) && '\r' != *(curr_matched - 1) && '\n' != *(curr_matched - 1)) {
                prefix = last_matched;
            } else {
                last_matched = curr_matched;
            }
            // 枚举目录
            if (!prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\')) {
                prefix.pop_back();
                dir = prefix;
            } else {
                // 枚举文件前缀
                util::file_system::dirname(prefix.c_str(), prefix.size(), dir);
            }

            util::file_system::scan_dir(dir.c_str(), fls);
            owent_foreach(std::string & fl, fls) {
                if (0 == UTIL_STRFUNC_STRNCASE_CMP(prefix.c_str(), fl.c_str(), prefix.size())) {
                    rule.assign(line, last_matched);
                    rule += fl;
                    ret.complete.push_back(rule);
                }
            }
        }
    } else {
        // 子命令
        if (complete) {
            std::string                         rule;
            cmd_wrapper_t::value_type::iterator iter = parent->children.begin();
            for (; iter != parent->children.end(); ++iter) {
                rule.reserve(last_matched - line + 1 + iter->first.size());
                rule.assign(line, last_matched);

                if (iter->first.empty()) {
                    continue;
                }

                if (!rule.empty() && rule.back() != ' ' && rule.back() != '\t') {
                    rule += ' ';
                }
                rule += iter->first.c_str();
                ret.complete.push_back(rule);
            }

            // file system
            if (parent->autocomplete_.test(cmd_autocomplete_flag_t::EN_CACF_FILES)) {
                std::list<std::string> fls;
                util::file_system::scan_dir(NULL, fls);
                owent_foreach(std::string & fl, fls) {
                    rule.assign(line, last_matched);
                    if (!rule.empty() && rule.back() != ' ' && rule.back() != '\t') {
                        rule += ' ';
                    }

                    ret.complete.push_back(rule + fl);
                }
            }
        }

        // 没有子命令则走hint
        if (hint && !parent->hint_.empty()) {
            ret.hint = parent->hint_;
        }
    }

    return ret;
}

void simulator_base::linenoise_thd_main(void *arg) {
    simulator_base *self = reinterpret_cast<simulator_base *>(arg);
    assert(self);

    if (!self->shell_opts_.cmds.empty()) {
        owent_foreach(std::string & cmd, self->shell_opts_.cmds) { self->insert_cmd(self->cmd_player_, cmd); }
    }

    if (!self->shell_opts_.read_file.empty()) {
        g_last_simulator->shell_cmd_manager_.read_file_ios.open(self->shell_opts_.read_file.c_str(), std::ios::in);

        uv_mutex_lock(&g_last_simulator->async_cmd_lock_);
        uv_async_send(&g_last_simulator->async_cmd_);
    }


    if (self->shell_opts_.no_interactive) {
        return;
    }

    // init
    linenoiseSetCompletionCallback(linenoise_completion);
    linenoiseSetHintsCallback(linenoise_hint);
    if (!self->shell_opts_.history_file.empty()) {
        linenoiseHistoryLoad(self->shell_opts_.history_file.c_str());
    }

    // readline loop
    std::string prompt      = "~>";
    char *      cmd_c       = NULL;
    bool        is_continue = true;
    while (is_continue && NULL != g_last_simulator && !g_last_simulator->is_closing()) {
        // reset errno, or ctrl+c will loop
        errno = 0;
        cmd_c = linenoise(prompt.c_str());

        // ctrl+c
        if (errno == EAGAIN) {
            continue;
        }

        // skip empty line
        if (NULL != cmd_c && *cmd_c != '\0') {
            uv_mutex_lock(&g_last_simulator->async_cmd_lock_);
            g_last_simulator->insert_cmd(g_last_simulator->get_current_player(), cmd_c);

            while (true) {
                uv_mutex_lock(&g_last_simulator->async_cmd_lock_);
                bool next_cmd = g_last_simulator->shell_cmd_manager_.cmds.empty();
                uv_mutex_unlock(&g_last_simulator->async_cmd_lock_);

                if (next_cmd) {
                    break;
                }
            }

            linenoiseHistoryAdd(cmd_c);
            if (!self->shell_opts_.history_file.empty()) {
                linenoiseHistorySave(self->shell_opts_.history_file.c_str());
            }
        }

        if (cmd_c != NULL) {
            linenoiseFree(cmd_c);
        }

        // update promote
        if (self->get_current_player()) {
            prompt = self->get_current_player()->get_id() + ">";
        }
    }

    if (NULL != g_last_simulator) {
        g_last_simulator->shell_opts_.no_interactive = true;
    }
}
