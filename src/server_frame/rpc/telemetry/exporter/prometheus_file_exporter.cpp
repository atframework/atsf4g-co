// Copyright 2023 atframework
// Created by owent on 2023-09-14.
//

#include "rpc/telemetry/exporter/prometheus_file_exporter.h"

#include <config/atframe_utils_build_feature.h>

#include <log/log_wrapper.h>

#include <prometheus/labels.h>
#include <prometheus/text_serializer.h>

#include <memory/object_allocator.h>

#include <opentelemetry/common/macros.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/sdk/common/global_log_handler.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>
#if ATFRAMEWORK_UTILS_ENABLE_EXCEPTION
#  include <exception>
#endif

#if !defined(__CYGWIN__) && defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

#  include <Windows.h>
#  include <direct.h>
#  include <io.h>

#  ifdef UNICODE
#    include <atlconv.h>
#    define VC_TEXT(x) A2W(x)
#  else
#    define VC_TEXT(x) x
#  endif

#  define FS_ACCESS(x) _access(x, 0)
#  define SAFE_STRTOK_S(...) strtok_s(__VA_ARGS__)
#  define FS_MKDIR(path, mode) _mkdir(path)

#else

#  include <dirent.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>

#  define FS_ACCESS(x) access(x, F_OK)
#  define SAFE_STRTOK_S(...) strtok_r(__VA_ARGS__)
#  define FS_MKDIR(path, mode) ::mkdir(path, mode)

#  if defined(__ANDROID__)
#    define FS_DISABLE_LINK 1
#  elif defined(__APPLE__)
#    if __dest_os != __mac_os_x
#      define FS_DISABLE_LINK 1
#    endif
#  endif

#endif

#include "rpc/telemetry/exporter/prometheus_utility.h"

#ifdef _MSC_VER
#  define strcasecmp _stricmp
#endif

#if (defined(_MSC_VER) && _MSC_VER >= 1600) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || \
    defined(__STDC_LIB_EXT1__)
#  ifdef _MSC_VER
#    define PROMETHEUS_FILE_SNPRINTF(buffer, bufsz, ...) sprintf_s(buffer, static_cast<size_t>(bufsz), __VA_ARGS__)
#  else
#    define PROMETHEUS_FILE_SNPRINTF(buffer, bufsz, fmt, args...) \
      snprintf_s(buffer, static_cast<rsize_t>(bufsz), fmt, ##args)
#  endif
#else
#  define PROMETHEUS_FILE_SNPRINTF(buffer, bufsz, fmt, args...) \
    snprintf(buffer, static_cast<size_t>(bufsz), fmt, ##args)
#endif

namespace rpc {
namespace telemetry {
namespace exporter {
namespace metrics {

namespace {

static std::tm GetLocalTime() {
  std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || defined(__STDC_LIB_EXT1__)
  std::tm ret;
  localtime_s(&now, &ret);
#elif defined(_MSC_VER) && _MSC_VER >= 1300
  std::tm ret;
  localtime_s(&ret, &now);
#elif defined(_XOPEN_SOURCE) || defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || defined(_POSIX_SOURCE)
  std::tm ret;
  localtime_r(&now, &ret);
#else
  std::tm ret = *localtime(&now);
#endif
  return ret;
}

static std::size_t FormatPath(char *buff, size_t bufz, opentelemetry::nostd::string_view fmt,
                              std::size_t rotate_index) {
  if (nullptr == buff || 0 == bufz) {
    return 0;
  }

  if (fmt.empty()) {
    buff[0] = '\0';
    return 0;
  }

  bool need_parse = false;
  bool running = true;
  std::size_t ret = 0;
  std::tm tm_obj_cache;
  std::tm *tm_obj_ptr = nullptr;

#define LOG_FMT_FN_TM_MEM(VAR, EXPRESS) \
                                        \
  int VAR;                              \
                                        \
  if (nullptr == tm_obj_ptr) {          \
    tm_obj_cache = GetLocalTime();      \
    tm_obj_ptr = &tm_obj_cache;         \
    VAR = tm_obj_ptr->EXPRESS;          \
                                        \
  } else {                              \
    VAR = tm_obj_ptr->EXPRESS;          \
  }

  for (size_t i = 0; i < fmt.size() && ret < bufz && running; ++i) {
    if (!need_parse) {
      if ('%' == fmt[i]) {
        need_parse = true;
      } else {
        buff[ret++] = fmt[i];
      }
      continue;
    }

    need_parse = false;
    switch (fmt[i]) {
      // =================== datetime ===================
      case 'Y': {
        if (bufz - ret < 4) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(year, tm_year + 1900);
          buff[ret++] = static_cast<char>(year / 1000 + '0');
          buff[ret++] = static_cast<char>((year / 100) % 10 + '0');
          buff[ret++] = static_cast<char>((year / 10) % 10 + '0');
          buff[ret++] = static_cast<char>(year % 10 + '0');
        }
        break;
      }
      case 'y': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(year, tm_year + 1900);
          buff[ret++] = static_cast<char>((year / 10) % 10 + '0');
          buff[ret++] = static_cast<char>(year % 10 + '0');
        }
        break;
      }
      case 'm': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(mon, tm_mon + 1);
          buff[ret++] = static_cast<char>(mon / 10 + '0');
          buff[ret++] = static_cast<char>(mon % 10 + '0');
        }
        break;
      }
      case 'j': {
        if (bufz - ret < 3) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(yday, tm_yday);
          buff[ret++] = static_cast<char>(yday / 100 + '0');
          buff[ret++] = static_cast<char>((yday / 10) % 10 + '0');
          buff[ret++] = static_cast<char>(yday % 10 + '0');
        }
        break;
      }
      case 'd': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(mday, tm_mday);
          buff[ret++] = static_cast<char>(mday / 10 + '0');
          buff[ret++] = static_cast<char>(mday % 10 + '0');
        }
        break;
      }
      case 'w': {
        LOG_FMT_FN_TM_MEM(wday, tm_wday);
        buff[ret++] = static_cast<char>(wday + '0');
        break;
      }
      case 'H': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(hour, tm_hour);
          buff[ret++] = static_cast<char>(hour / 10 + '0');
          buff[ret++] = static_cast<char>(hour % 10 + '0');
        }
        break;
      }
      case 'I': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(hour, tm_hour % 12 + 1);
          buff[ret++] = static_cast<char>(hour / 10 + '0');
          buff[ret++] = static_cast<char>(hour % 10 + '0');
        }
        break;
      }
      case 'M': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(minite, tm_min);
          buff[ret++] = static_cast<char>(minite / 10 + '0');
          buff[ret++] = static_cast<char>(minite % 10 + '0');
        }
        break;
      }
      case 'S': {
        if (bufz - ret < 2) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(sec, tm_sec);
          buff[ret++] = static_cast<char>(sec / 10 + '0');
          buff[ret++] = static_cast<char>(sec % 10 + '0');
        }
        break;
      }
      case 'F': {
        if (bufz - ret < 10) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(year, tm_year + 1900);
          LOG_FMT_FN_TM_MEM(mon, tm_mon + 1);
          LOG_FMT_FN_TM_MEM(mday, tm_mday);
          buff[ret++] = static_cast<char>(year / 1000 + '0');
          buff[ret++] = static_cast<char>((year / 100) % 10 + '0');
          buff[ret++] = static_cast<char>((year / 10) % 10 + '0');
          buff[ret++] = static_cast<char>(year % 10 + '0');
          buff[ret++] = '-';
          buff[ret++] = static_cast<char>(mon / 10 + '0');
          buff[ret++] = static_cast<char>(mon % 10 + '0');
          buff[ret++] = '-';
          buff[ret++] = static_cast<char>(mday / 10 + '0');
          buff[ret++] = static_cast<char>(mday % 10 + '0');
        }
        break;
      }
      case 'T': {
        if (bufz - ret < 8) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(hour, tm_hour);
          LOG_FMT_FN_TM_MEM(minite, tm_min);
          LOG_FMT_FN_TM_MEM(sec, tm_sec);
          buff[ret++] = static_cast<char>(hour / 10 + '0');
          buff[ret++] = static_cast<char>(hour % 10 + '0');
          buff[ret++] = ':';
          buff[ret++] = static_cast<char>(minite / 10 + '0');
          buff[ret++] = static_cast<char>(minite % 10 + '0');
          buff[ret++] = ':';
          buff[ret++] = static_cast<char>(sec / 10 + '0');
          buff[ret++] = static_cast<char>(sec % 10 + '0');
        }
        break;
      }
      case 'R': {
        if (bufz - ret < 5) {
          running = false;
        } else {
          LOG_FMT_FN_TM_MEM(hour, tm_hour);
          LOG_FMT_FN_TM_MEM(minite, tm_min);
          buff[ret++] = static_cast<char>(hour / 10 + '0');
          buff[ret++] = static_cast<char>(hour % 10 + '0');
          buff[ret++] = ':';
          buff[ret++] = static_cast<char>(minite / 10 + '0');
          buff[ret++] = static_cast<char>(minite % 10 + '0');
        }
        break;
      }

      // =================== rotate index ===================
      case 'n':
      case 'N': {
        std::size_t value = fmt[i] == 'n' ? rotate_index + 1 : rotate_index;
        auto res = PROMETHEUS_FILE_SNPRINTF(&buff[ret], bufz - ret, "%llu", static_cast<unsigned long long>(value));
        if (res < 0) {
          running = false;
        } else {
          ret += static_cast<std::size_t>(res);
        }
        break;
      }

      // =================== unknown ===================
      default: {
        buff[ret++] = fmt[i];
        break;
      }
    }
  }

#undef LOG_FMT_FN_TM_MEM

  if (ret < bufz) {
    buff[ret] = '\0';
  } else {
    buff[bufz - 1] = '\0';
  }
  return ret;
}

class ATFW_UTIL_SYMBOL_LOCAL FileSystemUtil {
 public:
  // When LongPathsEnabled on Windows, it allow 32767 characters in a absolute path.But it still only allow 260
  // characters in a relative path.
  // See https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation

  static constexpr const std::size_t kMaxPathSize =
#if defined(MAX_PATH)
      MAX_PATH;
#elif defined(_MAX_PATH)
      _MAX_PATH;
#elif defined(PATH_MAX)
      PATH_MAX;
#else
      260;
#endif

  static constexpr const char kDirectorySeparator =
#if !defined(__CYGWIN__) && defined(_WIN32)
      '\\';
#else
      '/';
#endif

  static std::size_t GetFileSize(const char *file_path) {
    std::fstream file;
    file.open(file_path, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
      return 0;
    }

    file.seekg(std::ios::end);
    auto size = file.tellg();
    file.close();

    if (size > 0) {
      return static_cast<std::size_t>(size);
    } else {
      return 0;
    }
  }

  static std::string DirName(opentelemetry::nostd::string_view file_path, int depth = 1) {
    if (file_path.empty()) {
      return "";
    }

    std::size_t sz = file_path.size() - 1;

    while (sz > 0 && ('/' == file_path[sz] || '\\' == file_path[sz])) {
      --sz;
    }

    while (sz > 0 && depth > 0) {
      if ('/' == file_path[sz] || '\\' == file_path[sz]) {
        --depth;
      }

      if (depth <= 0) {
        break;
      }
      --sz;
    }

    return static_cast<std::string>(file_path.substr(0, sz));
  }

  static bool IsExist(const char *file_path) { return 0 == FS_ACCESS(file_path); }

  static std::vector<std::string> SplitPath(opentelemetry::nostd::string_view path, bool normalize = false) {
    std::vector<std::string> out;

    std::string path_buffer = static_cast<std::string>(path);

    char *saveptr = nullptr;
    char *token = SAFE_STRTOK_S(&path_buffer[0], "\\/", &saveptr);
    while (nullptr != token) {
      if (0 != strlen(token)) {
        if (normalize) {
          // Normalize path
          if (0 == strcmp("..", token)) {
            if (!out.empty() && out.back() != "..") {
              out.pop_back();
            } else {
              out.push_back(token);
            }
          } else if (0 != strcmp(".", token)) {
            out.push_back(token);
          }
        } else {
          out.push_back(token);
        }
      }
      token = SAFE_STRTOK_S(nullptr, "\\/", &saveptr);
    }

    return out;
  }

  static bool MkDir(const char *dir_path, bool recursion, OPENTELEMETRY_MAYBE_UNUSED int mode) {
#if !(!defined(__CYGWIN__) && defined(_WIN32))
    if (0 == mode) {
      mode = S_IRWXU | S_IRWXG | S_IRWXO;
    }
#endif
    if (!recursion) {
      return 0 == FS_MKDIR(dir_path, static_cast<mode_t>(mode));
    }

    std::vector<std::string> path_segs = SplitPath(dir_path, true);

    if (path_segs.empty()) {
      return false;
    }

    std::string current_path;
    if (nullptr != dir_path && ('/' == *dir_path || '\\' == *dir_path)) {
      current_path.reserve(strlen(dir_path) + 4);
      current_path = *dir_path;

      // NFS Supporting
      char next_char = *(dir_path + 1);
      if ('/' == next_char || '\\' == next_char) {
        current_path += next_char;
      }
    }

    for (size_t i = 0; i < path_segs.size(); ++i) {
      current_path += path_segs[i];

      if (false == IsExist(current_path.c_str())) {
        if (0 != FS_MKDIR(current_path.c_str(), static_cast<mode_t>(mode))) {
          return false;
        }
      }

      current_path += kDirectorySeparator;
    }

    return true;
  }

#if !defined(UTIL_FS_DISABLE_LINK)
  enum class LinkOption : int32_t {
    kDefault = 0x00,        // hard link for default
    kSymbolicLink = 0x01,   // or soft link
    kDirectoryLink = 0x02,  // it's used only for windows
    kForceRewrite = 0x04,   // delete the old file if it exists
  };

  /**
   * @brief Create link
   * @param oldpath source path
   * @param newpath target path
   * @param options options
   * @return 0 for success, or error code
   */
  static int Link(const char *oldpath, const char *newpath,
                  int32_t options = static_cast<int32_t>(LinkOption::kDefault)) {
    if ((options & static_cast<int32_t>(LinkOption::kForceRewrite)) && IsExist(newpath)) {
      remove(newpath);
    }

#  if !defined(__CYGWIN__) && defined(_WIN32)
#    if defined(UNICODE)
    USES_CONVERSION;
#    endif

    if (options & static_cast<int32_t>(LinkOption::kSymbolicLink)) {
      DWORD dwFlags = 0;
      if (options & static_cast<int32_t>(LinkOption::kDirectoryLink)) {
        dwFlags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
#    if defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
        dwFlags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#    endif
      }

      if (CreateSymbolicLink(VC_TEXT(newpath), VC_TEXT(oldpath), dwFlags)) {
        return 0;
      }

      return static_cast<int>(GetLastError());
    } else {
      if (CreateHardLink(VC_TEXT(newpath), VC_TEXT(oldpath), nullptr)) {
        return 0;
      }

      return static_cast<int>(GetLastError());
    }

#  else
    int opts = 0;
    if (options & static_cast<int32_t>(LinkOption::kSymbolicLink)) {
      opts = AT_SYMLINK_FOLLOW;
    }

    int res = ::linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, opts);
    if (0 == res) {
      return 0;
    }

    return errno;

#  endif
  }
#endif
};
}  // namespace

class ATFW_UTIL_SYMBOL_LOCAL PrometheusFileBackend {
 public:
  /**
   * Default Constructor.
   *
   * This constructor initializes the collection for metrics to export
   * in this class with default capacity
   */
  explicit PrometheusFileBackend(const PrometheusFileExporterOptions &options)
      : options_(options), is_initialized_{false}, check_file_path_interval_{0} {
    file_ = atfw::memory::stl::make_shared<FileStats>();
    file_->is_shutdown.store(false);
    file_->rotate_index = 0;
    file_->written_size = 0;
    file_->left_flush_metrics = 0;
    file_->last_checkpoint = 0;
    file_->metric_family_count.store(0);
    file_->flushed_metric_family_count.store(0);
  }

  ~PrometheusFileBackend() {
    if (file_) {
      file_->background_thread_waker_cv.notify_all();
      std::unique_ptr<std::thread> background_flush_thread;
      {
        std::lock_guard<std::mutex> lock_guard{file_->background_thread_lock};
        file_->background_flush_thread.swap(background_flush_thread);
      }
      if (background_flush_thread && background_flush_thread->joinable()) {
        background_flush_thread->join();
      }
    }
  }

  /**
   * This function is called by export() function and add the collection of
   * records to the metricsToCollect collection
   *
   * @param records a collection of records to add to the metricsToCollect collection
   */
  void AddMetricData(const ::opentelemetry::sdk::metrics::ResourceMetrics &data) {
    if (!is_initialized_.load(std::memory_order_acquire)) {
      Initialize();
    }

    std::vector<::prometheus::MetricFamily> translated =
        ::opentelemetry::exporter::metrics::PrometheusExporterUtils::TranslateToPrometheus(
            data, options_.populate_target_info, options_.without_otel_scope);

    if (file_->written_size > 0 && file_->written_size >= options_.file_size) {
      RotateLog();
    }
    CheckUpdate();

    std::shared_ptr<std::ofstream> out = OpenLogFile(true);
    if (!out) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock_guard{file_->file_lock};

      serializer_.Serialize(*out, translated);

      file_->metric_family_count += translated.size();

      // Pipe file size always returns 0, we ignore the size limit of it.
      auto written_size = out->tellp();
      if (written_size >= 0) {
        file_->written_size = static_cast<std::size_t>(written_size);
      }

      if (options_.flush_count > 0) {
        if (file_->left_flush_metrics <= translated.size()) {
          file_->left_flush_metrics = options_.flush_count;

          out->flush();

          file_->flushed_metric_family_count.store(file_->metric_family_count.load(std::memory_order_acquire),
                                                   std::memory_order_release);
        } else {
          file_->left_flush_metrics -= translated.size();
        }
      }
    }

