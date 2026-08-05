// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <atframework/testing/runtime.h>

namespace atframework {
namespace testing {

class mock_resource;

// One recorded provider access (buffer or version request from the excel config manager).
struct ATFW_UTIL_SYMBOL_VISIBLE resource_access_record {
  // Empty path means a version request; non-empty path means a buffer request of that resource file.
  std::string path;
  uint64_t sequence = 0;
  bool found = false;
};

// In-memory resource provider bound to the excel_config_wrapper scoped provider seam. It supplies
// path -> bytes and one version string, never touches the filesystem, and keeps an access history.
// The real generated config manager still parses the bytes and builds indexes; this class never
// bypasses the real load/reload path.
class RPC_UNIT_TEST_API mock_resource {
 public:
  mock_resource();
  ~mock_resource();

  mock_resource(const mock_resource &) = delete;
  mock_resource &operator=(const mock_resource &) = delete;

  // Install this engine as the active provider (called by runtime before app init).
  void bind();
  // Clear the active provider and all in-memory state (called by runtime teardown).
  void unbind() noexcept;

  bool is_bound() const noexcept;

  // Provide or replace bytes of one resource file (e.g. "const.bytes").
  void set_file(gsl::string_view path, std::string bytes);
  void remove_file(gsl::string_view path);
  void clear_files();

  // Version served by the version provider. Change it (and optionally file bytes) then call
  // reload() to drive the real manager reload flow.
  void set_version(gsl::string_view version);
  const std::string &get_version() const noexcept;
  // When true, the version provider reports failure (simulates version read error).
  void set_version_error(bool error);

  // Drive excel_config_wrapper_reload_all(false) through the real wrapper. Returns its result code.
  int reload();

  const std::vector<resource_access_record> &access_history() const noexcept;

 private:
  bool on_get_buffer(std::string &out, const char *path);
  bool on_get_version(std::string &out);

  bool bound_ = false;
  bool version_error_ = false;
  std::string version_ = "0.0.0.1";
  std::unordered_map<std::string, std::string> files_;
  std::vector<resource_access_record> history_;
  uint64_t next_sequence_ = 1;
};

}  // namespace testing
}  // namespace atframework
