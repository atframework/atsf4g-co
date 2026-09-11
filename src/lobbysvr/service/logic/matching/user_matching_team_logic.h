// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>

#include <data/user_type_define.h>

#include <functional>

namespace rpc {
class context;
}

class user;

// 匹配与组队模块之间的适配层。队伍状态只从 user_team_manager 读取，不在此处重复保存。
class user_matching_team_logic : public atfw::util::design_pattern::noncopyable {
 public:
  using start_matching_check_function_t = std::function<void(rpc::context&, const user_ptr_t& user_inst)>;

  using matching_finish_function_t = std::function<void(rpc::context&, const user_ptr_t& user_inst)>;

  using start_matching_finish_function_t = std::function<void(rpc::context&, const user_ptr_t& user_inst)>;

  using level_select_function_t = std::function<void(rpc::context&, const user_ptr_t& user_inst)>;

  // 开启匹配前的检查
  static void register_start_matching_check_function(start_matching_check_function_t function);

  // 匹配结束后的通知
  static void register_matching_finish_function(matching_finish_function_t function);

  // 匹配开始后的通知
  static void register_start_matching_finish_function(start_matching_finish_function_t function);

  // 关卡选择后的通知
  static void register_level_select_function(level_select_function_t function);
  void start_matching_check(rpc::context& ctx);
  void matching_finish(rpc::context& ctx);
  void start_matching_finish(rpc::context& ctx);
  void level_select_notify(rpc::context& ctx);

  explicit user_matching_team_logic(user& owner) noexcept;

  bool is_in_team() const noexcept;

  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingTeamParameter>
  get_team_member_matching_team_parameter(rpc::context& ctx) const;

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  static start_matching_check_function_t start_matching_check_function_;
  static matching_finish_function_t matching_finish_function_;
  static start_matching_finish_function_t start_matching_finish_function_;
  static level_select_function_t level_select_function_;
};
