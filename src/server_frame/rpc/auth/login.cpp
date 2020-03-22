//
// Created by owt50 on 2016/9/28.
//

#include <sstream>

#include <utility/random_engine.h>

#include "login.h"

namespace rpc {
    namespace auth {
        namespace login {
            void generate_login_code(char *code, size_t sz) {
                if (sz > 0) {
                    for (size_t i = 0; i < sz - 1; ++i) {
                        code[i] = util::random_engine::random_between<char>(33, 127);
                    }
                    code[sz - 1] = 0;
                }
            }

            std::string make_open_id(uint32_t zone_id, uint32_t account_type, uint32_t channel_id, const std::string &openid) {
                std::ostringstream openid_buff;
                openid_buff << "z-" << zone_id << ":at-" << account_type << ":"
                            << "c-" << channel_id << ":" << openid;
                return openid_buff.str();
            }
        } // namespace login
    }     // namespace auth
} // namespace rpc