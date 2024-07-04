// Copyright 2024 atframework
// Created by owent

#include "memory/object_allocator_metrics.h"

#include <log/log_wrapper.h>

// for demangle
#ifdef __GLIBCXX__
#  include <cxxabi.h>
#  define USING_LIBSTDCXX_ABI 1
#elif defined(_LIBCPP_ABI_VERSION)
#  include <cxxabi.h>
#  define USING_LIBCXX_ABI 1
#endif

#include <mutex>
#include <unordered_map>

namespace atframework {
namespace memory {

namespace {

struct UTIL_SYMBOL_LOCAL object_allocator_metrics_global_cache {
  std::mutex lock;

  std::unordered_map<std::string, object_allocator_metrics> allocator_metrics;
};

static object_allocator_metrics_global_cache& get_statistics_set() {
  static object_allocator_metrics_global_cache global_instance;
  return global_instance;
}

static const char* skip_space(const char* input) {
  if (nullptr == input) {
    return nullptr;
  }

  while (*input && (' ' == *input || '\t' == *input || '\r' == *input || '\n' == *input)) {
    ++input;
  }

  return input;
}

static const char* find_char(const char* input, const char c) {
  if (nullptr == input) {
    return nullptr;
  }

  while (*input && *input != c) {
    ++input;
  }

  return input;
}

}  // namespace

ATFRAME_SERVICE_COMPONENT_MACRO_API ::std::string object_allocator_metrics_controller::try_parse_raw_name(
    const char* input) {
  if (nullptr == input) {
    return {};
  }

  if (!*input) {
    return {};
  }

  return input;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API ::std::string object_allocator_metrics_controller::try_parse_demangle_name(
    const char* input) {
  if (nullptr == input) {
    return {};
  }

  if (!*input) {
    return {};
  }

  const char* start = input;
  while (*start) {
    if (*start == '<' || *start == '[') {
      break;
    }

    ++start;
  }

  // Unknown pretty name, use origin for fallback
  do {
    if (!*start) {
      break;
    }

    // Parse guess_pretty_name() [with T = XXX]
    const char open_symbol = *start;
    const char close_symbol = open_symbol == '[' ? ']' : '>';

    const char* begin;
    if (*start == '[') {
      const char* find_eq = find_char(start, '=');
      if (find_eq && *find_eq == '=') {
        begin = skip_space(find_eq + 1);
      } else {
        begin = skip_space(start + 1);
      }
    } else {
      // Parse guess_pretty_name()<XXX>(void)
      begin = skip_space(start + 1);
    }

    size_t depth = 1;
    const char* end = begin;
    while (*end) {
      if (*end == open_symbol) {
        ++depth;
      } else if (*end == close_symbol) {
        --depth;
        if (depth <= 0) {
          break;
        }
      }
      ++end;
    }

    if (end > begin) {
      return std::string{begin, end};
    }
  } while (false);

  std::string fallback = input;
  std::string::size_type sidx = fallback.find("guess_pretty_name");
  if (std::string::npos != sidx) {
    return fallback.substr(sidx + 17);
  }

  return fallback;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API object_allocator_metrics*
object_allocator_metrics_controller::mutable_object_allocator_metrics_for_type(::std::string raw_name,
                                                                               ::std::string demangle_name,
                                                                               size_t unit_size) {
  if (raw_name.empty() && !demangle_name.empty()) {
    raw_name = demangle_name;
  } else if (demangle_name.empty() && !raw_name.empty()) {
#if defined(USING_LIBSTDCXX_ABI) || defined(USING_LIBCXX_ABI)
    int cxx_abi_status;
    char* realfunc_name = abi::__cxa_demangle(raw_name.c_str(), 0, 0, &cxx_abi_status);
    if (nullptr != realfunc_name) {
      demangle_name = realfunc_name;
      free(realfunc_name);
    } else {
      demangle_name = raw_name;
    }
#else
    demangle_name = raw_name;
#endif
  }

  if (raw_name.empty() && demangle_name.empty()) {
    demangle_name = util::log::format("<UNKNOWN TYPE> of size {}", unit_size);
    raw_name = demangle_name;
  }

  auto& statistics_data = get_statistics_set();
  std::lock_guard<std::mutex> lock_guard{statistics_data.lock};

  auto iter = statistics_data.allocator_metrics.find(raw_name);
  if (iter != statistics_data.allocator_metrics.end()) {
    return &iter->second;
  }

  object_allocator_metrics& ret = statistics_data.allocator_metrics[raw_name];
  ret.raw_name = raw_name;
  ret.demangle_name = demangle_name;
  ret.unit_size = unit_size;

  return &ret;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API void object_allocator_metrics_controller::add_constructor_counter(
    object_allocator_metrics* target, void*) {
  if (nullptr == target) {
    return;
  }

  target->constructor_counter += 1;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API void object_allocator_metrics_controller::add_allocate_counter(
    object_allocator_metrics* target, size_t count) {
  if (nullptr == target) {
    return;
  }

  target->allocate_counter += count;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API void object_allocator_metrics_controller::add_destructor_counter(
    object_allocator_metrics* target, void*) {
  if (nullptr == target) {
    return;
  }

  target->destructor_counter += 1;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API void object_allocator_metrics_controller::add_deallocate_counter(
    object_allocator_metrics* target, size_t count) {
  if (nullptr == target) {
    return;
  }

  target->deallocate_counter += count;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API void object_allocator_metrics_controller::foreach_object_statistics(
    util::nostd::function_ref<void(const object_allocator_metrics&)> fn) {
  auto& statistics_data = get_statistics_set();
  std::lock_guard<std::mutex> lock_guard{statistics_data.lock};

  for (auto& data : statistics_data.allocator_metrics) {
    fn(data.second);
  }
}

}  // namespace memory
}  // namespace atframework
