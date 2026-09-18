// Copyright 2026 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <log/log_wrapper.h>
// clang-format on

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 日志级别直接复用框架的 log_level, 便于和 FWINSTLOG* 对接
using ItemLogLevel = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_level;
using ItemLogCallerInfo = ::ATFRAMEWORK_UTILS_NAMESPACE_ID::log::log_formatter::caller_info_t;

/// @brief 一条日志记录 (交给上层日志处理器)
///
/// 所有文本字段都是视图, 不持有数据: 只在本次 on_log 回调执行期间有效。
/// 需要长期保存请自行拷成 std::string。
struct ATFW_UTIL_SYMBOL_VISIBLE ItemLogRecord {
  ItemLogLevel level = ItemLogLevel::kInfo;
  gsl::string_view file_path;
  uint32_t line_number = 0;
  gsl::string_view category;
  gsl::string_view message;
};

using ItemLogCallback = std::function<void(const ItemLogRecord& record)>;

/// @brief 日志处理器 (由使用方注册, 决定日志最终写到哪里)
struct ATFW_UTIL_SYMBOL_VISIBLE ItemLogHandler {
  /// @brief 日志分类名
  ///
  /// 注册时容器会把它复制到自己的存储 (见 ItemContainer::set_log_handler), 因此只需保证它在
  /// set_log_handler 调用期间有效; 容器初始化时会补成 "<category>:<容器GUID>"。
  gsl::string_view category = "ItemAlgorithm";
  /// @brief 低于该级别的日志不会格式化, 也不会回调 on_log
  ItemLogLevel min_level = ItemLogLevel::kTrace;
  /// @brief 未注册时所有日志被忽略
  ItemLogCallback on_log = nullptr;
};

/// @brief 供 FWINSTLOG* 使用的日志实例 (与 log_wrapper 同接口的适配层)
///
/// 容器自身与三种模式容器的代码直接用框架日志宏输出, 例如:
///
///   FWINSTLOGERROR(logger(), "check_add failed: type={} count={}", type_id, count);
///
/// 日志模块不再额外定义宏; 级别命名沿用 log_wrapper.h 的
/// FWINSTLOGTRACE / FWINSTLOGDEBUG / FWINSTLOGNOTICE / FWINSTLOGINFO /
/// FWINSTLOGWARNING / FWINSTLOGERROR / FWINSTLOGFATAL。
/// 格式化后的消息通过 ItemLogHandler::on_log 交给使用方。
class ATFW_UTIL_SYMBOL_VISIBLE ItemLogInstance {
 public:
  using log_level = ItemLogLevel;
  using caller_info_t = ItemLogCallerInfo;

  ItemLogInstance() noexcept = default;
  explicit ItemLogInstance(const ItemLogHandler* handler) noexcept : handler_(handler) {}

  /// @brief FWINSTLOG* 会先问这里, 再决定是否格式化
  bool check_level(log_level level) const noexcept {
    return nullptr != handler_ && nullptr != handler_->on_log && level >= handler_->min_level;
  }

#if defined(ATFRAMEWORK_UTILS_STRING_ENABLE_FWAPI) && ATFRAMEWORK_UTILS_STRING_ENABLE_FWAPI
  template <class... TARGS>
  void format_log(const caller_info_t& caller,
                  ::ATFRAMEWORK_UTILS_NAMESPACE_ID::string::details::fmtapi_format_string_t<char, TARGS...> fmt_text,
                  TARGS&&... args) const {
    dispatch(caller, ::ATFRAMEWORK_UTILS_NAMESPACE_ID::string::format(fmt_text, std::forward<TARGS>(args)...));
  }
#else
  /// @brief 未启用格式化库时的 printf 风格入口 (FWINSTLOG* 在该配置下也走这个)
  void log(const caller_info_t& caller, const char* fmt_text, ...) const;
#endif

 private:
  void dispatch(const caller_info_t& caller, std::string&& message) const;

 private:
  const ItemLogHandler* handler_ = nullptr;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
