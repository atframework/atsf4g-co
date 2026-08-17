// Copyright 2021 atframework
// Created by owent on 2019-10-09.
//

#pragma once

#include <config/compile_optimize.h>

#include <log/log_wrapper.h>
#include <mem_pool/lru_map.h>
#include <time/time_utility.h>

#include <dispatcher/task_action_base.h>
#include <dispatcher/task_manager.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>

#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"
#include "rpc/rpc_context.h"

namespace rpc {
class context;

template <typename TKey, typename TObject>
class ATFW_UTIL_SYMBOL_VISIBLE rpc_lru_cache_map {
 public:
  using key_type = TKey;
  using value_type = TObject;
  using self_type = rpc_lru_cache_map<TKey, TObject>;
  enum { RPC_LRU_CACHE_MAP_DEFAULT_RETRY_TIMES = 3 };  // NOLINT(cppcoreguidelines-use-enum-class)

  struct value_cache_type {
    task_type_trait::task_type io_task;
    key_type data_key;
    int64_t data_version;
    time_t last_visit_timepoint;
    value_type data_object;
    uint64_t saving_sequence;
    uint64_t saved_sequence;
    // 被 remove_cache 显式移除后置位：条目保留在池中作为墓碑，get_cache 对外不可见、await_save 拒绝写回、
    // set_cache 拒绝重新入池，防止已删除的记录被晚到的保存或完成回调复活。
    // 后续对同一 key 的 await_fetch 属于显式重新获取（先删除又获取的流程），会清除本标记并复用原对象。
    // 注意 LRU 淘汰（pop_front/pop_back/erase）不置位：淘汰不是删除，重新拉取后允许重建缓存。
    bool removed;

    explicit value_cache_type(const key_type &k)
        : data_key(k),
          data_version(0),
          last_visit_timepoint(0),
          saving_sequence(0),
          saved_sequence(0),
          removed(false) {}
    value_cache_type(const value_cache_type &) = default;
    value_cache_type(value_cache_type &&) = default;

    value_cache_type &operator=(const value_cache_type &) = default;
    value_cache_type &operator=(value_cache_type &&) = default;
  };

  using lru_map_type = atfw::util::mempool::lru_map<
      key_type, value_cache_type, std::hash<key_type>, std::equal_to<key_type>,
      atfw::util::memory::lru_map_option<atfw::util::memory::compat_strong_ptr_mode::kStrongRc>>;
  using cache_ptr_type = typename lru_map_type::store_type;
  using iterator = typename lru_map_type::iterator;
  using const_iterator = typename lru_map_type::const_iterator;
  using size_type = typename lru_map_type::size_type;

 public:
  cache_ptr_type get_cache(const key_type &key, bool update_visit = true) {
    auto iter = pool_.find(key, false);
    if (iter == pool_.end() || !iter->second || iter->second->removed) {
      // 不存在，或已被 remove_cache 移除（墓碑条目对外不可见）
      return nullptr;
    }

    cache_ptr_type ret = iter->second;
    if (update_visit) {
      ret->last_visit_timepoint = atfw::util::time::time_utility::get_now();
      // 刷新 LRU 访问序（移到 back）。墓碑条目不做提升，让其自然沉到最久未访问端优先被淘汰
      pool_.find(key, true);
    }
    return ret;
  }

  void set_cache(cache_ptr_type &cache) {
    if (!cache) {
      return;
    }

    if (cache->removed) {
      // 已被 remove_cache 显式移除的缓存不允许重新入池，防止已删除的记录被旧句柄复活
      return;
    }

    auto iter = pool_.find(cache->data_key);
    if (iter != pool_.end()) {
      if (iter->second == cache) {
        return;
      }
      pool_.erase(iter);
    }

    cache->last_visit_timepoint = atfw::util::time::time_utility::get_now();
    pool_.insert_key_value(cache->data_key, cache);
  }

  inline bool empty() const { return pool_.empty(); }

