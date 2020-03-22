//
// Created by owt50 on 2016/10/9.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_BASE_H
#define ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_BASE_H

#pragma once

#include <bitset>
#include <fstream>
#include <list>
#include <map>
#include <set>

#include <std/functional.h>
#include <std/smart_ptr.h>

#include <config/compiler_features.h>

#include <uv.h>

#include <lock/spin_lock.h>

#include "simulator_player_impl.h"

#include <cli/cmd_option.h>

extern "C" {
struct linenoiseCompletions;
};

#ifndef SIMULATOR_MSG_MAX_LENGTH

#ifdef ATBUS_MACRO_MSG_LIMIT
#define SIMULATOR_MSG_MAX_LENGTH (ATBUS_MACRO_MSG_LIMIT - 8192)
#else
#define SIMULATOR_MSG_MAX_LENGTH 65536
#endif

#endif

class simulator_base {
public:
    typedef std::shared_ptr<simulator_player_impl>         player_ptr_t;
    typedef std::function<void(util::cli::callback_param)> cmd_fn_t;

    struct cmd_autocomplete_flag_t {
        enum type { EN_CACF_FILES = 1, EN_CACF_MAX };
    };

    struct cmd_wrapper_t {
        typedef std::shared_ptr<cmd_wrapper_t>                   ptr_t;
        typedef std::map<util::cli::cmd_option_ci_string, ptr_t> value_type;
        value_type                                               children;
        cmd_wrapper_t *                                          parent;
        std::string                                              name;
        std::shared_ptr<util::cli::cmd_option_ci>                cmd_node;

        std::string                                       hint_;
        std::bitset<cmd_autocomplete_flag_t::EN_CACF_MAX> autocomplete_;

        cmd_wrapper_t(const std::string &n);

        // create a child node
        cmd_wrapper_t &operator[](const std::string &name);

        // bind a cmd handle
        std::shared_ptr<util::cli::cmd_option_ci> parent_node();

        // bind a cmd handle
        cmd_wrapper_t &bind(cmd_fn_t fn, const std::string &description);

        // bind a cmd handle
        cmd_wrapper_t &hint(std::string h);
    };

    struct linenoise_helper_t {
        std::list<std::string> complete;
        std::string            hint;
    };

public:
    simulator_base();
    virtual ~simulator_base();

    int init();

    void setup_signal();

    void setup_timer();

    int run(int argc, const char *argv[]);

    int stop();

    virtual int tick();

    template <typename Ty>
    bool insert_player(std::shared_ptr<Ty> player) {
        return insert_player(std::dynamic_pointer_cast<simulator_player_impl>(player));
    }

    bool         insert_player(player_ptr_t player);
    void         remove_player(const std::string &id, bool is_close = true);
    void         remove_player(player_ptr_t player);
    player_ptr_t get_player_by_id(const std::string &id);

    inline bool is_closing() const { return is_closing_; }

    bool sleep(time_t msec);

    template <typename Ty>
    std::shared_ptr<Ty> create_player(const std::string &host, int port) {
        if (is_closing_) {
            return std::shared_ptr<Ty>();
        }

        std::shared_ptr<Ty> ret  = std::make_shared<Ty>();
        player_ptr_t        bret = std::dynamic_pointer_cast<simulator_player_impl>(ret);
        ret->watcher_            = bret;
        bret->owner_             = this;

        if (0 != ret->connect(host, port)) {
            bret->owner_ = NULL;
            return std::shared_ptr<Ty>();
        }

        connecting_players_.insert(bret);
        return ret;
    }

    template <typename Ty, typename TParam>
    std::shared_ptr<Ty> create_player(const std::string &host, int port, TParam param) {
        if (is_closing_) {
            return std::shared_ptr<Ty>();
        }

        std::shared_ptr<Ty> ret  = std::make_shared<Ty>(param);
        player_ptr_t        bret = std::dynamic_pointer_cast<simulator_player_impl>(ret);
        ret->watcher_            = bret;
        bret->owner_             = this;

        if (0 != ret->connect(host, port)) {
            bret->owner_ = NULL;
            return std::shared_ptr<Ty>();
        }

        connecting_players_.insert(bret);
        return ret;
    }

