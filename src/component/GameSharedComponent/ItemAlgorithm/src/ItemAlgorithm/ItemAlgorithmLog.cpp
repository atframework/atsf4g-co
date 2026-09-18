// Copyright 2026 atframework

#include <ItemAlgorithm/ItemAlgorithmLog.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

void ItemLogInstance::dispatch(const caller_info_t& caller, std::string&& message) const {
  if (nullptr == handler_ || nullptr == handler_->on_log) {
    return;
  }

  // message 是本函数的参数, on_log 它在回调期间一直有效, 所以记录里可以直接用视图。
  // gsl::string_view 在部分构建里是 std::string_view, 而 caller_info_t 用框架自己的 nostd::string_view,
  // 两者在部分配置下不同型, 所以统一用 (data, size) 构造
  ItemLogRecord record;
  record.level = caller.level_id;
  record.file_path = gsl::string_view(caller.file_path.data(), caller.file_path.size());
  record.line_number = caller.line_number;
  record.category = handler_->category;
  record.message = message;
  handler_->on_log(record);
}

#if !defined(ATFRAMEWORK_UTILS_STRING_ENABLE_FWAPI) || !ATFRAMEWORK_UTILS_STRING_ENABLE_FWAPI
void ItemLogInstance::log(const caller_info_t& caller, const char* fmt_text, ...) const {
  if (nullptr == handler_ || nullptr == handler_->on_log) {
    return;
  }

  char buffer[1024];
  va_list args;
  va_start(args, fmt_text);
  int result = vsnprintf(buffer, sizeof(buffer), fmt_text, args);
  va_end(args);

  std::string message;
  if (result > 0) {
    size_t len = static_cast<size_t>(result);
    if (len >= sizeof(buffer)) {
      len = sizeof(buffer) - 1;
    }
    message.assign(buffer, len);
  }
  dispatch(caller, std::move(message));
}
#endif

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
