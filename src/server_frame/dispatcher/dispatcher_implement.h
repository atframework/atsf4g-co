// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#ifndef DISPATCHER_DISPATCHER_IMPLEMENT_H
#define DISPATCHER_DISPATCHER_IMPLEMENT_H

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <std/explicit_declare.h>

#include <gsl/select-gsl.h>

#include <atframe/atapp.h>
#include <atframe/atapp_module_impl.h>
#include <libatbus_protocol.h>

#include <utility/environment_helper.h>
#include <utility/protobuf_mini_dumper.h>

#include <list>
#include <string>
#include <unordered_map>

#include "dispatcher/dispatcher_type_defines.h"

#include "dispatcher/task_manager.h"

class dispatcher_implement : public ::atapp::module_impl {
 public:
  using msg_op_type_t = PROJECT_NAMESPACE_ID::EnMsgOpType;
  using msg_raw_t = dispatcher_raw_message;
  using resume_data_t = dispatcher_resume_data_t;
  using start_data_t = dispatcher_start_data_t;
  using msg_type_t = uint32_t;
  using msg_task_action_set_t = std::unordered_map<msg_type_t, task_manager::task_action_creator_t>;
  using msg_actor_action_set_t = std::unordered_map<msg_type_t, task_manager::actor_action_creator_t>;
  using rpc_task_action_set_t = std::unordered_map<std::string, task_manager::task_action_creator_t>;
  using rpc_actor_action_set_t = std::unordered_map<std::string, task_manager::actor_action_creator_t>;
  using rpc_service_set_t =
      std::unordered_map<std::string, const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::ServiceDescriptor *>;
  using rpc_method_set_t =
      std::unordered_map<std::string, const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *>;

  struct msg_filter_data_t {
    dispatcher_raw_message msg;

    inline explicit msg_filter_data_t(const dispatcher_raw_message &m) : msg(m) {}
  };

  struct dispatcher_result_t {
    int32_t result_code;
    uint64_t task_id;                               // 如果启动了一个协程任务，传出任务ID
    const atframework::DispatcherOptions *options;  // 如果启动了新任务传出可能的配置选项数据

    inline dispatcher_result_t() : result_code(0), task_id(0), options(nullptr) {}
  };

  /**
   * @brief 消息过滤器函数，调用式为: bool(const msg_filter_data_t&)
   * @note 返回false可以中断后续过滤器的执行并禁止消息分发
   */
  using msg_filter_handle_t = std::function<bool(const msg_filter_data_t &)>;

 public:
  int init() override;

  const char *name() const override;

  /**
   * @brief 获取实例标识，因为继承这个类型的都是单例，这个用来区分类型
   */
  uintptr_t get_instance_ident() const;

  /**
   * @brief 接收消息回调接口，通常会尝试恢复协程任务运行或创建一个协程任务
   * @param msg 消息数据原始信息
   * @param msg_buf 数据地址
   * @param msg_size 数据长度
   * @return 返回错误码或0
   */
  virtual dispatcher_result_t on_receive_message(rpc::context &ctx, msg_raw_t &msg, void *priv_data, uint64_t sequence);

  /**
   * @brief 接收消息回调接口，指定要检查的消息类型id，通常会尝试恢复协程任务运行或创建一个协程任务
   * @param msg 消息数据原始信息
   * @param expect_msg_type 消息类型id
   * @param msg_buf 数据地址
   * @param msg_size 数据长度
   * @return 返回错误码或0
   */
  virtual dispatcher_result_t on_receive_message(rpc::context &ctx, msg_raw_t &msg, uintptr_t expect_msg_type,
                                                 void *priv_data, uint64_t sequence);

  /**
   * @brief 发送消息消息失败的通知接口，通常会尝试填充错误码后恢复协程任务
   * @param msg 消息数据原始信息
   * @param msg_buf 数据地址
   * @param msg_size 数据长度
   * @param error_code 数据长度
   * @return 返回错误码或0
   */
  virtual int32_t on_send_message_failed(rpc::context &ctx, msg_raw_t &msg, int32_t error_code, uint64_t sequence);

  /**
   * @brief 创建任务(包括Actor)失败事件
   * @param start_data 启动数据
   * @param error_code 错误码
   */
  virtual void on_create_task_failed(start_data_t &start_data, int32_t error_code);

  /**
   * @brief 数据解包
   * @param real_msg 实际的消息结构
   * @param raw_msg 消息抽象结构
   * @param msg_buf 数据地址
   * @param msg_size 数据长度
   * @return 返回错误码或0
   */
  template <typename TMsg>
  int32_t unpack_protobuf_msg(TMsg &real_msg, msg_raw_t &raw_msg, const void *msg_buf, size_t msg_size);

  template <typename TMsg>
  TMsg *get_protobuf_msg(msg_raw_t &raw_msg);

  template <typename TMsg>
  TMsg *get_protobuf_msg(msg_raw_t &raw_msg, uintptr_t check_msg_type);

  /**
   * @brief 获取任务信息
   * @param raw_msg 消息抽象结构
   * @return 相关的任务id
   */
  virtual uint64_t pick_msg_task_id(msg_raw_t &raw_msg) = 0;