    inline cmd_wrapper_t &                           reg_req() { return *root_; }
    inline uv_loop_t *                               get_loop() { return &loop_; }
    inline const char *                              get_exec() const { return exec_path_; }
    inline std::shared_ptr<util::cli::cmd_option>    get_option_manager() { return args_mgr_; }
    inline std::shared_ptr<util::cli::cmd_option_ci> get_cmd_manager() { return cmd_mgr_; }
    inline std::vector<unsigned char> &              get_msg_buffer() { return shell_opts_.buffer_; }

    // this must be thread-safe
    int insert_cmd(player_ptr_t player, const std::string &cmd);

    template <typename Ty>
    void set_current_player(std::shared_ptr<Ty> p) {
        set_current_player(std::dynamic_pointer_cast<simulator_player_impl>(p));
    }
    void                set_current_player(player_ptr_t p) { cmd_player_ = p; }
    const player_ptr_t &get_current_player() const { return cmd_player_; }

    std::vector<player_ptr_t> get_all_players_list();

    std::vector<player_ptr_t> get_all_connecting_players();

    virtual int  dispatch_message(player_ptr_t player, const void *buffer, size_t sz) = 0;
    virtual void exec_cmd(player_ptr_t player, const std::string &cmd)                = 0;

    static void libuv_on_async_cmd(uv_async_t *handle);

    static void                linenoise_completion(const char *buf, linenoiseCompletions *lc);
    static char *              linenoise_hint(const char *buf, int *color, int *bold);
    static linenoise_helper_t &linenoise_build_complete(const char *buf, bool complete, bool hint);
    static void                linenoise_thd_main(void *arg);

    static inline util::cli::cmd_option_ci::ptr_type conv_cmd_mgr(util::cli::cmd_option_ci::func_ptr_t in) {
        return std::dynamic_pointer_cast<util::cli::cmd_option_ci>(in);
    }

private:
    static void libuv_on_sleep_timeout(uv_timer_t *handle);

private:
    bool        is_closing_;
    const char *exec_path_;
    uv_loop_t   loop_;
    uv_async_t  async_cmd_;
    uv_mutex_t  async_cmd_lock_;

    player_ptr_t cmd_player_;
    uv_thread_t  thd_cmd_;
    typedef struct {
        bool        is_used;
        uv_signal_t sigint;
        uv_signal_t sigquit;
        uv_signal_t sigterm;
    } signal_set_t;
    signal_set_t signals_;

    typedef struct {
        bool       is_used;
        uv_timer_t timer;
    } timer_info_t;
    timer_info_t tick_timer_;
    timer_info_t sleep_timer_;

    std::map<std::string, player_ptr_t>       players_;
    std::set<player_ptr_t>                    connecting_players_;
    std::shared_ptr<util::cli::cmd_option_ci> cmd_mgr_;
    std::shared_ptr<util::cli::cmd_option>    args_mgr_;
    std::shared_ptr<cmd_wrapper_t>            root_;

protected:
    typedef struct {
        std::string                history_file;
        std::string                protocol_log;
        bool                       no_interactive;
        std::string                read_file;
        std::vector<std::string>   cmds;
        std::vector<unsigned char> buffer_;
        uint64_t                   tick_timer_interval;
    } shell_cmd_opts_t;
    shell_cmd_opts_t shell_opts_;

    typedef struct {
        util::lock::spin_lock                            lock;
        std::list<std::pair<player_ptr_t, std::string> > cmds;
        std::fstream                                     read_file_ios;
    } shell_cmd_data_t;
    shell_cmd_data_t shell_cmd_manager_;
};

template <typename TPlayer, typename TMsg>
class simulator_msg_base : public simulator_base {
public:
    typedef TMsg                      msg_t;
    typedef TPlayer                   player_t;
    typedef std::shared_ptr<player_t> player_ptr_t;

    typedef std::function<void(player_ptr_t, msg_t &)> rsp_fn_t;

    struct cmd_sender_t {
        player_ptr_t     player;
        std::list<msg_t> requests;
        simulator_base * self;
    };

public:
    virtual ~simulator_msg_base() {}

    bool insert_player(player_ptr_t player) { return simulator_base::insert_player<TPlayer>(player); }

    void reg_rsp(uint32_t msg_id, rsp_fn_t fn) { msg_id_handles_[msg_id] = fn; }

    void reg_rsp(const std::string &msg_name, rsp_fn_t fn) { msg_name_handles_[msg_name] = fn; }

    static cmd_sender_t &get_sender(util::cli::callback_param param) { return *reinterpret_cast<cmd_sender_t *>(param.get_ext_param()); }