  inline iterator begin() { return pool_.begin(); }
  inline const_iterator cbegin() const { return pool_.cbegin(); }
  inline iterator end() { return pool_.end(); }
  inline const_iterator cend() const { return pool_.cend(); }
  inline const typename lru_map_type::value_type &front() const { return pool_.front(); }
  inline typename lru_map_type::value_type &front() { return pool_.front(); }
  inline const typename lru_map_type::value_type &back() const { return pool_.back(); }
  inline typename lru_map_type::value_type &back() { return pool_.back(); }
  inline void pop_front() { return pool_.pop_front(); }
  inline void pop_back() { return pool_.pop_back(); }
  inline iterator erase(iterator pos) { return pool_.erase(pos); }
  inline size_type size() const { return pool_.size(); }
  inline void reserve(size_type s) { pool_.reserve(s); }

  bool remove_cache(const key_type &key) {
    auto iter = pool_.find(key, false);
    if (iter == pool_.end() || !iter->second || iter->second->removed) {
      return false;
    }

    // 标记失效并保留为墓碑条目：get_cache 对外不可见，旧句柄不允许再通过 await_save 写回；
    // 后续 await_fetch 显式重新获取同一 key 时会清除标记并复用原对象（先删除又获取的流程）
    iter->second->removed = true;
    return true;
  }

  // 按 remove_cache 的语义清空全部条目：保留墓碑对象，阻止在途句柄写回复活。
  // 仅供单元测试在用例间清理进程级单例状态使用。
  size_type clear() {
    size_type removed = 0;
    for (auto iter = pool_.begin(); iter != pool_.end(); ++iter) {
      if (iter->second && !iter->second->removed) {
        iter->second->removed = true;
        ++removed;
      }
    }
    return removed;
  }

  // ==================== 协程接口，可能切出执行上下文 ====================
  /**
   * @brief 等待并提取拉取结果
   * @note 这个接口会自动合并多个拉取请求，并共享使用同一个LRU缓存
   * @param[in]  key  key
   * @param[out] out  输出的缓存对象指针
   * @param[in]  fn   拉取协程函数
   * @return 0或错误码
   */
  result_code_type await_fetch(
      rpc::context &ctx, const key_type &key, cache_ptr_type &out,
      std::function<result_code_type(rpc::context &ctx, const key_type &key, value_type &val_out, int64_t *out_version)>
          fn) {
    if (!fn) {
      FWLOGERROR("{} must be called with rpc function", __FUNCTION__);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

    // retry 只按真实等待次数计：每次等待后必须重新检查缓存状态，避免数据已就绪却误报超限
    int retry_times = RPC_LRU_CACHE_MAP_DEFAULT_RETRY_TIMES;
    while (true) {
      out = get_cache(key);

      // 没有可用缓存时探测 remove_cache 留下的墓碑条目：
      // 显式的重新获取表示记录重新有效，允许复用原对象并清除 removed 标记（先删除又获取的流程）
      if (nullptr == out) {
        auto tombstone_iter = pool_.find(key, false);
        if (tombstone_iter != pool_.end() && tombstone_iter->second) {
          out = tombstone_iter->second;
        }

        if (nullptr == out) {
          // 没有条目，本任务就是拉取任务
          break;
        }

        if (out->removed && task_type_trait::empty(out->io_task)) {
          // 空闲墓碑：本任务成为拉取任务，并在下方复用对象
          break;
        }
      }

      // 如果有在途 IO（拉取或保存），则排到它后面，避免读取到尚未持久化成功的临时状态。
      // 保存失败后缓存会被淘汰，continue 后会重新从 DB 拉取。
      if (!task_type_trait::empty(out->io_task)) {
        if (task_type_trait::is_exiting(out->io_task)) {
          // fallback, clear data, 理论上不会走到这个流程，前面就是reset掉
          task_type_trait::reset_task(out->io_task);
        } else {
          if (task_type_trait::get_task_id(out->io_task) == ctx.get_task_context().task_id) {
            // 重入调用：当前任务就是该缓存的 IO 任务，直接共享同一份进行中的缓存
            RPC_RETURN_CODE(0);
          }
          if (retry_times <= 0) {
            out.reset();
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED);
          }
          --retry_times;
          int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, out->io_task));
          if (res < 0) {
            out.reset();
            RPC_RETURN_CODE(res);
          }
          task_type_trait::reset_task(out->io_task);
        }
        continue;
      }

