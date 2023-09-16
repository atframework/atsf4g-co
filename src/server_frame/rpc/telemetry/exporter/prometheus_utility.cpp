// Copyright 2023 atframework
// Created by owent on 2023/07/19.
//

#include "rpc/telemetry/exporter/prometheus_utility.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

SERVER_FRAME_API std::string PrometheusUtility::SanitizePrometheusName(std::string name, bool label) {
  constexpr const auto replacement = '_';
  constexpr const auto replacement_dup = '=';

  bool (*valid)(std::size_t, char);
  if (label) {
    valid = [](std::size_t i, char c) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9' && i > 0)) {
        return true;
      }
      return false;
    };
  } else {
    valid = [](std::size_t i, char c) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ':' || (c >= '0' && c <= '9' && i > 0)) {
        return true;
      }
      return false;
    };
  }

  bool has_dup = false;
  for (size_t i = 0; i < name.size(); ++i) {
    if (valid(i, name[i])) {
      continue;
    }
    if (i > 0 && (name[i - 1] == replacement || name[i - 1] == replacement_dup)) {
      has_dup = true;
      name[i] = replacement_dup;
    } else {
      name[i] = replacement;
    }
  }
  if (has_dup) {
    auto end = std::remove(name.begin(), name.end(), replacement_dup);
    return std::string{name.begin(), end};
  }

  return name;
}

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc
