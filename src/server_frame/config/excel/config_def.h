//
// Created by owent on 2016/9/23.
//

#ifndef CONFIG_EXCEL_CONFIG_DEF_H_H
#define CONFIG_EXCEL_CONFIG_DEF_H_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <type_traits>

#include "libresloader.h"
#include "config_set.h"

namespace excel {
// ============================== 配置包装辅助 ==============================
    template<typename TCfg, typename TKeyFn>
    struct config_set_def_unwrapper_key {
        typedef typename std::conditional<std::is_member_function_pointer<TKeyFn>::value,
            typename std::result_of<TKeyFn(TCfg)>::type,
            TKeyFn
        >::type type;
    };

    template<typename TCfg, typename... TKeyFns>
    struct config_set_def_kv {
        typedef config_set<xresloader::conf_manager_kv<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, 0> type;
    };

    template<typename TCfg, int INDEX, typename... TKeyFns>
    struct config_set_mdef_kv {
        typedef config_set <xresloader::conf_manager_kv<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, INDEX> type;
    };

    template<typename TCfg, typename... TKeyFns>
    struct config_set_def_kl {
        typedef config_set<xresloader::conf_manager_kl<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, 0> type;
    };

    template<typename TCfg, int INDEX, typename... TKeyFns>
    struct config_set_mdef_kl {
        typedef config_set <xresloader::conf_manager_kl<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, INDEX> type;
    };

    template<typename TCfg, typename... TKeyFns>
    struct config_set_def_iv {
        typedef config_set<xresloader::conf_manager_iv<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, 0> type;
    };

    template<typename TCfg, int INDEX, typename... TKeyFns>
    struct config_set_mdef_iv {
        typedef config_set <xresloader::conf_manager_iv<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, INDEX> type;
    };

    template<typename TCfg, typename... TKeyFns>
    struct config_set_def_il {
        typedef config_set<xresloader::conf_manager_kil<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, 0> type;
    };

    template<typename TCfg, int INDEX, typename... TKeyFns>
    struct config_set_mdef_il {
        typedef config_set <xresloader::conf_manager_kil<TCfg,
            typename config_set_def_unwrapper_key<TCfg, TKeyFns>::type...
        >, INDEX> type;
    };

// ============================== 配置声明列表 ==============================
}
#endif //_CONFIG_EXCEL_CONFIG_DEF_H_H
