// Copyright 2021 atframework
// Created by owent on 2016-10-09.
//

#include "rpc/db/uuid.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.global.table.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/compiler_features.h>

#include <algorithm/murmur_hash.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <random/uuid_generator.h>
#include <time/time_utility.h>

#include <lock/lock_holder.h>
#include <lock/seq_alloc.h>
#include <lock/spin_rw_lock.h>

#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include <stdint.h>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

#include "rpc/db/db_utils.h"
#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_utils.h"

namespace rpc {
namespace db {
namespace uuid {
namespace detail {
struct short_uuid_encoder {
  constexpr static const size_t kKeyLength = 36;
  short_uuid_encoder() {
    // 有些外部平台不区分大小写，为了通用改成不区分大小写的混淆表
    // memcpy(keys, "M7Vy1DQnIj93B2kNPJCRxuoTYhvSpOgstKaZ0lrH8WmGdcXLbzeqwUE5F4i6Af", 62);
    memcpy(keys, "y102a3gq58zrjbovpm7w6ltiuesf9h4kxncd", kKeyLength);
  }

  ::util::lock::seq_alloc_u32 seq_;
  char keys[kKeyLength];
  size_t operator()(char *in, size_t insz, uint64_t val) {
    if (insz == 0 || NULL == in) {
      return 0;
    }

    size_t ret;
    for (ret = 1; val > 0 && ret < insz; ++ret) {
      in[ret] = keys[val % kKeyLength];
      val /= kKeyLength;
    }

    if (ret < kKeyLength) {
      in[0] = keys[ret];
    } else {
      in[0] = keys[kKeyLength - 1];
    }

    return ret;
  }

  size_t operator()(char *in, size_t insz) {
    uint32_t v = seq_.inc();
    if (0 == v) {
      v = seq_.inc();
    }

    return (*this)(in, insz, v);
  }
};
static short_uuid_encoder short_uuid_encoder_;
}  // namespace detail

std::string generate_standard_uuid(bool remove_minus) {
  return util::random::uuid_generator::generate_string_time(remove_minus);
}

std::string generate_standard_uuid_binary() {
  return util::random::uuid_generator::uuid_to_binary(util::random::uuid_generator::generate_time());
}

std::string generate_short_uuid() {
  // bus_id:(timestamp-2022-01-01 00:00:00):sequence
  // 2022-01-01 00:00:00 UTC => 1640995200
  uint64_t bus_id = logic_config::me()->get_local_server_id();
  time_t time_param = util::time::time_utility::get_now() - 1640995200;

  // 第一个字符用S，表示服务器生成，这样如果客户端生成的用C开头，就不会和服务器冲突
  // 第二个字符表示版本号，以便后续变更算法可以和之前区分开来
  char bin_buffer[64] = {'S', '1', 0};
  size_t start_index = 2;
  start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1, bus_id);
  start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1,
                                             time_param > 0 ? static_cast<uint64_t>(time_param) : 0);
  start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1);
  bin_buffer[start_index] = 0;

  return bin_buffer;
}

rpc_result<int64_t> generate_global_increase_id(rpc::context &ctx, uint32_t major_type, uint32_t minor_type,
                                                uint32_t patch_type) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  // 这个算法比许固定
  char keyvar[64];
  size_t keylen = sizeof(keyvar) - 1;
  int __snprintf_writen_length =
      UTIL_STRFUNC_SNPRINTF(keyvar, static_cast<int>(keylen), "guid:%x-%x-%x", major_type, minor_type, patch_type);
  if (__snprintf_writen_length < 0) {
    keyvar[sizeof(keyvar) - 1] = '\0';
    keylen = 0;
  } else {
    keylen = static_cast<size_t>(__snprintf_writen_length);
    keyvar[__snprintf_writen_length] = '\0';
  }

  rpc::db::redis_args args(2);
  {
    args.push("INCR");
    args.push(keyvar);
  }

  rpc::context child_ctx(ctx);
  rpc::context::tracer tracer;
  rpc::context::trace_option trace_option;
  trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(db_msg_dispatcher::me());
  trace_option.is_remote = true;
  trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  child_ctx.setup_tracer(tracer, "rpc.db.uuid.generate_global_increase_id", std::move(trace_option));

  uint64_t rpc_sequence = 0;
  int res = db_msg_dispatcher::me()->send_msg(
      db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, keyvar, keylen, child_ctx.get_task_context().task_id,
      logic_config::me()->get_local_server_id(), rpc::db::detail::unpack_integer, rpc_sequence,
      static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

  if (res < 0) {
    RPC_RETURN_TYPE(tracer.return_code(res));
  }

  PROJECT_NAMESPACE_ID::table_all_message msg;
  // 协程操作
  dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
  await_options.sequence = rpc_sequence;
  await_options.timeout =
      rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(), std::chrono::seconds{6});

  res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, msg, await_options));
  if (res < 0) {
    RPC_RETURN_TYPE(tracer.return_code(res));
  }

  if (!msg.has_simple()) {
    RPC_RETURN_TYPE(tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND));
  }

  RPC_RETURN_TYPE(tracer.return_code(static_cast<int>(msg.simple().msg_i64())));
}

