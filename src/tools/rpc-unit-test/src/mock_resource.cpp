// Copyright 2026 atframework

#include <atframework/testing/mock_resource.h>

#include <config/excel_config_wrapper.h>

#include <string>
#include <utility>
#include <vector>

namespace atframework {
namespace testing {

mock_resource::mock_resource() = default;
mock_resource::~mock_resource() { unbind(); }

void mock_resource::bind() {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (bound_) {
    return;
  }
  bound_ = true;
  excel_resource_provider_for_unit_test_t provider;
  provider.get_buffer = [this](std::string &out, const char *path) { return on_get_buffer(out, path); };
  provider.get_version = [this](std::string &out) { return on_get_version(out); };
  set_excel_resource_provider_for_unit_test(provider);
#endif
}

void mock_resource::unbind() noexcept {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (!bound_) {
    return;
  }
  bound_ = false;
  clear_excel_resource_provider_for_unit_test();
#endif
  files_.clear();
  history_.clear();
  version_error_ = false;
  version_ = "0.0.0.1";
  next_sequence_ = 1;
}

bool mock_resource::is_bound() const noexcept { return bound_; }

void mock_resource::set_file(gsl::string_view path, std::string bytes) {
  files_[std::string{path.data(), path.size()}] = std::move(bytes);
}

void mock_resource::remove_file(gsl::string_view path) { files_.erase(std::string{path.data(), path.size()}); }

void mock_resource::clear_files() { files_.clear(); }

void mock_resource::set_version(gsl::string_view version) { version_.assign(version.data(), version.size()); }

const std::string &mock_resource::get_version() const noexcept { return version_; }

void mock_resource::set_version_error(bool error) { version_error_ = error; }

int mock_resource::reload() { return excel_config_wrapper_reload_all(false); }

const std::vector<resource_access_record> &mock_resource::access_history() const noexcept { return history_; }

bool mock_resource::on_get_buffer(std::string &out, const char *path) {
  resource_access_record record;
  record.path = path != nullptr ? path : "";
  record.sequence = next_sequence_++;
  auto iter = files_.find(record.path);
  record.found = iter != files_.end();
  history_.push_back(record);
  if (iter == files_.end()) {
    return false;
  }
  out = iter->second;
  return true;
}

bool mock_resource::on_get_version(std::string &out) {
  resource_access_record record;
  record.sequence = next_sequence_++;
  record.found = !version_error_;
  history_.push_back(record);
  if (version_error_) {
    return false;
  }
  out = version_;
  return true;
}

}  // namespace testing
}  // namespace atframework