      RPC_RETURN_CODE(0);
    }

    if (nullptr == out) {
      // 尝试拉取，成功的话放进缓存
      auto res = pool_.insert_key_value(key, value_cache_type(key));
      if (!res.second) {
        out.reset();
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
      }

      out = res.first->second;
    } else {
      // 复用 remove_cache 留下的墓碑对象：先删除又获取 → 清除 removed 标记，原对象继续可用。
      // data_object 不在此清空：可能有在途保存任务仍持有其引用，拉取成功后由 fn 覆盖
      out->removed = false;
    }
    out->data_version = 0;
    out->last_visit_timepoint = atfw::util::time::time_utility::get_now();

    auto invoke_result = rpc::async_invoke(
        ctx, "rpc_lru_cache_map.await_fetch",
        [out, key, fn = std::move(fn)](rpc::context &child_ctx) -> rpc::result_code_type {
          int32_t ret = RPC_AWAIT_CODE_RESULT(fn(child_ctx, key, out->data_object, &out->data_version));

          if (task_type_trait::get_task_id(out->io_task) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(out->io_task);
          }

          RPC_RETURN_CODE(ret);
        });
    int32_t ret = 0;
    if (invoke_result.is_error()) {
      ret = *invoke_result.get_error();
    } else {
      // 拉取结束，重置拉取任务
      if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
        out->io_task = *invoke_result.get_success();
      }
      ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
      if (ret >= 0) {
        ret = task_type_trait::get_result(*invoke_result.get_success());
      }
    }

    if (0 == ret) {
      cache_ptr_type test_cache = get_cache(key, false);
      if (nullptr != test_cache) {
        // 可能前面的缓存被淘汰过，新起了拉取任务
        if (task_type_trait::get_task_id(test_cache->io_task) == ctx.get_task_context().task_id) {
          task_type_trait::reset_task(test_cache->io_task);
        }

        // 可能前面的缓存被淘汰过，新起了拉取任务，那么数据刷到最新即可
        // 理论上也不应该会走到这里流程
        if (out->data_version > test_cache->data_version) {
          set_cache(out);
        }
      } else {
        // 创建缓存，可能前面的缓存被淘汰了
        set_cache(out);
      }
    } else {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == ret) {
        FWLOGWARNING("Try to rpc fetch data failed and will remove lru cache(task: {}), res: {}",
                     ctx.get_task_context().task_id, ret);
      } else {
        FWLOGERROR("Try to rpc fetch data failed and will remove lru cache(task: {}), res: {}",
                   ctx.get_task_context().task_id, ret);
      }
      // 拉取期间缓存可能被淘汰并由其他任务重新拉取，只清除仍属于自己的条目，避免误删他人的新缓存
      if (get_cache(key, false) == out) {
        remove_cache(key);
      }
    }

    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 等待并提取保存结果
   * @note 这个接口会自动排队且合并多个保存请求
   * @param[out] out 输出的缓存对象指针，注意这个参数仅仅是传出参数，并不是保存这个缓存块
   * @param[in]  fn  保存协程函数
   * @return 0或错误码
   */
  result_code_type await_save(
      rpc::context &ctx, cache_ptr_type &inout,
      std::function<result_code_type(rpc::context &ctx, const value_type &in, int64_t *out_version)> fn) {
    if (!inout) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    if (inout->removed) {
      // 缓存已被 remove_cache 显式移除（记录已删除），禁止旧句柄再写回；
      // await_fetch 显式重新获取后 removed 标记会被清除，届时可继续保存
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
    }

    if (!fn) {
      FWLOGERROR("{} must be called with rpc function", "rpc_lru_cache_map<KEY,ALUE>.await_save");
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

    // 分配一个保存序号，相当于保存版本号
    uint64_t this_saving_seq = ++inout->saving_sequence;

    // 如果有其他任务正在做 IO，则需要等待那个任务完成。
    // 因为可能叠加很多任务，所以不能直接用拉取接口里得重试次数
    while (true) {
      if (inout->removed) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
      }

      // 如果其他得任务得保存已经覆盖了自己得版本，直接成功返回
      if (inout->saved_sequence >= this_saving_seq) {
        RPC_RETURN_CODE(0);
      }

      // 自己就是 IO 任务
      if (task_type_trait::empty(inout->io_task)) {
        break;
      }

      if (task_type_trait::is_exiting(inout->io_task)) {
        // fallback, clear data, 理论上不会走到这个流程，前面就是reset掉
        task_type_trait::reset_task(inout->io_task);
        continue;
      }

      if (task_type_trait::get_task_id(inout->io_task) == ctx.get_task_context().task_id) {
        // 重入调用：当前任务就是该缓存的 IO 任务，等待自己会死循环，直接走自己的保存流程
        break;
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, inout->io_task));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
      task_type_trait::reset_task(inout->io_task);
    }

    // 实际保存序号，因为可能延时执行，所以实际保存得时候可能被merge了其他请求得数据
    uint64_t real_saving_seq = inout->saving_sequence;

    auto invoke_result = rpc::async_invoke(
        ctx, "rpc_lru_cache_map.await_save",
        [inout, fn = std::move(fn)](rpc::context &child_ctx) -> rpc::result_code_type {
          int32_t ret = RPC_AWAIT_CODE_RESULT(fn(child_ctx, inout->data_object, &inout->data_version));

          if (task_type_trait::get_task_id(inout->io_task) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(inout->io_task);
          }

          RPC_RETURN_CODE(ret);
        });
    int32_t ret = 0;
    if (invoke_result.is_error()) {
      ret = *invoke_result.get_error();
    } else {
      // 拉取结束，重置拉取任务
      if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
        inout->io_task = *invoke_result.get_success();
      }
      ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
      if (ret >= 0) {
        ret = task_type_trait::get_result(*invoke_result.get_success());
      }
    }

    if (0 == ret && real_saving_seq > inout->saved_sequence) {
      inout->saved_sequence = real_saving_seq;
    } else if (0 != ret) {
      cache_ptr_type cur = get_cache(inout->data_key, false);
      if (cur == inout) {
        // 数据错误，清除缓存，下次重新拉取
        remove_cache(inout->data_key);
      }
    }

    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 等待指定 key 的在途拉取/保存任务结束（排空 IO），不发起新的读写
   * @note 用于删除/移除记录前的排空，避免删除后晚到的 IO 复活记录；相比 await_save 不会多保存一次
   * @param[in] key key
   * @return 0或错误码；没有缓存或没有在途 IO 时立即返回 0
   */
  result_code_type await_io_task(rpc::context &ctx, const key_type &key) {
    TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

    while (true) {
      cache_ptr_type cache = get_cache(key, false);
      if (nullptr == cache) {
        RPC_RETURN_CODE(0);
      }

      if (task_type_trait::empty(cache->io_task)) {
        RPC_RETURN_CODE(0);
      }

      if (task_type_trait::is_exiting(cache->io_task)) {
        // fallback, clear data, 理论上不会走到这个流程，前面就是reset掉
        task_type_trait::reset_task(cache->io_task);
        continue;
      }

      if (task_type_trait::get_task_id(cache->io_task) == ctx.get_task_context().task_id) {
        // 当前任务就是该缓存的 IO 任务，等待自己会死锁
        RPC_RETURN_CODE(0);
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, cache->io_task));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
      task_type_trait::reset_task(cache->io_task);
    }
  }

  /**
   * @brief 判断IO任务是否正在执行
   *
   * @param key 缓存Key
   * @return 缓存存在且IO任务正在执行返回true
   */
  bool is_io_task_running(const key_type &key) {
    cache_ptr_type cache = get_cache(key, false);
    if (nullptr == cache) {
      return false;
    }
    if (task_type_trait::empty(cache->io_task)) {
      return false;
    }
    if (task_type_trait::is_exiting(cache->io_task)) {
      return false;
    }
    return true;
  }

 private:
  lru_map_type pool_;
};
}  // namespace rpc
