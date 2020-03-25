//
// Created by owt50 on 2016/9/27.
//

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <config/compiler_features.h>
#include <config/extern_log_categorize.h>
#include <cstdlib>
#include <cstring>
#include <log/log_wrapper.h>
#include <std/foreach.h>
#include <time/time_utility.h>

#include <utility/random_engine.h>

#include <hiredis_happ.h>

#if defined(HOREDIS_HAPP_LIBHIREDIS_USING_SRC) && HOREDIS_HAPP_LIBHIREDIS_USING_SRC
#include <adapters/libuv.h>
#else
#include <hiredis/adapters/libuv.h>
#endif


#include "db_msg_dispatcher.h"
#include <config/logic_config.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>


struct db_async_data_t {
    uint64_t task_id;
    uint64_t bus_id;
    uint64_t sequence;

    redisReply *                   response;
    db_msg_dispatcher::unpack_fn_t unpack_fn;
};

static void _uv_close_and_free_callback(uv_handle_t *handle) { delete (uv_timer_t *)handle; }

#if defined(UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT) && UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT
#include <type_traits>
static_assert(std::is_trivial<db_async_data_t>::value, "db_async_data_t must be a trivial, because it will stored in a "
                                                       "buffer and will not call dtor fn");
#endif

db_msg_dispatcher::db_msg_dispatcher() : sequence_allocator_(0), tick_timer_(NULL), tick_msg_count_(0) {}

db_msg_dispatcher::~db_msg_dispatcher() {
    if (NULL != tick_timer_) {
        uv_timer_stop(tick_timer_);
        uv_close((uv_handle_t *)tick_timer_, _uv_close_and_free_callback);
        tick_timer_ = NULL;
    }
}

int32_t db_msg_dispatcher::init() {
    uv_loop_t *loop = uv_default_loop();

    if (NULL == tick_timer_) {
        tick_timer_ = new (std::nothrow) uv_timer_t();
        if (NULL == tick_timer_) {
            WLOGERROR("malloc timer failed");
            return hello::err::EN_SYS_MALLOC;
        }
        tick_timer_->data = this;

        int res = 0;
        do {
            int res = uv_timer_init(loop, tick_timer_);
            if (0 != res) {
                WLOGERROR("init db dispatcher timer failed, res: %d", res);
                break;
            }

            // load proc interval from configure
            res = uv_timer_start(tick_timer_, db_msg_dispatcher::on_timer_proc, logic_config::me()->get_cfg_db().proc, logic_config::me()->get_cfg_db().proc);
            if (0 != res) {
                WLOGERROR("start db dispatcher timer failed, res: %d", res);
                break;
            }
        } while (false);

        if (0 != res) {
            delete tick_timer_;
            tick_timer_ = NULL;
        }
    }

    // init
    cluster_init(logic_config::me()->get_cfg_db().cluster_default, channel_t::CLUSTER_DEFAULT);
    raw_init(logic_config::me()->get_cfg_db().raw_default, channel_t::RAW_DEFAULT);
    return hello::err::EN_SUCCESS;
}

int db_msg_dispatcher::tick() {
    tick_msg_count_ = 0;
    int prev_count  = -1;

    // no more than 64 messages in one tick
    while (prev_count != tick_msg_count_ && prev_count < 64) {
        prev_count = tick_msg_count_;
        uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    }

    return tick_msg_count_;
}