    SpawnBackgroundWorkThread();
  }

  bool ForceFlush(std::chrono::microseconds timeout) noexcept {
    std::chrono::microseconds wait_interval = timeout / 256;
    if (wait_interval <= std::chrono::microseconds{0}) {
      wait_interval = timeout;
    }

    std::size_t current_wait_for_flush_count = file_->metric_family_count.load(std::memory_order_acquire);

    while (timeout >= std::chrono::microseconds::zero()) {
      // No more metrics to flush
      {
        if (file_->flushed_metric_family_count.load(std::memory_order_acquire) >= current_wait_for_flush_count) {
          break;
        }
      }

      std::chrono::system_clock::time_point begin_time = std::chrono::system_clock::now();
      // Notify background thread to flush immediately
      {
        std::lock_guard<std::mutex> lock_guard{file_->background_thread_lock};
        if (!file_->background_flush_thread) {
          break;
        }
        file_->background_thread_waker_cv.notify_all();
      }

      // Wait result
      {
        std::unique_lock<std::mutex> lk(file_->background_thread_waiter_lock);
        file_->background_thread_waiter_cv.wait_for(lk, wait_interval);
      }

      std::chrono::system_clock::time_point end_time = std::chrono::system_clock::now();
      if (end_time - begin_time > std::chrono::microseconds{1}) {
        timeout -= std::chrono::duration_cast<std::chrono::microseconds>(end_time - begin_time);
      } else {
        timeout -= std::chrono::microseconds{1};
      }
    }

    return timeout >= std::chrono::microseconds::zero();
  }

  bool Shutdown(std::chrono::microseconds timeout) noexcept {
    file_->is_shutdown.store(true, std::memory_order_release);

    bool result = ForceFlush(timeout);
    return result;
  }

 private:
  void Initialize() {
    if (is_initialized_.load(std::memory_order_acquire)) {
      return;
    }

    // Double check
    std::string file_pattern;
    {
      std::lock_guard<std::mutex> lock_guard{file_->file_lock};
      if (is_initialized_.load(std::memory_order_acquire)) {
        return;
      }
      is_initialized_.store(true, std::memory_order_release);

      file_->rotate_index = 0;
      ResetLogFile();

      char file_path[FileSystemUtil::kMaxPathSize];
      for (std::size_t i = 0; options_.file_size > 0 && i < options_.rotate_size; ++i) {
        FormatPath(file_path, sizeof(file_path), options_.file_pattern, i);
        std::size_t existed_file_size = FileSystemUtil::GetFileSize(file_path);

        // 文件不存在fsz也是0
        if (existed_file_size < options_.file_size) {
          file_->rotate_index = i;
          break;
        }
      }

      file_pattern = options_.file_pattern;
    }

    // Reset the interval to check
    static std::time_t check_interval[128] = {0};
    // Some timezone contains half a hour, we use 1800s for the max check interval.
    if (check_interval[static_cast<int>('S')] == 0) {
      check_interval[static_cast<int>('R')] = 60;
      check_interval[static_cast<int>('T')] = 1;
      check_interval[static_cast<int>('F')] = 1800;
      check_interval[static_cast<int>('S')] = 1;
      check_interval[static_cast<int>('M')] = 60;
      check_interval[static_cast<int>('I')] = 1800;
      check_interval[static_cast<int>('H')] = 1800;
      check_interval[static_cast<int>('w')] = 1800;
      check_interval[static_cast<int>('d')] = 1800;
      check_interval[static_cast<int>('j')] = 1800;
      check_interval[static_cast<int>('m')] = 1800;
      check_interval[static_cast<int>('y')] = 1800;
      check_interval[static_cast<int>('Y')] = 1800;
    }

    {
      check_file_path_interval_ = 0;
      for (std::size_t i = 0; i + 1 < file_pattern.size(); ++i) {
        if (file_pattern[i] == '%') {
          int checked = static_cast<int>(file_pattern[i + 1]);
          if (checked > 0 && checked < 128 && check_interval[checked] > 0) {
            if (0 == check_file_path_interval_ || check_interval[checked] < check_file_path_interval_) {
              check_file_path_interval_ = check_interval[checked];
            }
          }
        }
      }
    }

    OpenLogFile(false);
  }

  std::shared_ptr<std::ofstream> OpenLogFile(bool destroy_content) {
    std::lock_guard<std::mutex> lock_guard{file_->file_lock};

    if (file_->current_file && file_->current_file->good()) {
      return file_->current_file;
    }

    ResetLogFile();

    char file_path[FileSystemUtil::kMaxPathSize + 1];
    std::size_t file_path_size =
        FormatPath(file_path, FileSystemUtil::kMaxPathSize, options_.file_pattern, file_->rotate_index);
    if (file_path_size <= 0) {
      OTEL_INTERNAL_LOG_ERROR("[Prometheus File] Generate file path from pattern " << options_.file_pattern
                                                                                   << " failed");
      return std::shared_ptr<std::ofstream>();
    }
    file_path[file_path_size] = 0;

    std::shared_ptr<std::ofstream> of = atfw::memory::stl::make_shared<std::ofstream>();

    std::string directory_name = FileSystemUtil::DirName(file_path);
    if (!directory_name.empty() && !FileSystemUtil::IsExist(directory_name.c_str())) {
      FileSystemUtil::MkDir(directory_name.c_str(), true, 0);
    }

    if (destroy_content) {
      of->open(file_path, std::ios::binary | std::ios::out | std::ios::trunc);
      if (!of->is_open()) {
        OTEL_INTERNAL_LOG_ERROR("[Prometheus File] Open " << static_cast<const char *>(file_path)
                                                          << " failed: " << options_.file_pattern);
        return std::shared_ptr<std::ofstream>();
      }
      of->close();
    }

    of->open(file_path, std::ios::binary | std::ios::out | std::ios::app);
    if (!of->is_open()) {
      OTEL_INTERNAL_LOG_ERROR("[Prometheus File] Open " << static_cast<const char *>(file_path)
                                                        << " failed: " << options_.file_pattern);
      return std::shared_ptr<std::ofstream>();
    }

    of->seekp(0, std::ios_base::end);
    file_->written_size = static_cast<size_t>(of->tellp());

    file_->current_file = of;
    file_->last_checkpoint = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    file_->file_path.assign(file_path, file_path_size);

    // 硬链接别名
#if !defined(FS_DISABLE_LINK)
    if (!options_.alias_pattern.empty()) {
      char alias_file_path[FileSystemUtil::kMaxPathSize + 1];
      std::size_t file_path_len =
          FormatPath(alias_file_path, sizeof(alias_file_path) - 1, options_.alias_pattern, file_->rotate_index);
      if (file_path_len <= 0) {
        OTEL_INTERNAL_LOG_ERROR("[Prometheus File] Generate alias file path from " << options_.alias_pattern
                                                                                   << " failed");
        return file_->current_file;
      }

      if (file_path_len < sizeof(alias_file_path)) {
        alias_file_path[file_path_len] = 0;
      }

      if (0 == strcasecmp(file_path, alias_file_path)) {
        return file_->current_file;
      }

      int res = FileSystemUtil::Link(file_->file_path.c_str(), alias_file_path,
                                     static_cast<int32_t>(FileSystemUtil::LinkOption::kForceRewrite));
      if (res != 0) {
        OTEL_INTERNAL_LOG_ERROR("[Prometheus File] Link " << file_->file_path << " to " << alias_file_path
                                                          << " failed, errno: " << res);
#  if !defined(__CYGWIN__) && defined(_WIN32)
        OTEL_INTERNAL_LOG_ERROR(
            "[Prometheus File]     you can use FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | "
            "FORMAT_MESSAGE_FROM_SYSTEM"
            << " | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, " << res << ", MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), "
            << "(LPTSTR) &lpMsgBuf, 0, nullptr) to get the error message, see "
            << "https://docs.microsoft.com/en-us/windows/desktop/api/WinBase/nf-winbase-formatmessage and "
            << "https://docs.microsoft.com/en-us/windows/desktop/Debug/retrieving-the-last-error-code for more "
               "details");
#  else
        OTEL_INTERNAL_LOG_ERROR("[Prometheus File]     you can use strerror(" << res << ") to get the error message");
#  endif
        return file_->current_file;
      }
    }
#endif

    return file_->current_file;
  }

  void RotateLog() {
    std::lock_guard<std::mutex> lock_guard{file_->file_lock};
    if (options_.rotate_size > 0) {
      file_->rotate_index = (file_->rotate_index + 1) % options_.rotate_size;
    } else {
      file_->rotate_index = 0;
    }
    ResetLogFile();
  }

  void CheckUpdate() {
    if (check_file_path_interval_ <= 0) {
      return;
    }

    std::time_t current_checkpoint = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (current_checkpoint / check_file_path_interval_ == file_->last_checkpoint / check_file_path_interval_) {
      return;
    }
    // Refresh checkpoint
    file_->last_checkpoint = current_checkpoint;

    char file_path[FileSystemUtil::kMaxPathSize + 1];
    size_t file_path_len = FormatPath(file_path, sizeof(file_path) - 1, options_.file_pattern, file_->rotate_index);
    if (file_path_len <= 0) {
      return;
    }

    std::string new_file_path;
    std::string old_file_path;
    new_file_path.assign(file_path, file_path_len);

    {
      // Lock for a short time
      std::lock_guard<std::mutex> lock_guard{file_->file_lock};
      old_file_path = file_->file_path;

      if (new_file_path == old_file_path) {
        // Refresh checking time
        return;
      }
    }

    std::string new_dir = FileSystemUtil::DirName(new_file_path);
    std::string old_dir = FileSystemUtil::DirName(old_file_path);

    // Reset rotate index when directory changes
    if (new_dir != old_dir) {
      file_->rotate_index = 0;
    }

    ResetLogFile();
  }

  void ResetLogFile() {
    // ResetLogFile is called in lock, do not lock again

    file_->current_file.reset();
    file_->last_checkpoint = 0;
    file_->written_size = 0;
  }

  void SpawnBackgroundWorkThread() {
    if (options_.flush_interval <= std::chrono::microseconds{0}) {
      return;
    }

    if (!file_) {
      return;
    }

#if ATFRAMEWORK_UTILS_ENABLE_EXCEPTION
    try {
#endif

      std::lock_guard<std::mutex> lock_guard_caller{file_->background_thread_lock};
      if (file_->background_flush_thread) {
        return;
      }

      std::shared_ptr<FileStats> concurrency_file = file_;
      std::chrono::microseconds flush_interval = options_.flush_interval;
      file_->background_flush_thread.reset(new std::thread([concurrency_file, flush_interval]() {
        std::chrono::system_clock::time_point last_free_job_timepoint = std::chrono::system_clock::now();
        std::size_t last_metric_family_count = 0;

        while (true) {
          std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
          // Exit flush thread if there is not data to flush more than one minute.
          if (now - last_free_job_timepoint > std::chrono::minutes{1}) {
            break;
          }

          if (concurrency_file->is_shutdown.load(std::memory_order_acquire)) {
            break;
          }

          {
            std::unique_lock<std::mutex> lk(concurrency_file->background_thread_waker_lock);
            concurrency_file->background_thread_waker_cv.wait_for(lk, flush_interval);
          }

          {
            std::size_t current_metric_family_count =
                concurrency_file->metric_family_count.load(std::memory_order_acquire);
            std::lock_guard<std::mutex> lock_guard{concurrency_file->file_lock};
            if (current_metric_family_count != last_metric_family_count) {
              last_metric_family_count = current_metric_family_count;
              last_free_job_timepoint = std::chrono::system_clock::now();
            }

            if (concurrency_file->current_file) {
              concurrency_file->current_file->flush();
            }

            concurrency_file->flushed_metric_family_count.store(current_metric_family_count, std::memory_order_release);
          }

          concurrency_file->background_thread_waiter_cv.notify_all();
        }

        // Detach running thread because it will exit soon
        std::unique_ptr<std::thread> background_flush_thread;
        {
          std::lock_guard<std::mutex> lock_guard_inner{concurrency_file->background_thread_lock};
          background_flush_thread.swap(concurrency_file->background_flush_thread);
        }
        if (background_flush_thread && background_flush_thread->joinable()) {
          background_flush_thread->detach();
        }
      }));
#if ATFRAMEWORK_UTILS_ENABLE_EXCEPTION
    } catch (std::exception &e) {
      FWLOGERROR("SpawnBackgroundWorkThread for PrometheusFileExporter but got exception: {}", e.what());
    } catch (...) {
      FWLOGERROR("{}", "SpawnBackgroundWorkThread for PrometheusFileExporter but got unknown exception");
    }
#endif
  }

 private:
  PrometheusFileExporterOptions options_;
  ::prometheus::TextSerializer serializer_;

  struct FileStats {
    std::atomic<bool> is_shutdown;
    std::size_t rotate_index;
    std::size_t written_size;
    std::size_t left_flush_metrics;
    std::shared_ptr<std::ofstream> current_file;
    std::mutex file_lock;
    std::time_t last_checkpoint;
    std::string file_path;
    std::atomic<std::size_t> metric_family_count;
    std::atomic<std::size_t> flushed_metric_family_count;

    std::unique_ptr<std::thread> background_flush_thread;
    std::mutex background_thread_lock;
    std::mutex background_thread_waker_lock;
    std::condition_variable background_thread_waker_cv;
    std::mutex background_thread_waiter_lock;
    std::condition_variable background_thread_waiter_cv;
  };
  std::shared_ptr<FileStats> file_;

  std::atomic<bool> is_initialized_;
  std::time_t check_file_path_interval_;
};

