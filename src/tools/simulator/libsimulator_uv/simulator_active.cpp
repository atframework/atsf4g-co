//
// Created by owt50 on 2016/10/11.
//

#include <list>

#include "simulator_active.h"

namespace proto {
    namespace detail {
        static std::list<simulator_activitor::fn_t>& get_all_active_fn() {
            static std::list<simulator_activitor::fn_t> ret;
            return ret;
        }

        simulator_activitor::simulator_activitor(fn_t fn) {
            get_all_active_fn().push_back(fn);
        }

        void simulator_activitor::active_all(simulator_base* base) {
            for(std::list<fn_t>::iterator iter = get_all_active_fn().begin(); iter != get_all_active_fn().end(); ++iter) {
                if (*iter) {
                    (*iter)(base);
                }
            }
        }
    }

};