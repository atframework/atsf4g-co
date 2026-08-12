// Copyright 2022 atframework

#include "logic/user_manager.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <atgateway/protocol/libatgw_protocol_api.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <dispatcher/task_manager.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>
#include <utility/protobuf_mini_dumper.h>

#include <router/router_manager_set.h>
#include <router/router_user_manager.h>
#include <memory>

#include "logic/session_manager.h"

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(user_manager);
#  else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(user_manager);
#  endif
#else
ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(user_manager);
#endif

SERVER_FRAME_API user_manager::user_manager() {}

SERVER_FRAME_API user_manager::~user_manager() {}

SERVER_FRAME_API rpc::result_code_type user_manager::remove(rpc::context &ctx, user_manager::user_ptr_t u,
                                                              bool force_kickoff) {
  if (!u) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(remove(ctx, u->get_user_id(), u->get_zone_id(), force_kickoff, u.get())));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API rpc::result_code_type user_manager::remove(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                                              bool force_kickoff, user_cache *check_user) {
  if (0 == user_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);

  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);
  // 先保存用户数据，防止重复保存
  if (!cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (check_user != nullptr && false == cache->is_object_equal(*check_user)) {
    auto check_sess = check_user->get_session();
    check_user->set_session(ctx, nullptr);
    if (check_sess && check_sess->get_user().get() == check_user) {
      check_sess->set_user(nullptr);
      session_manager::me()->remove(ctx, check_sess, static_cast<int32_t>(::atfw::gateway::close_reason_t::kKickoff));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!force_kickoff && !cache->is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 这里会触发保存
  if (force_kickoff) {
    RPC_RETURN_CODE(
        RPC_AWAIT_CODE_RESULT(router_user_manager::me()->remove_user_cache(ctx, user_id, zone_id, cache, nullptr)));
  } else {
    RPC_RETURN_CODE(
        RPC_AWAIT_CODE_RESULT(router_user_manager::me()->remove_user_object(ctx, user_id, zone_id, nullptr)));
  }
}

SERVER_FRAME_API void user_manager::async_remove(rpc::context &ctx, user_ptr_t u, bool force_kickoff) {
  if (!u) {
    return;
  }

  async_remove(ctx, u->get_user_id(), u->get_zone_id(), force_kickoff, u.get());
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API void user_manager::async_remove(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                                   bool force_kickoff, user_cache *check_user) {
  auto invoke_result = rpc::async_invoke(
      ctx, "user_manager.async_remove",
      [user_id, zone_id, force_kickoff, check_user](rpc::context &child_ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            user_manager::me()->remove(child_ctx, user_id, zone_id, force_kickoff, check_user)));
      });

  if (invoke_result.is_error()) {
    FWLOGERROR("Invoke task to remove user {}:{} failed, res: {}({})", zone_id, user_id, *invoke_result.get_error(),
               protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API rpc::result_code_type user_manager::save(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                                            const user_cache *check_user) {
  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);
  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);

  if (!cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  if (!cache->is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
  }

  if (check_user != nullptr && false == cache->is_object_equal(*check_user)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  auto res = RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr));
  if (res < 0) {
    FWLOGERROR("save user_cache {}:{} failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API bool user_manager::add_save_schedule(uint64_t user_id, uint32_t zone_id, bool kickoff) {
  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);
  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);

  if (!cache || !cache->is_writable()) {
    return false;
  }

  if (kickoff) {
    return router_manager_set::me()->add_downgrade_schedule(std::static_pointer_cast<router_object_base>(cache));
  }

  return router_manager_set::me()->add_save_schedule(std::static_pointer_cast<router_object_base>(cache));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API rpc::result_code_type user_manager::load(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                                            user_manager::user_ptr_t &output, bool force) {
  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);
  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);

  if (force || !cache) {
    auto res = RPC_AWAIT_CODE_RESULT(router_user_manager::me()->mutable_object(ctx, cache, key, nullptr));
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (cache) {
    output = cache->get_object();
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API size_t user_manager::size() const { return router_user_manager::me()->size(); }

SERVER_FRAME_API rpc::result_code_type user_manager::create(
    rpc::context &ctx, uint64_t user_id, uint32_t zone_id, const std::string &openid,
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> &login_lock_tb, uint64_t login_lock_ver,
    user_manager::user_ptr_t &output) {
  if (0 == user_id || openid.empty()) {
    FWLOGERROR("can not create user_cache without user id or open id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (find(user_id, zone_id)) {
    FWLOGERROR("user_cache {}:{} already exists, can not create again", zone_id, user_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // online user number limit
  if (size() > logic_config::me()->get_logic_cfg().user().max_online()) {
    FWLOGERROR("online number extended");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  user_key.set_user_id(user_id);
  user_key.set_zone_id(zone_id);
  // check conflict
  {
    auto lock_iter = create_user_lock_.find(user_key);
    if (lock_iter != create_user_lock_.end()) {
      FWLOGWARNING("there are more than one session trying to create user {}:{}", zone_id, user_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    }
  }
  create_user_lock_.insert(user_key);
  auto lock_guard = gsl::finally([user_key] {
    if (user_manager::is_instance_destroyed()) {
      return;
    }

    user_manager::me()->create_user_lock_.erase(user_key);
  });

  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);
  router_user_cache::ptr_t cache;
  router_user_private_type priv_data(&login_lock_tb, login_lock_ver, openid);

  auto res = RPC_AWAIT_CODE_RESULT(router_user_manager::me()->mutable_object(ctx, cache, key, &priv_data));
  if (res < 0 || !cache) {
    FWLOGERROR("pull user_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  output = cache->get_object();
  if (!output) {
    FWLOGERROR("user_cache {}:{} already exists(data version={}), can not create again", zone_id, user_id,
               output->get_data_version());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  // 新用户,启动创建初始化
  if (!output->has_create_init()) {
    // manager 创建初始化
    res = RPC_AWAIT_CODE_RESULT(output->create_init(ctx));
    if (res < 0) {
      FWLOGERROR("create_init user_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      auto remove_res = RPC_AWAIT_CODE_RESULT(router_user_manager::me()->remove_user_object(
          ctx, user_id, zone_id, std::static_pointer_cast<router_object_base>(cache), nullptr));
      if (remove_res < 0) {
        FWLOGERROR("remove user_cache {}:{} object after create_init failed, res: {}({})", zone_id, user_id,
                   remove_res, protobuf_mini_dumper_get_error_msg(remove_res));
      }
      RPC_RETURN_CODE(res);
    }

    // 初始化完成，保存一次
    res = RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr));
    if (res < 0) {
      FWLOGERROR("save user_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      auto remove_res = RPC_AWAIT_CODE_RESULT(router_user_manager::me()->remove_user_object(
          ctx, user_id, zone_id, std::static_pointer_cast<router_object_base>(cache), nullptr));
      if (remove_res < 0) {
        FWLOGERROR("remove user_cache {}:{} object after create_init failed, res: {}({})", zone_id, user_id,
                   remove_res, protobuf_mini_dumper_get_error_msg(remove_res));
      }
      RPC_RETURN_CODE(res);
    }

    FWLOGINFO("create user {}:{} success", zone_id, user_id);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SERVER_FRAME_API user_manager::user_ptr_t user_manager::find(uint64_t user_id, uint32_t zone_id) const {
  router_user_cache::key_t key(router_user_manager::me()->get_type_id(), zone_id, user_id);
  router_user_cache::ptr_t cache = router_user_manager::me()->get_cache(key);

  if (cache && cache->is_writable()) {
    return cache->get_object();
  }

  return nullptr;
}

SERVER_FRAME_API bool user_manager::has_create_user_lock(uint64_t user_id, uint32_t zone_id) const noexcept {
  PROJECT_NAMESPACE_ID::DUserIDKey user_key;
  user_key.set_user_id(user_id);
  user_key.set_zone_id(zone_id);

  return create_user_lock_.find(user_key) != create_user_lock_.end();
}