SERVER_FRAME_API PrometheusFileExporter::PrometheusFileExporter(const PrometheusFileExporterOptions &options)
    : options_(options),
      is_shutdown_(false),
      backend_{atfw::memory::stl::make_shared<PrometheusFileBackend>(options)} {}

SERVER_FRAME_API ::opentelemetry::sdk::metrics::AggregationTemporality
PrometheusFileExporter::GetAggregationTemporality(::opentelemetry::sdk::metrics::InstrumentType) const noexcept {
  // Prometheus exporter only support Cumulative
  return ::opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
}

SERVER_FRAME_API ::opentelemetry::sdk::common::ExportResult PrometheusFileExporter::Export(
    const ::opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept {
  if (is_shutdown_) {
    return ::opentelemetry::sdk::common::ExportResult::kFailure;
  }

  if (backend_) {
    backend_->AddMetricData(data);
    return ::opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  return ::opentelemetry::sdk::common::ExportResult::kFailure;
}

SERVER_FRAME_API bool PrometheusFileExporter::ForceFlush(std::chrono::microseconds timeout) noexcept {
  if (backend_) {
    return backend_->ForceFlush(timeout);
  }

  return true;
}

SERVER_FRAME_API bool PrometheusFileExporter::Shutdown(std::chrono::microseconds timeout) noexcept {
  is_shutdown_ = true;

  if (backend_) {
    return backend_->Shutdown(timeout);
  }

  return true;
}

SERVER_FRAME_API bool PrometheusFileExporter::IsShutdown() const { return is_shutdown_; }

}  // namespace metrics
}  // namespace exporter
}  // namespace telemetry
}  // namespace rpc
