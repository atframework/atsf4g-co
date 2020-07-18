//
// Created by owt50 on 2016/10/11.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_ACTIVE_H
#define ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_ACTIVE_H

#pragma once

class simulator_base;

namespace proto {
    namespace detail {
        class simulator_activitor {
        public:
            typedef void (*fn_t)(simulator_base *);

        public:
            simulator_activitor(fn_t fn);

            static void active_all(simulator_base *base);
        };
    }
};


#define SIMULATOR_ACTIVE_FN_NAME(IDENT) _simulator_active_fn_##IDENT
#define SIMULATOR_ACTIVE_OBJ_NAME(IDENT) _simulator_active_name_##IDENT

#define SIMULATOR_ACTIVE(IDENT, BASE)                                                                                  \
    static void SIMULATOR_ACTIVE_FN_NAME(IDENT)(simulator_base *);                                                     \
    namespace {                                                                                                        \
        static ::proto::detail::simulator_activitor SIMULATOR_ACTIVE_OBJ_NAME(IDENT)(SIMULATOR_ACTIVE_FN_NAME(IDENT)); \
    }                                                                                                                  \
    void SIMULATOR_ACTIVE_FN_NAME(IDENT)(simulator_base * BASE)


#define SIMULATOR_PRINT_PARAM_HELPER(PARAM, OSTREAM)                                                      \
    if (PARAM.get_cmd_array().size() > 1) {                                                               \
        std::string cmd_prefix = "";                                                                      \
        for (size_t i = 1; i < PARAM.get_cmd_array().size() - 1; ++i) {                                   \
            if (!PARAM.get_cmd_array()[i].first.empty() && '@' != PARAM.get_cmd_array()[i].first[0]) {    \
                cmd_prefix += PARAM.get_cmd_array()[i].first + " ";                                       \
            }                                                                                             \
        }                                                                                                 \
        for (size_t i = PARAM.get_cmd_array().size() - 2;; --i) {                                         \
            if (simulator_base::conv_cmd_mgr(PARAM.get_cmd_array()[i].second)) {                          \
                simulator_base::conv_cmd_mgr(PARAM.get_cmd_array()[i].second)->dump(OSTREAM, cmd_prefix); \
                break;                                                                                    \
            }                                                                                             \
            if (0 == i) {                                                                                 \
                break;                                                                                    \
            }                                                                                             \
        }                                                                                                 \
    }

#define SIMULATOR_CHECK_PARAMNUM(PARAM, N)                                                                                        \
    \
if(PARAM.get_params_number() < N) {                                                                                               \
        util::cli::shell_stream(std::cerr)() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "this command require " << N \
                                             << " parameters. but only got " << PARAM.get_params_number() << std::endl;           \
        SIMULATOR_PRINT_PARAM_HELPER(PARAM, std::cerr);                                                                           \
        return;                                                                                                                   \
    \
}

#define SIMULATOR_ERR_MSG() util::cli::shell_stream(std::cerr)() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED
#define SIMULATOR_INFO_MSG() util::cli::shell_stream(std::cout)() << util::cli::shell_font_style::SHELL_FONT_COLOR_GREEN
#define SIMULATOR_WARN_MSG() util::cli::shell_stream(std::cout)() << util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW
#define SIMULATOR_TRACE_MSG() std::cout

#endif // ATFRAMEWORK_LIBSIMULATOR_SIMULATOR_ACTIVE_H