  /**
   * @brief 获取消息类型ID
   * @param raw_msg 消息抽象结构
   * @return 消息类型ID
   */
  virtual msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) = 0;

  /**
   * @brief 获取消息的RPC名字
   * @param raw_msg 消息抽象结构
   * @return 消息的RPC名字,如果不是RPC消息，返回空字符串
   */
  virtual const std::string &pick_rpc_name(msg_raw_t &raw_msg) = 0;

  /**
   * @brief 获取操作类型
   * @param raw_msg 消息抽象结构
   * @note 这只是一个调度曾规范，不强制执行。详情 @see PROJECT_NAMESPACE_ID::EnMsgOpType
   * @return 消息操作类型
   */
  virtual msg_op_type_t pick_msg_op_type(msg_raw_t &raw_msg) = 0;

  /**
   * @brief 创建协程任务
   * @param raw_msg 消息抽象结构
   * @param task_id 相关的任务id
   * @return 返回错误码或0
   */
  virtual int create_task(start_data_t &start_data, task_manager::id_t &task_id);

  /**
   * @brief 创建Actor
   * @param raw_msg 消息抽象结构
   * @return 返回错误码或0
   */
  virtual task_manager::actor_action_ptr_t create_actor(start_data_t &start_data);

  /**
   * @brief 根据类型ID获取action或actor选项
   * @param raw_msg 消息抽象结构
   * @return 返回action或actor选项或NULL
   */
  virtual const atframework::DispatcherOptions *get_options_by_message_type(msg_type_t msg_type);

  /**
   * @brief 注册Action
   * @param msg_type 消息类型ID
   * @return 或错误码
   */
  template <typename TAction>
  inline int register_action(msg_type_t msg_type) {
    const atframework::DispatcherOptions *options = get_options_by_message_type(msg_type);
    return _register_action(msg_type, task_manager::me()->make_task_creator<TAction>(options));
  }

  /**
   * @brief 注册Action
   * @param rpc_full_name 注册的RPC完整名称
   * @return 或错误码
   */
  template <typename TAction>
  int register_action(const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::ServiceDescriptor *service_desc,
                      const std::string &rpc_name) {
    if (nullptr == service_desc) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
    }
    registered_service_[service_desc->full_name()] = service_desc;

    std::string::size_type final_segment = rpc_name.find_last_of('.');
    std::string rpc_short_name;
    if (std::string::npos == final_segment) {
      rpc_short_name = rpc_name;
    } else {
      rpc_short_name = rpc_name.substr(final_segment + 1);
    }
    const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method =
        service_desc->FindMethodByName(rpc_short_name);
    if (nullptr == method) {
      FWLOGERROR("{} try to register rpc action {} for service {} failed, not found", name(), rpc_name,
                 service_desc->full_name());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
    registered_method_[method->full_name()] = method;

    if (method->full_name() != rpc_name) {
      FWLOGERROR("{} try to register rpc action {} for service {} failed, the real full name is {}", name(), rpc_name,
                 service_desc->full_name(), method->full_name());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }

    const atframework::DispatcherOptions *options = nullptr;
    if (method->options().HasExtension(atframework::rpc_options)) {
      options = &method->options().GetExtension(atframework::rpc_options);
    }

    return _register_action(method->full_name(), task_manager::me()->make_task_creator<TAction>(options));
  }

  /**
   * @brief 注册Action
   * @param msg_type 消息类型ID
   * @return 或错误码
   */
  template <typename TAction>
  inline int register_actor(msg_type_t msg_type) {
    const atframework::DispatcherOptions *options = get_options_by_message_type(msg_type);
    return _register_action(msg_type, task_manager::me()->make_actor_creator<TAction>(options));
  }

  /**
   * @brief 注册Action
   * @param rpc_full_name 注册的RPC完整名称
   * @return 或错误码
   */
  template <typename TAction>
  int register_actor(const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::ServiceDescriptor *service_desc,
                     const std::string &rpc_name) {
    if (nullptr == service_desc) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
    }
    registered_service_[service_desc->full_name()] = service_desc;

    std::string::size_type final_segment = rpc_name.find_last_of('.');
    std::string rpc_short_name;
    if (std::string::npos == final_segment) {
      rpc_short_name = rpc_name;
    } else {
      rpc_short_name = rpc_name.substr(final_segment + 1);
    }
    const ::ATBUS_MACRO_PROTOBUF_NAMESPACE_ID::MethodDescriptor *method =
        service_desc->FindMethodByName(rpc_short_name);
    if (nullptr == method) {
      FWLOGERROR("{} try to register rpc actor {} for service {} failed, not found", name(), rpc_name,
                 service_desc->full_name());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
    registered_method_[method->full_name()] = method;

    if (method->full_name() != rpc_name) {
      FWLOGERROR("{} try to register rpc action {} for service {} failed, the real full name is {}", name(), rpc_name,
                 service_desc->full_name(), method->full_name());
      return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }

    const atframework::DispatcherOptions *options = nullptr;
    if (method->options().HasExtension(atframework::rpc_options)) {
      options = &method->options().GetExtension(atframework::rpc_options);
    }

    return _register_action(method->full_name(), task_manager::me()->make_actor_creator<TAction>(options));
  }

  /**
   * @brief 添加前置过滤器
   * @param fn 函数或仿函数
   * @note 被添加的过滤器会先执行
   */
  void push_filter_to_front(msg_filter_handle_t fn);

  /**
   * @brief 添加后置过滤器
   * @param fn 函数或仿函数
   * @note 被添加的过滤器会最后执行
   */
  void push_filter_to_back(msg_filter_handle_t fn);

  /**
   * @brief If is closing
   * @return true if it's closing
   */
  bool is_closing() const noexcept;

  /**
   * @brief Try to get registered service descriptor by full name
   * @param service_full_name service full name
   * @return service descriptor or nullptr if not found
   */
  rpc_service_set_t::mapped_type get_registered_service(const std::string &service_full_name) const noexcept;

  /**
   * @brief Try to get registered method descriptor by full name
   * @param method_full_name method full name
   * @return method descriptor or nullptr if not found
   */
  rpc_method_set_t::mapped_type get_registered_method(const std::string &method_full_name) const noexcept;

 protected:
  const std::string &get_empty_string();

 private:
  int _register_action(msg_type_t msg_type, task_manager::task_action_creator_t action);
  int _register_action(msg_type_t msg_type, task_manager::actor_action_creator_t action);
  int _register_action(const std::string &rpc_full_name, task_manager::task_action_creator_t action);
  int _register_action(const std::string &rpc_full_name, task_manager::actor_action_creator_t action);

 private:
  msg_task_action_set_t task_action_map_by_id_;
  msg_actor_action_set_t actor_action_map_by_id_;
  rpc_task_action_set_t task_action_map_by_name_;
  rpc_actor_action_set_t actor_action_map_by_name_;
  rpc_service_set_t registered_service_;
  rpc_method_set_t registered_method_;

  std::list<msg_filter_handle_t> msg_filter_list_;
  mutable std::string human_readable_name_;
};

