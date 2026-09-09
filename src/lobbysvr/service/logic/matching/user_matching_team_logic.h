// Copyright 2026 atframework

#pragma once

#include <design_pattern/noncopyable.h>
#include <nostd/nullability.h>

class user;

// 匹配与组队模块之间的适配层。队伍状态只从 user_team_manager 读取，不在此处重复保存。
class user_matching_team_logic : public atfw::util::design_pattern::noncopyable {
 public:
  using start_matching_check_function_t = std::function<void(rpc::context&)>;

  using matching_finish_function_t = std::function<void(rpc::context&)>;

  using start_matching_finish_function_t = std::function<void(rpc::context&)>;

  // 开启匹配前的检查
  void register_start_matching_check_function(start_matching_check_function_t function);

  // 匹配结束后的通知
  void register_matching_finish_function(matching_finish_function_t function);

  // 匹配开始后的通知
  void register_start_matching_finish_function(start_matching_finish_function_t function);

  void start_matching_check(rpc::context& ctx);
  void matching_finish(rpc::context& ctx);
  void start_matching_finish(rpc::context& ctx);

  explicit user_matching_team_logic(user& owner) noexcept;

  bool is_in_team() const noexcept;

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
  start_matching_check_function_t start_matching_check_function_;
  matching_finish_function_t matching_finish_function_;
  start_matching_finish_function_t start_matching_finish_function_;
};
