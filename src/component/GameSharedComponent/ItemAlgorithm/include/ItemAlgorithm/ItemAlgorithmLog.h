// Copyright 2026 atframework

#pragma once

#include "ItemAlgorithm/ItemAlgorithmConfig.h"

// clang-format off
#include <log/log_wrapper.h>
// clang-format on

#include <cstdint>
#include <functional>
#include <string>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

enum class ItemLogLevel : uint8_t { kDebug = 0, kInfo = 1, kWarning = 2, kError = 3 };

struct ItemLogRecord {
  ItemLogLevel level = ItemLogLevel::kInfo;
  const char* file_name = nullptr;
  int line_number = 0;
  std::string category;
  std::string message;
};

using ItemLogCallback = std::function<void(const ItemLogRecord& record)>;

struct ItemLogHandler {
  std::string category = "ItemAlgorithm";
  ItemLogCallback on_log = nullptr;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END

// ============================================================
// 道具算法日志调用宏
// 需在 ItemGridAlgorithm 或其子类的成员函数内使用 (通过成员 log() 输出)
// ============================================================
#define ITEM_ALGORITHM_LOG(level, message) log((level), __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_FMT(level, ...) log((level), __FILE__, __LINE__, LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_DEBUG(message) \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kDebug, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_INFO(message) \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kInfo, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_WARNING(message) \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kWarning, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_ERROR(message) \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kError, __FILE__, __LINE__, (message))
#define ITEM_ALGORITHM_LOG_DEBUG_FMT(...)                                                      \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kDebug, __FILE__, __LINE__, \
      LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_INFO_FMT(...)                                                      \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kInfo, __FILE__, __LINE__, \
      LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_WARNING_FMT(...)                                                      \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kWarning, __FILE__, __LINE__, \
      LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
#define ITEM_ALGORITHM_LOG_ERROR_FMT(...)                                                      \
  log(::ITEM_ALGORITHM_NAMESPACE_ID::item_algorithm::ItemLogLevel::kError, __FILE__, __LINE__, \
      LOG_WRAPPER_FWAPI_FORMAT(__VA_ARGS__))
