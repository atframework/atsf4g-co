//
// Created by owt50 on 2016/9/26.
//

#include <typeinfo>

#include "actor_action_base.h"
#include "dispatcher_implement.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <utility/protobuf_mini_dumper.h>

#if defined(__GLIBCXX__) || defined(_LIBCOPP_ABI_VERSION)
#  include <cxxabi.h>
#endif

int dispatcher_implement::init() { return 0; }

const char *dispatcher_implement::name() const {
  if (!human_readable_name_.empty()) {
    return human_readable_name_.c_str();
  }
#if defined(__GLIBCXX__) || defined(_LIBCOPP_ABI_VERSION)
  const char *raw_name = typeid(*this).name();
  int cxx_abi_status;
  char *readable_name = abi::__cxa_demangle(raw_name, 0, 0, &cxx_abi_status);
  if (nullptr == readable_name) {
    human_readable_name_ = ::atapp::module_impl::name();
    return human_readable_name_.c_str();
  }

  human_readable_name_ = readable_name;
  free(readable_name);

#else
  human_readable_name_ = ::atapp::module_impl::name();
#endif

  return human_readable_name_.c_str();
}

uintptr_t dispatcher_implement::get_instance_ident() const { return reinterpret_cast<uintptr_t>(this); }

dispatcher_implement::dispatcher_result_t dispatcher_implement::on_receive_message(rpc::context &ctx, msg_raw_t &msg,
                                                                                   void *priv_data, uint64_t sequence) {
  return on_receive_message(ctx, msg, get_instance_ident(), priv_data, sequence);
}