template <typename TMsg>
int32_t dispatcher_implement::unpack_protobuf_msg(TMsg &real_msg, msg_raw_t &raw_msg, const void *msg_buf,
                                                  size_t msg_size) {
  raw_msg.msg_addr = nullptr;
  raw_msg.msg_type = get_instance_ident();

  if (nullptr == msg_buf || 0 == msg_size) {
    FWLOGERROR("{} try to parameter error, nullptr == msg_buf or 0 == msg_size", name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (false == real_msg.ParseFromArray(msg_buf, static_cast<int>(msg_size))) {
    FWLOGERROR("{} unpack msg for type={} failed\n{}", name(), get_instance_ident(),
               real_msg.InitializationErrorString().c_str());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }

  raw_msg.msg_addr = &real_msg;

  if (nullptr != WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT) &&
      WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)
          ->check_level(util::log::log_wrapper::level_t::LOG_LW_DEBUG)) {
    FWLOGDEBUG("{}(type={}) recv msg.\n{}", name(), get_instance_ident(), protobuf_mini_dumper_get_readable(real_msg));
  }

  return 0;
}

template <typename TMsg>
TMsg *dispatcher_implement::get_protobuf_msg(msg_raw_t &raw_msg) {
  if (get_instance_ident() != raw_msg.msg_type) {
    return nullptr;
  }

  return reinterpret_cast<TMsg *>(raw_msg.msg_addr);
}

template <typename TMsg>
TMsg *dispatcher_implement::get_protobuf_msg(msg_raw_t &raw_msg, uintptr_t check_msg_type) {
  if (0 != check_msg_type && check_msg_type != raw_msg.msg_type) {
    return NULL;
  }

  return reinterpret_cast<TMsg *>(raw_msg.msg_addr);
}

#define REG_TASK_MSG_HANDLE(dispatcher, ret, act, proto) \
  if (ret < 0) {                                         \
    dispatcher::me()->register_action<act>(proto);       \
  } else {                                               \
    ret = dispatcher::me()->register_action<act>(proto); \
  }

#define REG_ACTOR_MSG_HANDLE(dispatcher, ret, act, proto) \
  if (ret < 0) {                                          \
    dispatcher::me()->register_actor<act>(proto);         \
  } else {                                                \
    ret = dispatcher::me()->register_actor<act>(proto);   \
  }

#define REG_TASK_RPC_HANDLE(dispatcher, ret, act, service_desc, rpc_name) \
  if (ret < 0) {                                                          \
    dispatcher::me()->register_action<act>(service_desc, rpc_name);       \
  } else {                                                                \
    ret = dispatcher::me()->register_action<act>(service_desc, rpc_name); \
  }

#define REG_ACTOR_RPC_HANDLE(dispatcher, ret, act, service_desc, rpc_name) \
  if (ret < 0) {                                                           \
    dispatcher::me()->register_actor<act>(service_desc, rpc_name);         \
  } else {                                                                 \
    ret = dispatcher::me()->register_actor<act>(service_desc, rpc_name);   \
  }

#endif  // ATF4G_CO_DISPATCHER_IMPLEMENT_H
