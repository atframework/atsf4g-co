#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <atframe/atapp.h>

#include <common/file_system.h>
#include <time/time_utility.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>

#include <config/server_frame_build_feature.h>

class orbit_server_module : public atapp::module_impl, public std::enable_shared_from_this<orbit_server_module> {
 public:
  int init() override;
  int stop() override;

  const char *name() const override { return "orbit_server_module"; }

  int tick() override;
};