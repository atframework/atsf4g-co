//
// Created by owent on 2019/07/15.
//

#ifndef ROUTER_ROUTER_SYSTEM_DEFS_H
#define ROUTER_ROUTER_SYSTEM_DEFS_H

#pragma once

#include <cstddef>
#include <list>
#include <stdint.h>

#include <design_pattern/singleton.h>
#include <std/smart_ptr.h>

class router_object_base;
class router_manager_base;
class router_manager_set;

struct router_system_timer_t {
    uint32_t                          timer_sequence;
    uint32_t                          type_id;
    time_t                            timeout;
    std::weak_ptr<router_object_base> obj_watcher;
};

#endif