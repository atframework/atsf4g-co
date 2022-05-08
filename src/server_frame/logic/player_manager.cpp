// Copyright 2022 atframework

#include "logic/player_manager.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <log/log_wrapper.h>
#include <proto_base.h>
#include <time/time_utility.h>

#include <config/logic_config.h>
#include <dispatcher/task_manager.h>
#include <rpc/db/login.h>
#include <rpc/db/player.h>
#include <rpc/rpc_utils.h>
#include <utility/protobuf_mini_dumper.h>

#include <router/router_manager_set.h>
#include <router/router_player_manager.h>

#include "logic/session_manager.h"

rpc::result_code_type player_manager::remove(rpc::context &ctx, player_manager::player_ptr_t u, bool force_kickoff) {
  if (!u) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
  }

  return remove(ctx, u->get_user_id(), u->get_zone_id(), force_kickoff, u.get());
}

rpc::result_code_type player_manager::remove(rpc::context &ctx, uint64_t user_id, uint32_t zone_id, bool force_kickoff,
                                             player_cache *check_user) {
  if (0 == user_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);

  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);
  // 先保存用户数据，防止重复保存
  if (!cache) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (check_user != nullptr && false == cache->is_object_equal(*check_user)) {
    auto check_sess = check_user->get_session();
    check_user->set_session(ctx, nullptr);
    if (check_sess && check_sess->get_player().get() == check_user) {
      check_sess->set_player(nullptr);
      session_manager::me()->remove(check_sess, ::atframe::gateway::close_reason_t::EN_CRT_KICKOFF);
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!force_kickoff && !cache->is_writable()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 这里会触发保存
  if (force_kickoff) {
    return router_player_manager::me()->remove_player_cache(ctx, user_id, zone_id, cache, nullptr);
  } else {
    return router_player_manager::me()->remove_player_object(ctx, user_id, zone_id, nullptr);
  }
}

rpc::result_code_type player_manager::save(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                           const player_cache *check_user) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

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
    FWLOGERROR("save player_cache {}:{} failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

bool player_manager::add_save_schedule(uint64_t user_id, uint32_t zone_id) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (!cache || !cache->is_writable()) {
    return false;
  }

  return router_manager_set::me()->add_save_schedule(std::static_pointer_cast<router_object_base>(cache));
}

rpc::result_code_type player_manager::load(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                           player_manager::player_ptr_t &output, bool force) {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (force || !cache) {
    auto res = RPC_AWAIT_CODE_RESULT(router_player_manager::me()->mutable_object(ctx, cache, key, nullptr));
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

size_t player_manager::size() const { return router_player_manager::me()->size(); }

rpc::result_code_type player_manager::create(rpc::context &ctx, uint64_t user_id, uint32_t zone_id,
                                             const std::string &openid, PROJECT_NAMESPACE_ID::table_login &login_tb,
                                             std::string &login_ver, player_manager::player_ptr_t &output) {
  if (0 == user_id || openid.empty()) {
    FWLOGERROR("can not create player_cache without user id or open id");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  if (find(user_id, zone_id)) {
    FWLOGERROR("player_cache {}:{} already exists, can not create again", zone_id, user_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  // online user number limit
  if (size() > logic_config::me()->get_logic().user().max_online()) {
    FWLOGERROR("online number extended");
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  PROJECT_NAMESPACE_ID::DPlayerIDKey user_key;
  user_key.set_user_id(user_id);
  user_key.set_zone_id(zone_id);
  // check conflict
  {
    auto lock_iter = create_user_lock_.find(user_key);
    if (lock_iter != create_user_lock_.end()) {
      FWLOGWARNING("there are more than one session trying to create player {}:{}", zone_id, user_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OTHER_DEVICE);
    }
  }
  create_user_lock_.insert(user_key);
  auto lock_guard = gsl::finally([user_key] {
    if (player_manager::is_instance_destroyed()) {
      return;
    }

    player_manager::me()->create_user_lock_.erase(user_key);
  });

  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache;
  router_player_private_type priv_data(&login_tb, &login_ver);

  auto res = RPC_AWAIT_CODE_RESULT(router_player_manager::me()->mutable_object(ctx, cache, key, &priv_data));
  if (res < 0 || !cache) {
    FWLOGERROR("pull player_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  output = cache->get_object();
  if (!output) {
    FWLOGERROR("player_cache {}:{} already exists(data version={}), can not create again", zone_id, user_id,
               output->get_data_version());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_ACCESS_DENY);
  }

  // 新用户，数据版本号为0，启动创建初始化
  if (0 == output->get_data_version()) {
    // manager 创建初始化
    if (output->get_login_info().account().version_type() >= PROJECT_NAMESPACE_ID::EN_VERSION_INNER) {
      res = RPC_AWAIT_CODE_RESULT(output->create_init(ctx, PROJECT_NAMESPACE_ID::EN_VERSION_DEFAULT));
    } else {
      res = RPC_AWAIT_CODE_RESULT(output->create_init(ctx, output->get_login_info().account().version_type()));
    }
    if (res < 0) {
      FWLOGERROR("save create {}:{} object failed, res: {}({})", zone_id, user_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }

    // 初始化完成，保存一次
    res = RPC_AWAIT_CODE_RESULT(cache->save(ctx, nullptr));
    if (res < 0) {
      FWLOGERROR("save player_cache {}:{} object failed, res: {}({})", zone_id, user_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }

    FWLOGINFO("create player {}:{} success", zone_id, user_id);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

player_manager::player_ptr_t player_manager::find(uint64_t user_id, uint32_t zone_id) const {
  router_player_cache::key_t key(router_player_manager::me()->get_type_id(), zone_id, user_id);
  router_player_cache::ptr_t cache = router_player_manager::me()->get_cache(key);

  if (cache && cache->is_writable()) {
    return cache->get_object();
  }

  return nullptr;
}

bool player_manager::has_create_user_lock(uint64_t user_id, uint32_t zone_id) const noexcept {
  PROJECT_NAMESPACE_ID::DPlayerIDKey user_key;
  user_key.set_user_id(user_id);
  user_key.set_zone_id(zone_id);

  return create_user_lock_.find(user_key) != create_user_lock_.end();
}