int32_t db_msg_dispatcher::dispatch(const void *msg_buf, size_t msg_buf_sz) {
    assert(msg_buf_sz == sizeof(db_async_data_t));
    hello::table_all_message table_msg;

    const db_async_data_t *req = reinterpret_cast<const db_async_data_t *>(msg_buf);

    if (NULL == req->response) {
        WLOGERROR("task [0x%llx] DB msg, no response found", static_cast<unsigned long long>(req->task_id));
        return hello::err::EN_SYS_PARAM;
    }

    int ret = hello::err::EN_SUCCESS;
    switch (req->response->type) {
    case REDIS_REPLY_STATUS: {
        if (0 == UTIL_STRFUNC_STRNCASE_CMP("OK", req->response->str, 2)) {
            WLOGINFO("db reply status: %s", req->response->str);
        } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("CAS_FAILED", req->response->str, 10)) {
            WLOGINFO("db reply status: %s", req->response->str);
            if (req->response->str[10] && req->response->str[11]) {
                table_msg.set_version(&req->response->str[11]);
            }
            ret = hello::err::EN_DB_OLD_VERSION;
        } else {
            table_msg.set_version(req->response->str);
            ret = hello::err::EN_SUCCESS;
        }
        break;
    }
    case REDIS_REPLY_ERROR: {
        if (0 == UTIL_STRFUNC_STRNCASE_CMP("CAS_FAILED", req->response->str, 10)) {
            if (req->response->str[10] && req->response->str[11]) {
                table_msg.set_version(&req->response->str[11]);
            }
            ret = hello::err::EN_DB_OLD_VERSION;
        } else {
            WLOGERROR("db reply error: %s", req->response->str);
            ret = hello::err::EN_DB_REPLY_ERROR;
        }
        break;
    }
    default: {
        if (NULL != req->unpack_fn) {
            ret = req->unpack_fn(table_msg, req->response);
            if (ret < 0) {
                WLOGERROR("db unpack data error, res: %d", ret);
            }
        } else if (REDIS_REPLY_STRING == req->response->type) {
            WLOGINFO("db reply msg: %s", req->response->str);
        }
        break;
    }
    }

    table_msg.set_bus_id(req->bus_id);
    table_msg.set_dst_task_id(req->task_id);
    table_msg.set_error_code(ret);

    msg_raw_t msg;
    msg.msg_addr = &table_msg;
    msg.msg_type = get_instance_ident();
    ret          = on_recv_msg(msg, req->response, req->sequence);
    return ret;
}

uint64_t db_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
    hello::table_all_message *real_msg = get_protobuf_msg<hello::table_all_message>(raw_msg);
    if (NULL == real_msg) {
        return 0;
    }

    return real_msg->dst_task_id();
}

db_msg_dispatcher::msg_type_t db_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) { return 0; }

int db_msg_dispatcher::send_msg(channel_t::type t, const char *ks, size_t kl, uint64_t task_id, uint64_t pd, unpack_fn_t fn, uint64_t& sequence, 
    int argc, const char **argv, const size_t *argvlen) {

    if (t > channel_t::CLUSTER_BOUND && t < channel_t::SENTINEL_BOUND) {
        if (db_cluster_conns_[t]) {
            return cluster_send_msg(*db_cluster_conns_[t], ks, kl, task_id, pd, fn, sequence, argc, argv, argvlen);
        } else {
            WLOGERROR("db cluster %d not inited", static_cast<int>(t));
            return hello::err::EN_SYS_INIT;
        }
    }

    if (t >= channel_t::RAW_DEFAULT && t < channel_t::RAW_BOUND) {
        if (db_raw_conns_[t - channel_t::RAW_DEFAULT]) {
            return raw_send_msg(*db_raw_conns_[t - channel_t::RAW_DEFAULT], task_id, pd, fn, sequence, argc, argv, argvlen);
        } else {
            WLOGERROR("db single %d not inited", static_cast<int>(t));
            return hello::err::EN_SYS_INIT;
        }
    }

    WLOGERROR("db channel %d invalid", static_cast<int>(t));
    return hello::err::EN_SYS_PARAM;
}

void *db_msg_dispatcher::get_cache_buffer(size_t len) {
    if (pack_cache_.size() < len) {
        size_t sz = 1;
        while (sz < len) {
            sz <<= 1;
        }
        pack_cache_.resize(sz);
    }

    return &pack_cache_[0];
}

bool db_msg_dispatcher::is_available(channel_t::type t) const {
    if (t >= channel_t::RAW_DEFAULT && t < channel_t::RAW_BOUND) {
        return !!db_raw_conns_[t - channel_t::RAW_DEFAULT];
    } else {
        return !!db_cluster_conns_[t];
    }
}

const std::string &db_msg_dispatcher::get_db_script_sha1(uint32_t type) const { return db_script_sha1_[type % hello::EnDBScriptShaType_ARRAYSIZE]; }

void db_msg_dispatcher::set_db_script_sha1(uint32_t type, const char *str, int len) {
    db_script_sha1_[type % hello::EnDBScriptShaType_ARRAYSIZE].assign(str, len);
}

void db_msg_dispatcher::set_on_connected(channel_t::type t, user_callback_t fn) {
    if (t >= channel_t::MAX || t < 0) {
        return;
    }

    user_callback_onconnected_[t].push_back(fn);
}

void db_msg_dispatcher::log_debug_fn(const char *content) { WCLOGDEBUG(log_categorize_t::DB, "%s", content); }

void db_msg_dispatcher::log_info_fn(const char *content) { WCLOGINFO(log_categorize_t::DB, "%s", content); }