struct unique_id_key_t {
  uint32_t major_type;
  uint32_t minor_type;
  uint32_t patch_type;

  friend bool operator==(const unique_id_key_t &l, const unique_id_key_t &r) noexcept {
    return l.major_type == r.major_type && l.minor_type == r.minor_type && l.patch_type == r.patch_type;
  }

  friend bool operator<(const unique_id_key_t &l, const unique_id_key_t &r) noexcept {
    if (l.major_type != r.major_type) {
      return l.major_type < r.major_type;
    }

    if (l.minor_type != r.minor_type) {
      return l.minor_type < r.minor_type;
    }

    return l.patch_type < r.patch_type;
  }

  friend bool operator<=(const unique_id_key_t &l, const unique_id_key_t &r) noexcept { return l == r || l < r; }
};

struct unique_id_value_t {
  task_type_trait::task_type alloc_task;
  util::lock::atomic_int_type<int64_t> unique_id_index;
  util::lock::atomic_int_type<int64_t> unique_id_base;
  std::list<task_type_trait::task_type> wake_tasks;

  unique_id_value_t() noexcept
      :
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
        alloc_task{},
#else
        alloc_task{nullptr},
#endif
        unique_id_index{0},
        unique_id_base{0} {
  }

  unique_id_value_t(unique_id_value_t &&other) noexcept
      : alloc_task{std::move(other.alloc_task)},
        unique_id_index{other.unique_id_index.load()},
        unique_id_base{other.unique_id_base.load()},
        wake_tasks{std::move(other.wake_tasks)} {}
};

struct unique_id_container_helper {
  std::size_t operator()(unique_id_key_t const &v) const noexcept {
    uint32_t data[3] = {v.major_type, v.minor_type, v.patch_type};
    uint64_t out[2];
    util::hash::murmur_hash3_x64_128(data, sizeof(data), 0, out);
    return static_cast<std::size_t>(out[0]);
  }
};

static std::unordered_map<unique_id_key_t, unique_id_value_t, unique_id_container_helper> g_unique_id_pools;
static util::lock::spin_rw_lock g_unique_id_pool_locker;

struct unique_id_container_waker {
  unique_id_key_t key;
  inline explicit unique_id_container_waker(unique_id_key_t k) : key(k) {}

  void operator()() const {
    using real_map_type = std::unordered_map<unique_id_key_t, unique_id_value_t, unique_id_container_helper>;
    real_map_type::iterator iter;

    {
      util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard(g_unique_id_pool_locker);
      iter = g_unique_id_pools.find(key);
      if (iter == g_unique_id_pools.end()) {
        return;
      }
    }

    while (!iter->second.wake_tasks.empty()) {
      if (!task_type_trait::empty(iter->second.alloc_task) && !task_type_trait::is_exiting(iter->second.alloc_task)) {
        break;
      }

      auto wake_task = *iter->second.wake_tasks.begin();
      if (!task_type_trait::empty(wake_task) && !task_type_trait::is_exiting(wake_task)) {
        // iter will be erased in task
        dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
        callback_data.message.msg_type = reinterpret_cast<uintptr_t>(reinterpret_cast<const void *>(&iter->second));
        callback_data.sequence = task_type_trait::get_task_id(wake_task);

        rpc::custom_resume(wake_task, callback_data);
      } else {
        // This should not be called
        if (!task_type_trait::empty(wake_task)) {
          FWLOGERROR("Wake iterator of task {} should be removed by task action",
                     task_type_trait::get_task_id(wake_task));
        }
        iter->second.wake_tasks.pop_front();
      }
    }
  }

  static rpc::result_void_type insert_into_pool(rpc::context &ctx, unique_id_value_t &pool,
                                                task_type_trait::task_type task) {
    // Append to wake list and then custom_wait to switch out
    auto iter = pool.wake_tasks.emplace(pool.wake_tasks.end(), std::move(task));

    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = task_type_trait::get_task_id(task);
    await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                                          std::chrono::seconds{6});

    RPC_AWAIT_IGNORE_RESULT(rpc::custom_wait(ctx, reinterpret_cast<const void *>(&pool), nullptr, await_options));
    pool.wake_tasks.erase(iter);

    RPC_RETURN_VOID;
  }
};

