// Copyright 2026 atframework

#pragma once

#include <common/file_system.h>

#include <string>
#include <vector>

#if defined(RPC_UNIT_TEST_EXCEL_RESOURCE_DIR)
namespace atframework {
namespace testing {
namespace detail {

// Mirror the deployment bindir layout (install/cloud-native/charts/libapp/templates/_atapp.logic.yaml.tpl):
// class-filtered tables live in the ServerOnly/ and Both/ sub-directories, which take precedence over the
// root directory (e.g. orbit_client_template.bytes is generated into Both/). The returned order is the
// precedence order: the first directory containing a resource file wins.
inline std::vector<std::string> get_excel_resource_bindirs() {
  std::vector<std::string> ret;
  const std::string root_dir = RPC_UNIT_TEST_EXCEL_RESOURCE_DIR;
  for (const char* sub_dir : {"ServerOnly", "Both", ""}) {
    std::string dir = root_dir;
    if (sub_dir[0] != '\0') {
      dir += "/";
      dir += sub_dir;
    }
    if (atfw::util::file_system::is_exist(dir.c_str())) {
      ret.push_back(std::move(dir));
    }
  }
  return ret;
}

}  // namespace detail
}  // namespace testing
}  // namespace atframework
#endif