dispatcher_implement::dispatcher_result_t dispatcher_implement::on_receive_message(rpc::context &ctx, msg_raw_t &msg,
                                                                                   uintptr_t expect_msg_type,
                                                                                   void *priv_data, uint64_t sequence) {
  dispatcher_result_t ret;
  if (nullptr == msg.msg_addr) {
    FWLOGERROR("msg.msg_addr == nullptr.");
    ret.result_code = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
    return ret;
  }

  if (expect_msg_type != msg.msg_type) {
    FWLOGERROR("msg.msg_type expected: {}, real: {}.", expect_msg_type, msg.msg_type);
    ret.result_code = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
    return ret;
  }

  // 消息过滤器
  // 用于提供给所有消息进行前置处理的功能
  // 过滤器可以控制消息是否要下发下去
  if (!msg_filter_list_.empty()) {
    msg_filter_data_t filter_data(msg);

    for (std::list<msg_filter_handle_t>::iterator iter = msg_filter_list_.begin(); iter != msg_filter_list_.end();
         ++iter) {
      if (false == (*iter)(filter_data)) {
        ret.result_code = 0;
        return ret;
      }
    }
  }

  msg_op_type_t op_type = pick_msg_op_type(msg);
  if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_MIXUP == op_type ||
      PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE == op_type) {
    ret.task_id = pick_msg_task_id(msg);
    if (ret.task_id > 0) {  // 如果是恢复任务则尝试切回协程任务
      // 查找并恢复已有task
      dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
      callback_data.message = msg;
      callback_data.private_data = priv_data;
      callback_data.sequence = sequence;
      callback_data.context = &ctx;

      ret.result_code = rpc::custom_resume(ret.task_id, callback_data);
      return ret;
    }

    if (PROJECT_NAMESPACE_ID::EN_MSG_OP_TYPE_UNARY_RESPONSE == op_type) {
      FWLOGDEBUG("Ignore response message {}:{} of {} without task id", pick_rpc_name(msg), pick_msg_type_id(msg),
                 name());
      ret.result_code = 0;
      return ret;
    }
  }

  dispatcher_start_data_type callback_data = dispatcher_make_default<dispatcher_start_data_type>();
  callback_data.message = msg;
  callback_data.private_data = priv_data;
  callback_data.context = &ctx;

  // 先尝试使用task 模块
  int res = create_task(callback_data, ret.task_id);
  ret.options = callback_data.options;

  if (res == PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND) {
    task_manager::actor_action_ptr_t actor;
    if (!actor_action_map_by_id_.empty()) {
      actor = create_actor(callback_data);
      ret.options = callback_data.options;
      // actor 流程
      if (actor) {
        ret.result_code = actor->run(std::move(callback_data));
        return ret;
      }
    }
  }

  if (res < 0) {
    if (PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND == res) {
      FWLOGWARNING("{}(type={}) create task failed, task action or actor action not registered", name(),
                   expect_msg_type);
    } else {
      FWLOGERROR("{}(type={}) create task failed, error={}({})", name(), expect_msg_type, res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
    ret.result_code = res;

    on_create_task_failed(callback_data, res);
    return ret;
  }

  // 不创建消息
  if (res == 0 && 0 == ret.task_id) {
    ret.result_code = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    return ret;
  }

  // 再启动
  ret.result_code = task_manager::me()->start_task(ret.task_id, callback_data);
  return ret;
}

int32_t dispatcher_implement::on_send_message_failed(rpc::context &ctx, msg_raw_t &msg, int32_t error_code,
                                                     uint64_t sequence) {
  // msg->set_rpc_result(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_SEND_FAILED);
  uint64_t task_id = pick_msg_task_id(msg);
  if (task_id > 0) {  // 如果是恢复任务则尝试切回协程任务
    FWLOGERROR("dispatcher {} send data failed with error code = {}, try to resume task {}", name(), error_code,
               task_id);
    // 查找并恢复已有task
    dispatcher_resume_data_type callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
    callback_data.message = msg;
    callback_data.sequence = sequence;
    callback_data.context = &ctx;

    return rpc::custom_resume(task_id, callback_data);
  }

  FWLOGERROR("send data failed with error code = {}", error_code);
  return 0;
}

void dispatcher_implement::on_create_task_failed(dispatcher_start_data_type &, int32_t) {}

int dispatcher_implement::create_task(dispatcher_start_data_type &start_data, task_type_trait::id_type &task_id) {
  task_id = 0;

  msg_type_t msg_type_id = pick_msg_type_id(start_data.message);
  const std::string &rpc_name = pick_rpc_name(start_data.message);
  if (0 == msg_type_id && rpc_name.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  if (task_action_map_by_id_.empty() && task_action_map_by_name_.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  if (0 != msg_type_id) {
    msg_task_action_set_t::iterator iter = task_action_map_by_id_.find(msg_type_id);
    if (task_action_map_by_id_.end() != iter && iter->second) {
      start_data.options = &iter->second->options;
      return (*iter->second)(task_id, start_data);
    }
  }

  if (!rpc_name.empty()) {
    rpc_task_action_set_t::iterator iter = task_action_map_by_name_.find(rpc_name);
    if (task_action_map_by_name_.end() != iter && iter->second) {
      start_data.options = &iter->second->options;
      return (*iter->second)(task_id, start_data);
    }
  }

  return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
}

task_manager::actor_action_ptr_t dispatcher_implement::create_actor(dispatcher_start_data_type &start_data) {
  msg_type_t msg_type_id = pick_msg_type_id(start_data.message);
  const std::string &rpc_name = pick_rpc_name(start_data.message);
  if (0 == msg_type_id && rpc_name.empty()) {
    return nullptr;
  }

  if (actor_action_map_by_id_.empty() && actor_action_map_by_name_.empty()) {
    return nullptr;
  }

  if (0 != msg_type_id) {
    msg_actor_action_set_t::iterator iter = actor_action_map_by_id_.find(msg_type_id);
    if (actor_action_map_by_id_.end() != iter && iter->second) {
      start_data.options = &iter->second->options;
      return (*iter->second)(start_data);
    }
  }

  if (!rpc_name.empty()) {
    rpc_actor_action_set_t::iterator iter = actor_action_map_by_name_.find(rpc_name);
    if (actor_action_map_by_name_.end() != iter && iter->second) {
      start_data.options = &iter->second->options;
      return (*iter->second)(start_data);
    }
  }

  return nullptr;
}

const atframework::DispatcherOptions *dispatcher_implement::get_options_by_message_type(msg_type_t) { return nullptr; }

void dispatcher_implement::push_filter_to_front(msg_filter_handle_t fn) { msg_filter_list_.push_front(fn); }

void dispatcher_implement::push_filter_to_back(msg_filter_handle_t fn) { msg_filter_list_.push_back(fn); }

bool dispatcher_implement::is_closing() const noexcept { return NULL == get_app() || get_app()->is_closing(); }

dispatcher_implement::rpc_service_set_t::mapped_type dispatcher_implement::get_registered_service(
    const std::string &service_full_name) const noexcept {
  auto iter = registered_service_.find(service_full_name);
  if (iter == registered_service_.end()) {
    return nullptr;
  }

  return iter->second;
}

dispatcher_implement::rpc_method_set_t::mapped_type dispatcher_implement::get_registered_method(
    const std::string &method_full_name) const noexcept {
  auto iter = registered_method_.find(method_full_name);
  if (iter == registered_method_.end()) {
    return nullptr;
  }

  return iter->second;
}

const std::string &dispatcher_implement::get_empty_string() {
  static std::string ret;
  return ret;
}

int dispatcher_implement::_register_action(msg_type_t msg_type, task_manager::task_action_creator_t action) {
  msg_task_action_set_t::iterator iter = task_action_map_by_id_.find(msg_type);
  if (task_action_map_by_id_.end() != iter) {
    FWLOGERROR("{} try to register more than one task actions to type {}.", name(), msg_type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  task_action_map_by_id_[msg_type] = action;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int dispatcher_implement::_register_action(msg_type_t msg_type, task_manager::actor_action_creator_t action) {
  msg_actor_action_set_t::iterator iter = actor_action_map_by_id_.find(msg_type);
  if (actor_action_map_by_id_.end() != iter) {
    FWLOGERROR("{} try to register more than one actor actions to type {}.", name(), msg_type);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  actor_action_map_by_id_[msg_type] = action;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int dispatcher_implement::_register_action(const std::string &rpc_full_name,
                                           task_manager::task_action_creator_t action) {
  rpc_task_action_set_t::iterator iter = task_action_map_by_name_.find(rpc_full_name);
  if (task_action_map_by_name_.end() != iter) {
    FWLOGERROR("{} try to register more than one task actions to rpc {}.", name(), rpc_full_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  task_action_map_by_name_[rpc_full_name] = action;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int dispatcher_implement::_register_action(const std::string &rpc_full_name,
                                           task_manager::actor_action_creator_t action) {
  rpc_actor_action_set_t::iterator iter = actor_action_map_by_name_.find(rpc_full_name);
  if (actor_action_map_by_name_.end() != iter) {
    FWLOGERROR("{} try to register more than one actor actions to rpc {}.", name(), rpc_full_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  actor_action_map_by_name_[rpc_full_name] = action;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