template <int64_t bits_off>
static rpc::rpc_result<int64_t> generate_global_unique_id(rpc::context &ctx, uint32_t major_type, uint32_t minor_type,
                                                          uint32_t patch_type) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

  // POOL => 1 | 50 | 13
  // constexpr int64_t bits_off   = 13;
  constexpr int64_t bits_range = 1 << bits_off;
  constexpr int64_t bits_mask = bits_range - 1;

  unique_id_key_t key;
  key.major_type = major_type;
  key.minor_type = minor_type;
  key.patch_type = patch_type;

  unique_id_value_t *alloc;
  do {
    using real_map_type = std::unordered_map<unique_id_key_t, unique_id_value_t, unique_id_container_helper>;
    real_map_type::iterator iter;

    {
      util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard(g_unique_id_pool_locker);
      iter = g_unique_id_pools.find(key);
      if (g_unique_id_pools.end() != iter) {
        alloc = &iter->second;
        break;
      }
    }

    util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard(g_unique_id_pool_locker);
    iter = g_unique_id_pools.insert(real_map_type::value_type(key, unique_id_value_t{})).first;

    if (g_unique_id_pools.end() == iter) {
      RPC_RETURN_TYPE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }

    alloc = &iter->second;
  } while (false);

  int64_t ret = 0;
  int try_left = 5;
  bool should_wake_key = false;
  bool has_scheduled = false;

  auto self_task = task_manager::me()->get_task(ctx.get_task_context().task_id);
  if (task_type_trait::empty(self_task)) {
    RPC_RETURN_TYPE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_NOT_FOUND);
  }

  while (try_left-- > 0 && ret <= 0) {
    // 任务已经失败或者不在任务中
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      ret = task_manager::convert_task_status_to_error_code(current_task_status);
      break;
    }

    // Queue to Allocate id pool
    if (!task_type_trait::empty(alloc->alloc_task) && !task_type_trait::is_exiting(alloc->alloc_task) &&
        alloc->alloc_task != self_task) {
      RPC_AWAIT_IGNORE_VOID(unique_id_container_waker::insert_into_pool(ctx, *alloc, self_task));
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED;
      has_scheduled = true;
      continue;
    }

    auto &unique_id_index = alloc->unique_id_index;
    auto &unique_id_base = alloc->unique_id_base;

    int64_t current_unique_id_index = unique_id_index.load();
    current_unique_id_index &= bits_mask;
    ret = (unique_id_base.load(util::lock::memory_order_acquire) << bits_off) | current_unique_id_index;

    // call rpc to allocate a id pool
    if (0 == (ret >> bits_off) || 0 == (ret & bits_mask)) {
      // Keep order here
      if (!has_scheduled && !alloc->wake_tasks.empty()) {
        RPC_AWAIT_IGNORE_VOID(unique_id_container_waker::insert_into_pool(ctx, *alloc, self_task));
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED;
        has_scheduled = true;
        continue;
      }

      alloc->alloc_task = self_task;
      int64_t res = RPC_AWAIT_TYPE_RESULT(generate_global_increase_id(ctx, major_type, minor_type, patch_type));
      // WLOGINFO("=====DEBUG===== generate uuid pool for (%u, %u, %u), val: %lld", major_type, minor_type, patch_type,
      // static_cast<long long>(res));
      if (alloc->alloc_task == self_task) {
        task_type_trait::reset_task(alloc->alloc_task);
      }
      should_wake_key = true;
      if (res <= 0) {
        ret = res;
        continue;
      }
      unique_id_base.store(res, util::lock::memory_order_release);
      unique_id_index.store(0);
    }

    ++unique_id_index;
  }

  if (should_wake_key) {
    rpc::async_then(ctx, "rpc.uuid.unique_id.waker", self_task, unique_id_container_waker(key));
  }

  // WLOGINFO("=====DEBUG===== malloc uuid for (%u, %u, %u), val: %lld", major_type, minor_type, patch_type,
  // static_cast<long long>(ret));
  if (0 == ret) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  }
  RPC_RETURN_TYPE(ret);
}

rpc_result<int64_t> generate_global_unique_id(rpc::context &ctx, uint32_t major_type, uint32_t minor_type,
                                              uint32_t patch_type) {
  if (PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID == major_type ||
      PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_GUILD_ID == major_type) {
    // POOL => 1 | * | 5
    // EN_GLOBAL_UUID_MAT_USER_ID:     [1 | 55 | 5] | 3
    // EN_GLOBAL_UUID_MAT_GUILD_ID:    [1 | 55 | 5] | 3
    // 公会和玩家账号分配采用短ID模式
    return generate_global_unique_id<5>(ctx, major_type, minor_type, patch_type);
  } else {
    // POOL => 1 | 50 | 13
    return generate_global_unique_id<13>(ctx, major_type, minor_type, patch_type);
  }
}
}  // namespace uuid
}  // namespace db
}  // namespace rpc
