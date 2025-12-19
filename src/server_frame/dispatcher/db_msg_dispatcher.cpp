// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4005)
#endif

#include <hiredis_happ.h>

#if defined(HOREDIS_HAPP_LIBHIREDIS_USING_SRC) && HOREDIS_HAPP_LIBHIREDIS_USING_SRC
#  include <adapters/libuv.h>
#else
#  include <hiredis/adapters/libuv.h>
#endif

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <config/compiler_features.h>
#include <config/extern_log_categorize.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>
#include <cstdlib>
#include <cstring>

#include <utility/random_engine.h>

#include <config/logic_config.h>
#include "db_msg_dispatcher.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <rpc/rpc_utils.h>

struct db_async_data_t {
  uint64_t task_id;
  uint64_t node_id;
  uint64_t sequence;

  redisReply *response;
  db_msg_dispatcher::unpack_fn_t unpack_fn;
};

static void _uv_close_and_free_callback(uv_handle_t *handle) { delete (uv_timer_t *)handle; }

#if defined(UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT) && UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT
#  include <type_traits>
static_assert(std::is_trivial<db_async_data_t>::value,
              "db_async_data_t must be a trivial, because it will stored in a "
              "buffer and will not call dtor fn");
#endif

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(db_msg_dispatcher);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(db_msg_dispatcher);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(db_msg_dispatcher);
#endif

SERVER_FRAME_API db_msg_dispatcher::db_msg_dispatcher()
    : sequence_allocator_(0), tick_timer_(nullptr), tick_msg_count_(0) {}

SERVER_FRAME_API db_msg_dispatcher::~db_msg_dispatcher() {
  if (nullptr != tick_timer_) {
    uv_timer_stop(tick_timer_);
    uv_close((uv_handle_t *)tick_timer_, _uv_close_and_free_callback);
    tick_timer_ = nullptr;
  }
}