int db_msg_dispatcher::script_load(redisAsyncContext *c, uint32_t type) {
    // load lua script
    int                status;
    std::string        script;
    const std::string &script_file_path = logic_config::me()->get_cfg_db().db_script_file[type % hello::EnDBScriptShaType_ARRAYSIZE];
    if (script_file_path.empty()) {
        return 0;
    }

    open_file(script_file_path.c_str(), script);
    status = redisAsyncCommand(c, script_callback, reinterpret_cast<void *>(static_cast<intptr_t>(type % hello::EnDBScriptShaType_ARRAYSIZE)), "SCRIPT LOAD %s",
                               script.c_str());
    if (REDIS_OK != status) {
        WLOGERROR("send db msg failed, status: %d, msg: %s", status, c->errstr);
    }

    return status;
}

int db_msg_dispatcher::open_file(const char *file, std::string &script) {
    script.clear();
    char path[util::file_system::MAX_PATH_LEN];
    UTIL_STRFUNC_SNPRINTF(path, util::file_system::MAX_PATH_LEN, "%s/script/%s", logic_config::me()->get_cfg_logic().server_resource_dir.c_str(), file);

    if (false == util::file_system::get_file_content(script, path, false)) {
        WLOGERROR("load db script file %s failed", path);
        return hello::err::EN_SYS_NOTFOUND;
    }

    return 0;
}

void db_msg_dispatcher::on_timer_proc(uv_timer_t *handle) {
    time_t sec  = util::time::time_utility::get_now();
    time_t usec = util::time::time_utility::get_now_usec();

    db_msg_dispatcher *dispatcher = reinterpret_cast<db_msg_dispatcher *>(handle->data);
    assert(dispatcher);
    owent_foreach(std::shared_ptr<hiredis::happ::cluster> & clu, dispatcher->db_cluster_conns_) {
        if (!clu) {
            continue;
        }

        clu->proc(sec, usec);
    }

    owent_foreach(std::shared_ptr<hiredis::happ::raw> & raw_conn, dispatcher->db_raw_conns_) {
        if (!raw_conn) {
            continue;
        }

        raw_conn->proc(sec, usec);
    }
}

void db_msg_dispatcher::script_callback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = reinterpret_cast<redisReply *>(r);

    if (reply && reply->type == REDIS_REPLY_STRING && reply->str) {
        WLOGDEBUG("db script reply: %s", reply->str);
        (me()->set_db_script_sha1(reinterpret_cast<intptr_t>(privdata), reply->str, reply->len));

    } else if (c->err) {
        WLOGERROR("db got a error response, %s", c->errstr);
    }

    // 响应调度器
    ++me()->tick_msg_count_;
}

// cluster
int db_msg_dispatcher::cluster_init(const std::vector<logic_config::LC_DBCONN> &conns, int index) {
    if (index >= channel_t::SENTINEL_BOUND || index < 0) {
        return hello::err::EN_SYS_PARAM;
    }

    std::shared_ptr<hiredis::happ::cluster> &conn = db_cluster_conns_[index];
    if (conn) {
        conn->reset();
    }
    conn.reset();

    if (conns.empty()) {
        return hello::err::EN_SUCCESS;
    }

    conn            = std::make_shared<hiredis::happ::cluster>();
    size_t conn_idx = util::random_engine::random_between<size_t>(0, conns.size());

    // 初始化
    conn->init(conns[conn_idx].host, conns[conn_idx].port);

    // 设置日志handle
    {
        hiredis::happ::cluster::log_fn_t info_fn  = db_msg_dispatcher::log_info_fn;
        hiredis::happ::cluster::log_fn_t debug_fn = db_msg_dispatcher::log_debug_fn;

        util::log::log_wrapper *wrapper = WLOG_GETCAT(log_categorize_t::DB);
        if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
            debug_fn = NULL;
        }

        if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_INFO)) {
            info_fn = NULL;
        }

        conn->set_log_writer(info_fn, debug_fn);
    }

    // 设置连接成功注入login脚本和user脚本
    conn->set_on_connect(db_msg_dispatcher::cluster_on_connect);
    conn->set_on_connected(db_msg_dispatcher::cluster_on_connected);

    conn->set_timeout(logic_config::me()->get_cfg_db().timeout);
    conn->set_timer_interval(logic_config::me()->get_cfg_db().time_retry_sec, logic_config::me()->get_cfg_db().time_retry_usec);

    conn->set_cmd_buffer_size(sizeof(db_async_data_t));

    // 启动cluster
    if (conn->start() >= 0) {
        return 0;
    }

    return hello::err::EN_SYS_INIT;
}

