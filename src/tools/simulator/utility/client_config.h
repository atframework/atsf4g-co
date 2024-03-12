//
// Created by owt50 on 2016-10-12.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_CONFIG_H
#define ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_CONFIG_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <string>

struct client_config {
  static std::string host;
  static std::string lua_player_code;
  static std::string lua_player_file;
  static int port;
};

#endif  // ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_CONFIG_H