    void write_protocol(const msg_t &msg, bool incoming) {
        const std::string &text = dump_message(msg);
        if (text.empty() || shell_opts_.protocol_log.empty()) {
            return;
        }

        if (!proto_file.is_open()) {
            proto_file.open(shell_opts_.protocol_log.c_str(), std::ios::app | std::ios::out | std::ios::binary);
        }

        if (proto_file.is_open()) {
            if (incoming) {
                proto_file << "<<<<<<<<<<<<" << std::endl;
            } else {
                proto_file << ">>>>>>>>>>>>" << std::endl;
            }
            proto_file << text << std::endl;
        }

        if (!shell_opts_.no_interactive) {
            if (incoming) {
                // std::cout << std::endl<< "<<<<<<<<<<<< " << pick_message_name(msg) << "(" << pick_message_id(msg) << ")"
                std::cout << std::endl << "<<<<<<<<<<<< " << text << std::endl;
            } else {
                // std::cout << std::endl<< ">>>>>>>>>>>> " << pick_message_name(msg) << "(" << pick_message_id(msg) << ")"
                std::cout << std::endl << ">>>>>>>>>>>> " << text << std::endl;
            }
        }
    }

    virtual int dispatch_message(simulator_base::player_ptr_t bp, const void *buffer, size_t sz) UTIL_CONFIG_OVERRIDE {
        player_ptr_t player = std::dynamic_pointer_cast<player_t>(bp);
        msg_t        msg;
        int          ret = unpack_message(msg, buffer, sz);
        if (ret < 0) {
            return ret;
        }

        write_protocol(msg, true);
        return dispatch_message(player, msg);
    }

    virtual int dispatch_message(player_ptr_t player, msg_t &msg) {
        uint32_t msg_id = pick_message_id(msg);
        if (msg_id != 0) {
            typename std::map<uint32_t, rsp_fn_t>::iterator iter = msg_id_handles_.find(msg_id);
            if (msg_id_handles_.end() != iter && iter->second) {
                iter->second(player, msg);
            }
        }

        std::string msg_name = pick_message_name(msg);
        if (!msg_name.empty()) {
            typename std::map<std::string, rsp_fn_t>::iterator iter = msg_name_handles_.find(msg_name);
            if (msg_name_handles_.end() != iter && iter->second) {
                iter->second(player, msg);
            }
        }

        return 0;
    }

    virtual void exec_cmd(simulator_base::player_ptr_t p, const std::string &cmd) UTIL_CONFIG_OVERRIDE {
        cmd_sender_t sender;
        sender.self   = this;
        sender.player = std::dynamic_pointer_cast<player_t>(p);
        get_cmd_manager()->start(cmd, true, &sender);
        p = std::dynamic_pointer_cast<simulator_player_impl>(sender.player);

        for (typename std::list<msg_t>::iterator iter = sender.requests.begin(); sender.player && iter != sender.requests.end(); ++iter) {
            size_t msg_len = get_msg_buffer().size();
            if (pack_message(*iter, &get_msg_buffer()[0], msg_len) >= 0) {

                if (msg_len < SIMULATOR_MSG_MAX_LENGTH) {
                    write_protocol(*iter, false);
                    int res = sender.player->on_write_message(p->last_network(), &get_msg_buffer()[0], msg_len);
                    if (res < 0) {
                        util::cli::shell_stream ss(std::cerr);
                        ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "player " << sender.player->get_id()
                             << " try to send data failed, res: " << res << std::endl;
                    }
                } else {
                    util::cli::shell_stream ss(std::cerr);
                    ss() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "player " << sender.player->get_id() << " try to send msg "
                         << pick_message_name(*iter) << "(" << pick_message_id(*iter) << ") failed, packaged data too large." << std::endl;
                }
            }
        }
    }

    virtual uint32_t pick_message_id(const msg_t &msg) const { return 0; };

    virtual std::string pick_message_name(const msg_t &msg) const { return std::string(); }

    virtual std::string dump_message(const msg_t &msg) { return std::string(); }

    virtual int pack_message(const msg_t &msg, void *buffer, size_t &sz) const  = 0;
    virtual int unpack_message(msg_t &msg, const void *buffer, size_t sz) const = 0;

private:
    std::map<uint32_t, rsp_fn_t>    msg_id_handles_;
    std::map<std::string, rsp_fn_t> msg_name_handles_;
    std::fstream                    proto_file;
};


#endif // ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_BASE_H