void db_msg_dispatcher::cluster_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r, void *privdata) {
    redisReply *     reply = reinterpret_cast<redisReply *>(r);
    db_async_data_t *req   = reinterpret_cast<db_async_data_t *>(privdata);

    // 所有的请求都应该走标准流程，出错了
    if (NULL == req) {
        WLOGERROR("all cmd should has a req data");
        return;
    }

    do {
        // 无回包,可能是连接出现问题
        if (NULL == reply) {
            if (NULL == c) {
                WLOGERROR("connect to db failed.");
            } else if (c->err) {
                WLOGERROR("db got a error response, %s", c->errstr);
            }

            break;
        }

        // 响应调度器
        req->response = reply;
        me()->dispatch(req, sizeof(db_async_data_t));

        ++me()->tick_msg_count_;
    } while (false);
}

void db_msg_dispatcher::cluster_on_connect(hiredis::happ::cluster *, hiredis::happ::connection *conn) {
    assert(conn);

    // 加入事件池
    redisLibuvAttach(conn->get_context(), uv_default_loop());
}

void db_msg_dispatcher::cluster_on_connected(hiredis::happ::cluster *clu, hiredis::happ::connection *conn, const struct redisAsyncContext *, int status) {
    if (0 != status || NULL == conn) {
        WLOGERROR("connect to db host %s failed, status: %d", (NULL == conn ? "Unknown" : conn->get_key().name.c_str()), status);
        return;
    }

    WLOGINFO("connect to db host %s success", conn->get_key().name.c_str());
    // 注入redis的lua脚本
    for (int i = 0; i < hello::EnDBScriptShaType_descriptor()->value_count(); ++i) {
        me()->script_load(conn->get_context(), hello::EnDBScriptShaType_descriptor()->value(i)->number());
    }

    for (int i = 0; i < channel_t::SENTINEL_BOUND; ++i) {
        std::shared_ptr<hiredis::happ::cluster> &clu_ptr = me()->db_cluster_conns_[i];
        if (clu_ptr && clu_ptr.get() == clu) {
            auto &ucbk = me()->user_callback_onconnected_[i];
            for (auto &cb : ucbk) {
                if (cb) cb();
            }
        }
    }
}

int db_msg_dispatcher::cluster_send_msg(hiredis::happ::cluster &clu, const char *ks, size_t kl, uint64_t task_id, uint64_t pd, unpack_fn_t fn, 
    uint64_t& sequence, int argc, const char **argv, const size_t *argvlen) {
    hiredis::happ::cmd_exec *cmd;
    if (NULL == fn) {
        cmd = clu.exec(ks, kl, NULL, NULL, argc, argv, argvlen);
    } else {
        // 异步数据
        db_async_data_t req;
        req.bus_id    = pd;
        req.task_id   = task_id;
        req.unpack_fn = fn;
        req.response  = NULL;
        req.sequence  = allocate_sequence();

        // 防止异步调用转同步调用，预先使用栈上的DBAsyncData
        cmd = clu.exec(ks, kl, cluster_request_callback, &req, argc, argv, argvlen);

        // 这里之后异步数据不再保存在栈上，放到cmd里
        if (NULL != cmd) {
            memcpy(cmd->buffer(), &req, sizeof(db_async_data_t));
            cmd->private_data(cmd->buffer());
        }
    }

    if (NULL == cmd) {
        WLOGERROR("send db msg failed");
        return hello::err::EN_DB_SEND_FAILED;
    }

    return hello::err::EN_SUCCESS;
}

// raw
int db_msg_dispatcher::raw_init(const std::vector<logic_config::LC_DBCONN> &conns, int index) {
    if (index >= channel_t::RAW_BOUND || index < channel_t::RAW_DEFAULT) {
        return hello::err::EN_SYS_PARAM;
    }

    std::shared_ptr<hiredis::happ::raw> &conn = db_raw_conns_[index - channel_t::RAW_DEFAULT];
    if (conn) {
        conn->reset();
    }
    conn.reset();

    if (conns.empty()) {
        return hello::err::EN_SUCCESS;
    }

    conn = std::make_shared<hiredis::happ::raw>();

    // 初始化- raw入口唯一
    conn->init(conns[0].host, conns[0].port);

    // 设置日志handle
    {
        hiredis::happ::raw::log_fn_t info_fn  = db_msg_dispatcher::log_info_fn;
        hiredis::happ::raw::log_fn_t debug_fn = db_msg_dispatcher::log_debug_fn;

        util::log::log_wrapper *wrapper = WLOG_GETCAT(log_categorize_t::DB);
        if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
            debug_fn = NULL;
        }

        if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_INFO)) {
            info_fn = NULL;
        }

        conn->set_log_writer(info_fn, debug_fn);
    }

    // 设置连接成功注入login脚本和user脚本
    conn->set_on_connect(db_msg_dispatcher::raw_on_connect);
    conn->set_on_connected(db_msg_dispatcher::raw_on_connected);

    conn->set_timeout(logic_config::me()->get_cfg_db().timeout);
    conn->set_timer_interval(logic_config::me()->get_cfg_db().time_retry_sec, logic_config::me()->get_cfg_db().time_retry_usec);

    conn->set_cmd_buffer_size(sizeof(db_async_data_t));

    // 启动raw
    if (conn->start() >= 0) {
        return 0;
    }

    return hello::err::EN_SYS_INIT;
}
void db_msg_dispatcher::raw_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r, void *privdata) {
    redisReply *     reply = reinterpret_cast<redisReply *>(r);
    db_async_data_t *req   = reinterpret_cast<db_async_data_t *>(privdata);

    // 所有的请求都应该走标准流程，出错了
    if (NULL == req) {
        WLOGERROR("all cmd should has a req data");
        return;
    }

    do {
        // 无回包,可能是连接出现问题
        if (NULL == reply) {
            if (NULL == c) {
                WLOGERROR("connect to db failed.");
            } else if (c->err) {
                WLOGERROR("db got a error response, %s", c->errstr);
            }

            break;
        }

        // 响应调度器
        req->response = reply;
        me()->dispatch(req, sizeof(db_async_data_t));

        ++me()->tick_msg_count_;
    } while (false);
}

void db_msg_dispatcher::raw_on_connect(hiredis::happ::raw *c, hiredis::happ::connection *conn) {
    assert(conn);

    // 加入事件池
    redisLibuvAttach(conn->get_context(), uv_default_loop());
}

void db_msg_dispatcher::raw_on_connected(hiredis::happ::raw *raw_conn, hiredis::happ::connection *conn, const struct redisAsyncContext *, int status) {
    if (0 != status || NULL == conn) {
        WLOGERROR("connect to db host %s failed, status: %d", (NULL == conn ? "Unknown" : conn->get_key().name.c_str()), status);
        return;
    }

    WLOGINFO("connect to db host %s success", conn->get_key().name.c_str());

    for (int i = channel_t::RAW_DEFAULT; i < channel_t::RAW_BOUND; ++i) {
        std::shared_ptr<hiredis::happ::raw> &raw_ptr = me()->db_raw_conns_[i - channel_t::RAW_DEFAULT];
        if (raw_conn && raw_ptr.get() == raw_conn) {
            auto &ucbk = me()->user_callback_onconnected_[i];
            for (auto &cb : ucbk) {
                if (cb) cb();
            }
        }
    }
}

int db_msg_dispatcher::raw_send_msg(hiredis::happ::raw &raw_conn, uint64_t task_id, uint64_t pd, unpack_fn_t fn, uint64_t& sequence, 
    int argc, const char **argv, const size_t *argvlen) {
    hiredis::happ::cmd_exec *cmd;
    if (NULL == fn) {
        cmd = raw_conn.exec(NULL, NULL, argc, argv, argvlen);
    } else {
        // 异步数据
        db_async_data_t req;
        req.bus_id    = pd;
        req.task_id   = task_id;
        req.unpack_fn = fn;
        req.response  = NULL;
        req.sequence  = allocate_sequence();

        // 防止异步调用转同步调用，预先使用栈上的DBAsyncData
        cmd = raw_conn.exec(raw_request_callback, &req, argc, argv, argvlen);

        // 这里之后异步数据不再保存在栈上，放到cmd里
        if (NULL != cmd) {
            memcpy(cmd->buffer(), &req, sizeof(db_async_data_t));
            cmd->private_data(cmd->buffer());
        }
    }

    if (NULL == cmd) {
        WLOGERROR("send db msg failed");
        return hello::err::EN_DB_SEND_FAILED;
    }

    return hello::err::EN_SUCCESS;
}

uint64_t db_msg_dispatcher::allocate_sequence() {
    return ++ sequence_allocator_;
}