SERVER_FRAME_API int32_t db_msg_dispatcher::init() {
  uv_loop_t *loop = uv_default_loop();

  if (nullptr == tick_timer_) {
    tick_timer_ = new (std::nothrow) uv_timer_t();
    if (nullptr == tick_timer_) {
      FWLOGERROR("malloc {} failed", "timer");
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }
    tick_timer_->data = this;

    int res = 0;
    do {
      res = uv_timer_init(loop, tick_timer_);
      if (0 != res) {
        FWLOGERROR("init db dispatcher timer failed, res: {}({})", res, uv_strerror(res));
        break;
      }

      // load proc interval from configure
      uint64_t timer_tick_interval =
          static_cast<uint64_t>(logic_config::me()->get_cfg_db().timer().proc().seconds() * 1000 +
                                logic_config::me()->get_cfg_db().timer().proc().nanos() / 1000000);
      res = uv_timer_start(tick_timer_, db_msg_dispatcher::on_timer_proc, timer_tick_interval, timer_tick_interval);
      if (0 != res) {
        FWLOGERROR("start db dispatcher timer failed, res: {}({})", res, uv_strerror(res));
        break;
      }
    } while (false);

    if (0 != res) {
      delete tick_timer_;
      tick_timer_ = nullptr;
    }
  }

  // init
  cluster_init(logic_config::me()->get_cfg_db().cluster(), channel_t::CLUSTER_DEFAULT);
  raw_init(logic_config::me()->get_cfg_db().raw(), channel_t::RAW_DEFAULT);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

SERVER_FRAME_API const char *db_msg_dispatcher::name() const { return "db_msg_dispatcher"; }

SERVER_FRAME_API int db_msg_dispatcher::tick() {
  tick_msg_count_ = 0;
  int prev_count = -1;

  // no more than 64 messages in one tick
  while (prev_count != tick_msg_count_ && prev_count < 64) {
    prev_count = tick_msg_count_;
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  }

  return tick_msg_count_;
}

SERVER_FRAME_API int32_t db_msg_dispatcher::dispatch(const void *msg_buf, size_t msg_buf_sz) {
  assert(msg_buf_sz == sizeof(db_async_data_t));
  const db_async_data_t *req = reinterpret_cast<const db_async_data_t *>(msg_buf);

  rpc::context ctx{rpc::context::create_without_task()};
  rpc::context *ctx_ptr = nullptr;

  if (0 != req->task_id) {
    // Try to reuse context in task
    task_type_trait::task_type task = task_manager::me()->get_task(req->task_id);
    if (!task_type_trait::empty(task)) {
      ctx_ptr = task_manager::get_shared_context(task);
    }
  }

  if (nullptr == ctx_ptr) {
    ctx_ptr = &ctx;
  }

  PROJECT_NAMESPACE_ID::table_all_message *table_msg = ctx_ptr->create<PROJECT_NAMESPACE_ID::table_all_message>();
  if (nullptr == table_msg) {
    FWLOGERROR("{} create message instance failed", name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  if (nullptr == req->response) {
    FWLOGERROR("task [{}] DB msg, no response found", req->task_id);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  switch (req->response->type) {
    case REDIS_REPLY_STATUS: {
      if (0 == UTIL_STRFUNC_STRNCASE_CMP("OK", req->response->str, 2)) {
        FWLOGINFO("db reply status: {}", req->response->str);
      } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("CAS_FAILED", req->response->str, 10)) {
        FWLOGINFO("db reply status: {}", req->response->str);
        if (req->response->str[10] && req->response->str[11]) {
          table_msg->set_version(atfw::util::string::to_int<uint64_t>(&req->response->str[11]));
        }
        ret = PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION;
      } else {
        table_msg->set_version(atfw::util::string::to_int<uint64_t>(req->response->str));
        ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      }
      break;
    }
    case REDIS_REPLY_ERROR: {
      if (0 == UTIL_STRFUNC_STRNCASE_CMP("CAS_FAILED", req->response->str, 10)) {
        if (req->response->str[10] && req->response->str[11]) {
          table_msg->set_version(atfw::util::string::to_int<uint64_t>(&req->response->str[11]));
        }
        ret = PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION;
      } else {
        FWLOGERROR("db reply error: {}", req->response->str);
        ret = PROJECT_NAMESPACE_ID::err::EN_DB_REPLY_ERROR;
      }
      break;
    }
    default: {
      if (nullptr != req->unpack_fn) {
        ret = req->unpack_fn(*table_msg, req->response);
        if (ret < 0) {
          FWLOGERROR("db unpack data error, res: {}", ret);
        }
      } else if (REDIS_REPLY_STRING == req->response->type) {
        FWLOGINFO("db reply msg: {}", req->response->str);
      }
      break;
    }
  }

  table_msg->set_node_id(req->node_id);
  table_msg->set_destination_task_id(req->task_id);
  table_msg->set_error_code(ret);

  dispatcher_raw_message callback_msg = dispatcher_make_default<dispatcher_raw_message>();
  callback_msg.msg_addr = table_msg;
  callback_msg.message_type = get_instance_ident();

  dispatcher_result_t res = on_receive_message(*ctx_ptr, callback_msg, req->response, req->sequence);
  ret = res.result_code;
  return ret;
}

SERVER_FRAME_API uint64_t db_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
  PROJECT_NAMESPACE_ID::table_all_message *real_msg =
      get_protobuf_msg<PROJECT_NAMESPACE_ID::table_all_message>(raw_msg);
  if (nullptr == real_msg) {
    return 0;
  }

  return real_msg->destination_task_id();
}

SERVER_FRAME_API const std::string &db_msg_dispatcher::pick_rpc_name(msg_raw_t &) { return get_empty_string(); }

int db_msg_dispatcher::send_msg(channel_t::type t, const char *ks, size_t kl, uint64_t task_id, uint64_t pd,
                                unpack_fn_t fn, uint64_t &sequence, int argc, const char **argv,
                                const size_t *argvlen) {
  if (t > channel_t::CLUSTER_BOUND && t < channel_t::SENTINEL_BOUND) {
    if (db_cluster_conns_[t]) {
      return cluster_send_msg(*db_cluster_conns_[t], ks, kl, task_id, pd, fn, sequence, argc, argv, argvlen);
    } else {
      FWLOGERROR("db cluster {} not inited", static_cast<int>(t));
      return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
    }
  }

  if (t >= channel_t::RAW_DEFAULT && t < channel_t::RAW_BOUND) {
    if (db_raw_conns_[t - channel_t::RAW_DEFAULT]) {
      return raw_send_msg(*db_raw_conns_[t - channel_t::RAW_DEFAULT], task_id, pd, fn, sequence, argc, argv, argvlen);
    } else {
      FWLOGERROR("db single {} not inited", static_cast<int>(t));
      return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
    }
  }

  FWLOGERROR("db channel {} invalid", static_cast<int>(t));
  return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
}

SERVER_FRAME_API void *db_msg_dispatcher::get_cache_buffer(size_t len) {
  if (pack_cache_.size() < len) {
    size_t sz = 1;
    while (sz < len) {
      sz <<= 1;
    }
    pack_cache_.resize(sz);
  }

  return &pack_cache_[0];
}

SERVER_FRAME_API bool db_msg_dispatcher::is_available(channel_t::type t) const {
  if (t >= channel_t::RAW_DEFAULT && t < channel_t::RAW_BOUND) {
    return !!db_raw_conns_[t - channel_t::RAW_DEFAULT];
  } else {
    return !!db_cluster_conns_[t];
  }
}

SERVER_FRAME_API const std::string &db_msg_dispatcher::get_db_script_sha1(script_type type) const {
  if (type >= script_type::kMax) {
    return db_script_sha1_[0];
  }

  return db_script_sha1_[static_cast<size_t>(type)];
}

SERVER_FRAME_API void db_msg_dispatcher::set_db_script_sha1(script_type type, const char *str, int len) {
  if (type >= script_type::kMax || type <= script_type::kInvalid || len <= 0) {
    return;
  }

  db_script_sha1_[static_cast<size_t>(type)].assign(str, static_cast<size_t>(len));
}

SERVER_FRAME_API void db_msg_dispatcher::set_on_connected(channel_t::type t, user_callback_t fn) {
  if (t >= channel_t::MAX || t < 0) {
    return;
  }

  user_callback_onconnected_[t].push_back(fn);
}

void db_msg_dispatcher::log_debug_fn(const char *content) { WCLOGDEBUG(log_categorize_t::DB, "%s", content); }

void db_msg_dispatcher::log_info_fn(const char *content) { WCLOGINFO(log_categorize_t::DB, "%s", content); }

int db_msg_dispatcher::script_load(redisAsyncContext *c, script_type type) {
  // load lua script
  int status;
  std::string script;
  std::stringstream script_stream;
  switch (type) {
    case script_type::kCompareAndSetHashTable: {
      script_stream << "local real_version_str = redis.call('HGET', KEYS[1], ARGV[1])\n";
      script_stream << "local real_version = 0\n";
      script_stream << "if real_version_str != false and real_version_str != nil then\n";
      script_stream << "  real_version = tonumber(real_version_str)\n";
      script_stream << "end\n";
      script_stream << "local except_version = tonumber(ARGV[2])\n";
      script_stream << "local unpack_fn = table.unpack or unpack -- Lua 5.1 - 5.3\n";
      script_stream << "if real_version == 0 or except_version == real_version then\n";
      script_stream << "  ARGV[2] = real_version + 1;\n";
      script_stream << "  redis.call('HMSET', KEYS[1], unpack_fn(ARGV))\n";
      script_stream << "  return  { ok = tostring(ARGV[2]) }\n";
      script_stream << "else\n";
      script_stream << "  return  { err = 'CAS_FAILED|' .. tostring(real_version) }\n";
      script_stream << "end\n";
      break;
    }
    default:
      break;
  }

  script = script_stream.str();
  if (script.empty()) {
    return 0;
  }

  status = redisAsyncCommand(c, script_callback, reinterpret_cast<void *>(static_cast<intptr_t>(type)),
                             "SCRIPT LOAD %s", script.c_str());
  if (REDIS_OK != status) {
    FWLOGERROR("send db msg failed, status: {}, msg: {}", status, c->errstr);
  }

  return status;
}

void db_msg_dispatcher::on_timer_proc(uv_timer_t *handle) {
  time_t sec = atfw::util::time::time_utility::get_now();
  time_t usec = atfw::util::time::time_utility::get_now_usec();

  db_msg_dispatcher *dispatcher = reinterpret_cast<db_msg_dispatcher *>(handle->data);
  assert(dispatcher);
  for (std::shared_ptr<hiredis::happ::cluster> &clu : dispatcher->db_cluster_conns_) {
    if (!clu) {
      continue;
    }

    clu->proc(sec, usec);
  }

  for (std::shared_ptr<hiredis::happ::raw> &raw_conn : dispatcher->db_raw_conns_) {
    if (!raw_conn) {
      continue;
    }

    raw_conn->proc(sec, usec);
  }
}

void db_msg_dispatcher::script_callback(redisAsyncContext *c, void *r, void *privdata) {
  redisReply *reply = reinterpret_cast<redisReply *>(r);

  if (reply && reply->type == REDIS_REPLY_STRING && reply->str) {
    FWLOGDEBUG("db script reply: {}", reply->str);
    (me()->set_db_script_sha1(static_cast<script_type>(reinterpret_cast<intptr_t>(privdata)), reply->str,
                              static_cast<int>(reply->len)));

  } else if (c->err) {
    FWLOGERROR("db got a error response, {}", c->errstr);
  }

  // 响应调度器
  ++me()->tick_msg_count_;
}

// cluster
int db_msg_dispatcher::cluster_init(const PROJECT_NAMESPACE_ID::config::db_group_cfg &conns, int index) {
  if (index >= channel_t::SENTINEL_BOUND || index < 0) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  std::shared_ptr<hiredis::happ::cluster> &conn = db_cluster_conns_[index];
  if (conn) {
    conn->reset();
  }
  conn.reset();

  if (0 == conns.gateways_size()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  conn = atfw::memory::stl::make_shared<hiredis::happ::cluster>();
  int32_t conn_idx = atfw::util::random_engine::random_between<int32_t>(0, conns.gateways_size());

  // 初始化
  conn->init(conns.gateways(conn_idx).host(), static_cast<uint16_t>(conns.gateways(conn_idx).port()));

  // 设置日志handle
  {
    hiredis::happ::cluster::log_fn_t info_fn = db_msg_dispatcher::log_info_fn;
    hiredis::happ::cluster::log_fn_t debug_fn = db_msg_dispatcher::log_debug_fn;

    atfw::util::log::log_wrapper *wrapper = WLOG_GETCAT(log_categorize_t::DB);
    if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
      debug_fn = nullptr;
    }

    if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_INFO)) {
      info_fn = nullptr;
    }

    conn->set_log_writer(info_fn, debug_fn);
  }

  // 设置连接成功注入login脚本和user脚本
  conn->set_on_connect(db_msg_dispatcher::cluster_on_connect);
  conn->set_on_connected(db_msg_dispatcher::cluster_on_connected);

  conn->set_timeout(logic_config::me()->get_cfg_db().timer().timeout().seconds());
  conn->set_timer_interval(logic_config::me()->get_cfg_db().timer().retry().seconds(),
                           logic_config::me()->get_cfg_db().timer().retry().nanos() / 1000);

  conn->set_cmd_buffer_size(sizeof(db_async_data_t));

  // 启动cluster
  if (conn->start() >= 0) {
    return 0;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
}

void db_msg_dispatcher::cluster_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r,
                                                 void *privdata) {
  redisReply *reply = reinterpret_cast<redisReply *>(r);
  db_async_data_t *req = reinterpret_cast<db_async_data_t *>(privdata);

  // 所有的请求都应该走标准流程，出错了
  if (nullptr == req) {
    FWLOGERROR("{}", "all cmd should has a req data");
    return;
  }

  do {
    // 无回包,可能是连接出现问题
    if (nullptr == reply) {
      if (nullptr == c) {
        FWLOGERROR("{}", "connect to db failed.");
      } else if (c->err) {
        FWLOGERROR("db got a error response, {}", c->errstr);
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

void db_msg_dispatcher::cluster_on_connected(hiredis::happ::cluster *clu, hiredis::happ::connection *conn,
                                             const struct redisAsyncContext *, int status) {
  if (0 != status || nullptr == conn) {
    FWLOGERROR("connect to db host {} failed, status: {}", (nullptr == conn ? "Unknown" : conn->get_key().name.c_str()),
               status);
    return;
  }

  FWLOGINFO("connect to db host {} success", conn->get_key().name);
  // 注入redis的lua脚本
  me()->script_load(conn->get_context(), script_type::kCompareAndSetHashTable);

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

int db_msg_dispatcher::cluster_send_msg(hiredis::happ::cluster &clu, const char *ks, size_t kl, uint64_t task_id,
                                        uint64_t pd, unpack_fn_t fn, uint64_t &sequence, int argc, const char **argv,
                                        const size_t *argvlen) {
  hiredis::happ::cmd_exec *cmd;
  if (nullptr == fn) {
    cmd = clu.exec(ks, kl, nullptr, nullptr, argc, argv, argvlen);
  } else {
    // 异步数据
    db_async_data_t req;
    req.node_id = pd;
    req.task_id = task_id;
    req.unpack_fn = fn;
    req.response = nullptr;
    req.sequence = allocate_sequence();
    sequence = req.sequence;

    // 防止异步调用转同步调用，预先使用栈上的DBAsyncData
    cmd = clu.exec(ks, kl, cluster_request_callback, &req, argc, argv, argvlen);

    // 这里之后异步数据不再保存在栈上，放到cmd里
    if (nullptr != cmd) {
      memcpy(cmd->buffer(), &req, sizeof(db_async_data_t));
      cmd->private_data(cmd->buffer());
    }
  }

  if (nullptr == cmd) {
    FWLOGERROR("{}", "send db msg failed");
    return PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

// raw
int db_msg_dispatcher::raw_init(const PROJECT_NAMESPACE_ID::config::db_group_cfg &conns, int index) {
  if (index >= channel_t::RAW_BOUND || index < channel_t::RAW_DEFAULT) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  std::shared_ptr<hiredis::happ::raw> &conn = db_raw_conns_[index - channel_t::RAW_DEFAULT];
  if (conn) {
    conn->reset();
  }
  conn.reset();

  if (0 == conns.gateways_size()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  conn = atfw::memory::stl::make_shared<hiredis::happ::raw>();

  // 初始化- raw入口唯一
  conn->init(conns.gateways(0).host(), static_cast<uint16_t>(conns.gateways(0).port()));

  // 设置日志handle
  {
    hiredis::happ::raw::log_fn_t info_fn = db_msg_dispatcher::log_info_fn;
    hiredis::happ::raw::log_fn_t debug_fn = db_msg_dispatcher::log_debug_fn;

    atfw::util::log::log_wrapper *wrapper = WLOG_GETCAT(log_categorize_t::DB);
    if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
      debug_fn = nullptr;
    }

    if (!wrapper->check_level(util::log::log_wrapper::level_t::LOG_LW_INFO)) {
      info_fn = nullptr;
    }

    conn->set_log_writer(info_fn, debug_fn);
  }

  // 设置连接成功注入login脚本和user脚本
  conn->set_on_connect(db_msg_dispatcher::raw_on_connect);
  conn->set_on_connected(db_msg_dispatcher::raw_on_connected);

  conn->set_timeout(logic_config::me()->get_cfg_db().timer().timeout().seconds());
  conn->set_timer_interval(logic_config::me()->get_cfg_db().timer().retry().seconds(),
                           logic_config::me()->get_cfg_db().timer().retry().nanos() / 1000);

  conn->set_cmd_buffer_size(sizeof(db_async_data_t));

  // 启动raw
  if (conn->start() >= 0) {
    return 0;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
}
void db_msg_dispatcher::raw_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r,
                                             void *privdata) {
  redisReply *reply = reinterpret_cast<redisReply *>(r);
  db_async_data_t *req = reinterpret_cast<db_async_data_t *>(privdata);

  // 所有的请求都应该走标准流程，出错了
  if (nullptr == req) {
    FWLOGERROR("{}", "all cmd should has a req data");
    return;
  }

  do {
    // 无回包,可能是连接出现问题
    if (nullptr == reply) {
      if (nullptr == c) {
        FWLOGERROR("{}", "connect to db failed.");
      } else if (c->err) {
        FWLOGERROR("db got a error response, {}", c->errstr);
      }

      break;
    }

    // 响应调度器
    req->response = reply;
    me()->dispatch(req, sizeof(db_async_data_t));

    ++me()->tick_msg_count_;
  } while (false);
}

void db_msg_dispatcher::raw_on_connect(hiredis::happ::raw *, hiredis::happ::connection *conn) {
  assert(conn);

  // 加入事件池
  redisLibuvAttach(conn->get_context(), uv_default_loop());
}

void db_msg_dispatcher::raw_on_connected(hiredis::happ::raw *raw_conn, hiredis::happ::connection *conn,
                                         const struct redisAsyncContext *, int status) {
  if (0 != status || nullptr == conn) {
    FWLOGERROR("connect to db host {} failed, status: {}", (nullptr == conn ? "Unknown" : conn->get_key().name.c_str()),
               status);
    return;
  }

  FWLOGINFO("connect to db host {} success", conn->get_key().name);

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

int db_msg_dispatcher::raw_send_msg(hiredis::happ::raw &raw_conn, uint64_t task_id, uint64_t pd, unpack_fn_t fn,
                                    uint64_t &sequence, int argc, const char **argv, const size_t *argvlen) {
  hiredis::happ::cmd_exec *cmd;
  if (nullptr == fn) {
    cmd = raw_conn.exec(nullptr, nullptr, argc, argv, argvlen);
  } else {
    // 异步数据
    db_async_data_t req;
    req.node_id = pd;
    req.task_id = task_id;
    req.unpack_fn = fn;
    req.response = nullptr;
    req.sequence = allocate_sequence();
    sequence = req.sequence;

    // 防止异步调用转同步调用，预先使用栈上的DBAsyncData
    cmd = raw_conn.exec(raw_request_callback, &req, argc, argv, argvlen);

    // 这里之后异步数据不再保存在栈上，放到cmd里
    if (nullptr != cmd) {
      memcpy(cmd->buffer(), &req, sizeof(db_async_data_t));
      cmd->private_data(cmd->buffer());
    }
  }

  if (nullptr == cmd) {
    FWLOGERROR("send db msg failed");
    return PROJECT_NAMESPACE_ID::err::EN_DB_SEND_FAILED;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

SERVER_FRAME_API uint64_t db_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }
