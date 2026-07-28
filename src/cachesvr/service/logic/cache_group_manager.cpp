// Copyright 2026 atframework
// Created by owent on 2020-12-19.
//

#include "logic/cache_group_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/arena.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <protocol/pbdesc/com.struct.cache.pb.h>
#include <protocol/pbdesc/svr.struct.cache.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <dispatcher/task_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/logic_server_setup.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/user/user_basic.h>

#include <rpc/cache/cache_algorithm.h>

#include <rpc/lobby/lobbysvrservice.atfw.gen.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config/server_frame_build_feature.h"

cache_group_manager::cache_group_manager()
    : user_cache_group_(*this, cache_group_manager::pull_user_cache_fn, cache_group_manager::pack_user_cache_fn,
                        cache_group_manager::update_meta_user_cache_fn) {}

cache_group_manager::~cache_group_manager() {}

int cache_group_manager::tick() {
  time_t now = util::time::time_utility::get_now();

  int ret = 0;
  ret += tick_watcher(now);
  ret += tick_cache_groups(now);
  if (!double_check_cache_objects_.empty()) {
    ret += tick_double_check(now);
  }

  return ret;
}

int cache_group_manager::tick_watcher(time_t now) {
  const auto &watcher_conf = logic_config::me()->get_logic_cfg().cache().watcher();

  size_t max_recycle_count = watcher_conf.max_recycle_count_per_tick();
  if (max_recycle_count <= 0) {
    max_recycle_count = 1000;
  }

  // size_t max_watcher_number = watcher_conf.max_number();
  // if (max_watcher_number <= 0) {
  //     max_watcher_number = 1000000;
  // }
  size_t ret = 0;

  // Watcher GC流程
  for (; !timers_.empty() && (0 == max_recycle_count || ret < max_recycle_count); ++ret) {
    auto iter = timers_.begin();

    cache_object_base *cache_object = (*iter).cache_object;
    cache_watcher_t *watcher_object = (*iter).watcher_object;

    bool need_remove = false;
    do {
      // 无效数据
      if (nullptr == cache_object || nullptr == watcher_object) {
        need_remove = true;
        break;
      }

      // 长时间未访问，缓存可以淘汰。正常watcher会定期刷新访问时间
      if (watcher_object->get_expired_time() <= now) {
        need_remove = true;
        break;
      }

      // TODO 缓存数量超出预期,高负载保护
      // if (max_watcher_number > 0 && timers_size > max_watcher_number) {
      //     need_remove = true;
      //
      //     // TODO OSS日志告警。可能需要扩容缓存服务器
      //     break;
      // }
    } while (false);

    if (need_remove) {
      // 和下面 remove_timer 一样
      // 支持重入,这里需要和cache_watcher_t::cleanup_timer保持一致
      if (nullptr != watcher_object && iter == watcher_object->get_timer_handle()) {
        // 解绑watcher内的定时器，后面会移除
        watcher_object->move_timer_out();
      }

      // 先复制，允许重入
      timers_.erase(iter);

      // 也要移除相关的watcher
      if (nullptr != cache_object && nullptr != watcher_object) {
        // 如果是即将释放最后一个watcher，可以加入double check队列，尽早释放cache对象
        if (1 == cache_object->get_watcher_size()) {
          double_check_cache_objects_.insert(cache_object->get_key());
        }
        cache_object->remove_watcher(watcher_object->get_key(), watcher_object);
      }
      continue;
    }

    if (watcher_object->get_next_check_time() > now) {
      break;
    }

    // 刷新重设定时器
    setup_timer(*cache_object, *watcher_object);
  }

  return static_cast<int>(ret);
}

int cache_group_manager::tick_cache_groups(time_t now) {
  const auto &data_conf = logic_config::me()->get_logic_cfg().cache().data();

  size_t max_recycle_count = data_conf.max_recycle_count_per_tick();
  if (max_recycle_count <= 0) {
    max_recycle_count = 100;
  }

  int64_t cachesvr_version = 1;
  logic_server_common_module *logic_module = logic_server_last_common_module();
  if (nullptr != logic_module) {
    cachesvr_version = logic_module->get_discovery_service_version(atfw::component::logic_service_type::kCacheSvr);
  }

  int ret = 0;

  // 缓存组tick - user cache
  {
    int res = user_cache_group_.tick(cachesvr_version, now, data_conf.max_user_cache_number(),
                                     data_conf.gc_user_cache_number(), max_recycle_count);
    if (res >= 0) {
      ret += res;
    }
  }
  return ret;
}

int cache_group_manager::tick_double_check(time_t) {
  int ret = 0;
  std::unordered_set<PROJECT_NAMESPACE_ID::object_cache_key, rpc::cache_api::cache_key_hash_t,
                     rpc::cache_api::cache_key_equal_t>
      double_check_cache_objects;
  double_check_cache_objects.swap(double_check_cache_objects_);
  double_check_cache_objects_.reserve(256);

  for (const auto &cache_key : double_check_cache_objects) {
    cache_group_base *group = get_group(cache_key.cache_type());
    if (nullptr == group) {
      continue;
    }

    std::shared_ptr<cache_object_base> cache_object = group->get_cache(cache_key);
    if (!cache_object) {
      continue;
    }

    if (!cache_object->has_watcher() && !cache_object->is_cache_valid()) {
      group->remove_cache(cache_key, false);
      ++ret;
    }
  }

  return ret;
}

void cache_group_manager::setup_timer(cache_object_base &cache_object, cache_watcher_t &watcher) {
  cache_watcher_timer_handle_t handle = timers_.insert(timers_.end(), cache_watcher_timer_t{&cache_object, &watcher});
  watcher.move_timer_in(*this, std::move(handle));
  watcher.update_next_check_time();
}

void cache_group_manager::remove_timer(cache_watcher_timer_handle_t &handle) {
  if (handle == timers_.end()) {
    return;
  }

  cache_object_base *cache_object = (*handle).cache_object;
  cache_watcher_t *watcher_object = (*handle).watcher_object;

  // 和上面 tick_watcher 一样
  // 支持重入,这里需要和cache_watcher_t::cleanup_timer保持一致
  if (nullptr != watcher_object && handle == watcher_object->get_timer_handle()) {
    // 解绑watcher内的定时器，后面会移除
    watcher_object->move_timer_out();
  }

  // 先复制，允许重入
  timers_.erase(handle);
  reset_timer_handle(handle);

  // 也要移除相关的watcher
  if (nullptr != cache_object && nullptr != watcher_object) {
    // 如果是即将释放最后一个watcher，可以加入double check队列，尽早释放cache对象
    if (1 == cache_object->get_watcher_size()) {
      double_check_cache_objects_.insert(cache_object->get_key());
    }
    cache_object->remove_watcher(watcher_object->get_key(), watcher_object);
  }
}

void cache_group_manager::reset_timer_handle(cache_watcher_timer_handle_t &handle) { handle = timers_.end(); }

bool cache_group_manager::is_time_handle_valid(const cache_watcher_timer_handle_t &handle) {
  return handle != timers_.end();
}

cache_group_base *cache_group_manager::get_group(PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type) {
  switch (cache_type) {
    case PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER:
      return &user_cache_group_;
    default:
      return nullptr;
  }
}

rpc::result_code_type cache_group_manager::pull_user_cache_fn(::rpc::context &ctx,
                                                              user_cache_group_t::pull_data_param_ptr_t &fill_data) {
  if (nullptr == fill_data || fill_data->empty()) {
    RPC_RETURN_CODE(0);
  }

  std::vector<rpc::db::user::table_key_t> user_id_key;
  std::vector<rpc::db::user::batch_get_result_t> pull_user_results;
  user_id_key.reserve(fill_data->size());

  std::vector<rpc::db::login_lock::table_key_t> login_lock_key;
  std::vector<rpc::db::login_lock::batch_get_result_t> pull_login_lock_results;
  std::unordered_map<uint64_t, const rpc::db::login_lock::batch_get_result_t *> pull_login_lock_index;
  login_lock_key.reserve(fill_data->size());

  std::unordered_set<uint64_t> pull_user_id_set;
  pull_user_id_set.reserve(fill_data->size());

  for (auto &fill_key : *fill_data) {
    if (nullptr == fill_key.second) {
      continue;
    }

    user_id_key.emplace_back(fill_key.first.zone_id(), fill_key.first.instance_id());
    if (pull_user_id_set.end() == pull_user_id_set.find(fill_key.first.instance_id())) {
      pull_user_id_set.insert(fill_key.first.instance_id());
      login_lock_key.emplace_back(fill_key.first.instance_id());
    }
  }

  if (user_id_key.empty()) {
    RPC_RETURN_CODE(0);
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::user::batch_partly_get_basic_info(ctx, gsl::make_span(user_id_key), pull_user_results));
  if (ret < 0) {
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      RPC_RETURN_CODE(0);
    }
    FWLOGERROR("rpc::db::user::batch_partly_get_basic_info failed, res: {}({})", ret,
               protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  // 在线表
  ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_lock::batch_get_all(ctx, gsl::make_span(login_lock_key), pull_login_lock_results));
  if (ret < 0) {
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      RPC_RETURN_CODE(0);
    }
    FWLOGERROR("rpc::db::login_lock::batch_get_all failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }
  pull_login_lock_index.reserve(pull_login_lock_results.size());
  for (const auto &table : pull_login_lock_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    pull_login_lock_index[(*table.message)->user_id()] = &table;
  }

  for (const auto &table : pull_user_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_key table_key;

    table_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    table_key.set_zone_id((*table.message)->zone_id());
    table_key.set_instance_id((*table.message)->user_id());

    auto iter_fill = fill_data->find(table_key);
    if (iter_fill == fill_data->end()) {
      continue;
    }

    if (nullptr == iter_fill->second) {
      continue;
    }

    rpc::user::convert_to_client_data(*iter_fill->second, **table.message);
  }

  std::vector<task_type_trait::task_type> pending_tasks;
  pending_tasks.reserve(pull_login_lock_index.size());

  for (const auto &table : pull_user_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_key table_key;

    table_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    table_key.set_zone_id((*table.message)->zone_id());
    table_key.set_instance_id((*table.message)->user_id());

    auto iter_fill = fill_data->find(table_key);
    if (iter_fill == fill_data->end()) {
      continue;
    }

    if (nullptr == iter_fill->second) {
      continue;
    }

    // 判定是否在线
    auto iter_login_lock = pull_login_lock_index.find((*table.message)->user_id());
    if (iter_login_lock == pull_login_lock_index.end()) {
      continue;
    }
    const auto &login_lock_table = *iter_login_lock->second->message;

    uint64_t destination_server_id = login_lock_table->router_server_id();
    if (destination_server_id == 0) {
      continue;
    }

    auto fill_data_ptr = fill_data;
    // 查询gamesvr
    auto invoke_task = rpc::async_invoke(
        ctx, "cache_group_manager::pull_user_cache_fn",
        [fill_data_ptr, table_key, destination_server_id](rpc::context &child_ctx) -> rpc::result_code_type {
          auto req_body = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSObjectCacheGetUserCacheDataReq>(child_ctx);
          auto rsp_body = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSObjectCacheGetUserCacheDataRsp>(child_ctx);
          protobuf_copy_message(*req_body->mutable_key(), table_key);

          auto res = RPC_AWAIT_CODE_RESULT(rpc::lobby::object_cache_get_user_cache_data(
              child_ctx, destination_server_id, table_key.zone_id(), table_key.instance_id(),
              std::to_string(table_key.instance_id()), *req_body, *rsp_body));
          if (res < 0) {
            FWLOGERROR("user {}:{} get_user_cache_data failed.res: {}({})", table_key.zone_id(),
                       table_key.instance_id(), res, protobuf_mini_dumper_get_error_msg(res));
            RPC_RETURN_CODE(res);
          }

          if (PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED == rsp_body->result()) {
            FWLOGINFO("user {}:{} but offline, init by db", table_key.zone_id(), table_key.instance_id());
            RPC_RETURN_CODE(0);
          }

          auto iter_fill_data = fill_data_ptr->find(table_key);
          if (iter_fill_data == fill_data_ptr->end()) {
            RPC_RETURN_CODE(0);
          }

          if (nullptr == iter_fill_data->second) {
            RPC_RETURN_CODE(0);
          }

          update_meta_user_cache_fn(child_ctx, rsp_body->cache_meta(), *iter_fill_data->second);
          RPC_RETURN_CODE(0);
        });

    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_tasks.emplace_back(std::move(*invoke_task.get_success()));
      } else {
        if (task_type_trait::get_result(*invoke_task.get_success()) != 0) {
          FWLOGERROR("invoke_task a task to Get failed.res: {}({})",
                     task_type_trait::get_result(*invoke_task.get_success()),
                     protobuf_mini_dumper_get_error_msg(task_type_trait::get_result(*invoke_task.get_success())));
          continue;
        }
      }
    } else {
      FWLOGERROR("invoke_task a task to Get failed.res: {}({})", *invoke_task.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_task.get_error()));
      continue;
    }
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(ctx, pending_tasks));
  if (ret == 0) {
    // check single task result
    for (const auto &task : pending_tasks) {
      ret = task_type_trait::get_result(task);
      if (ret != 0) {
        RPC_RETURN_CODE(ret);
      }
    }
  } else {
    FWLOGERROR("pull_user_cache_fn failed, result: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}

void cache_group_manager::pack_user_cache_fn(
    rpc::context &ctx, const user_cache_group_t::value_type &input,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &output) {
  PROJECT_NAMESPACE_ID::object_cache_content *content = output.Add();
  if (nullptr == content) {
    return;
  }

  PROJECT_NAMESPACE_ID::DCacheApiObjectData convert_data;
  PROJECT_NAMESPACE_ID::DCacheApiObjectData *convert_data_ptr = nullptr;
  if (input.get_data().GetArena() != nullptr) {
    convert_data_ptr =
        google::protobuf::Arena::Create<PROJECT_NAMESPACE_ID::DCacheApiObjectData>(input.get_data().GetArena());
  } else {
    convert_data_ptr = &convert_data;
  }
  if (convert_data_ptr == nullptr) {
    return;
  }
  convert_data_ptr->set_allocated_user_cache(const_cast<PROJECT_NAMESPACE_ID::DUserBasicData *>(&input.get_data()));
  auto defer_clear_data = gsl::finally(
      [&convert_data_ptr]() { ATFW_EXPLICIT_UNUSED_ATTR auto *_ = convert_data_ptr->release_user_cache(); });

  if (rpc::cache_api::pack_cache_content_to_any(ctx, *content->mutable_cache_data(), *convert_data_ptr)) {
    content->set_data_version(input.get_data_version());
  }
}

void cache_group_manager::update_meta_user_cache_fn(rpc::context &ctx,
                                                    const PROJECT_NAMESPACE_ID::object_cache_meta &input,
                                                    user_cache_group_t::cache_type &output) {
  if (input.cache_meta().type_url().empty()) {
    return;
  }

  auto meta_msg = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DCacheApiMetaData>(ctx);
  if (!rpc::cache_api::unpack_cache_meta_from_any(ctx, *meta_msg, input.cache_meta())) {
    FWLOGERROR("unpack user cache meta failed, got type_url: {}, size: {}, unpack to {}", input.cache_meta().type_url(),
               input.cache_meta().value().size(), meta_msg->GetDescriptor()->full_name());
    return;
  }

  if (meta_msg->has_user_meta()) {
    rpc::cache_api::update_cache_content_from_meta(ctx, output, meta_msg->user_meta());
  }
}
