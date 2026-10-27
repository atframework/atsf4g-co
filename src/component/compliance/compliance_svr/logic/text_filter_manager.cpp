// Copyright 2026 atframework

#include "logic/text_filter_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/compliance_svr_config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <lock/lock_holder.h>
#include <log/log_wrapper.h>
#include <testing/unit_test_case_cleanup.h>
#include <testing/unit_test_global_register.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace compliance {

text_filter_manager::text_filter_manager() = default;
text_filter_manager::~text_filter_manager() = default;

int text_filter_manager::init() { return reload(); }

int text_filter_manager::reload() {
  const auto& configuration =
      logic_config::me()->get_server_instance_config<atframework::compliance::config::compliance_svr_cfg>();
  const std::string& path = configuration.text_filter_dictionary_file();
  if (path.empty()) {
    FWLOGERROR("compliance_svr.text_filter_dictionary_file is required");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    FWLOGERROR("Cannot open compliance text filter dictionary: {}", path);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }
  auto candidate = std::make_shared<dictionary_t>();
  dictionary_t::load_options options;
  options.verify_semantics = true;
  auto result = candidate->load(input, options);
  if (result != dictionary_t::status::kOk || input.peek() != std::char_traits<char>::eof()) {
    FWLOGERROR("Invalid compliance text filter dictionary: {}, status={}", path, dictionary_t::status_message(result));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }
  if (!candidate->get_statistics().nocase) {
    FWLOGERROR("Compliance text filter dictionary must enable --nocase: {}", path);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }
  {
    atfw::util::lock::lock_holder<std::mutex> lock{dictionary_mutex_};
    dictionary_ = candidate;
  }
  FWLOGINFO("Loaded compliance text filter dictionary: {}, keywords={}", path, candidate->get_statistics().keywords);
  return 0;
}

void text_filter_manager::cleanup() {
  atfw::util::lock::lock_holder<std::mutex> lock{dictionary_mutex_};
  dictionary_ = nullptr;
}

std::shared_ptr<const text_filter_manager::dictionary_t> text_filter_manager::get_dictionary() const {
  atfw::util::lock::lock_holder<std::mutex> lock{dictionary_mutex_};
  return dictionary_;
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace {
ATFW_SERVER_FRAME_TESTING_SETUP(compliance_text_filter_cleanup_registered) {
  server_frame_unit_test_register_case_cleanup("compliance.text_filter_manager", kUnitTestCaseCleanupLevelServiceCache,
                                               [] {
                                                 if (!text_filter_manager::is_instance_destroyed()) {
                                                   text_filter_manager::me()->cleanup();
                                                 }
                                               });
}
}  // namespace
#endif

}  // namespace compliance
