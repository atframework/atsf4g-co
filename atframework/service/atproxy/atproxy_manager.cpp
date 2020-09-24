#include <algorithm>

#include "atproxy_manager.h"
#include <time/time_utility.h>

namespace atframe {
    namespace proxy {
        static const std::string* next_listen_address(atproxy_manager::node_info_t& node_info) {
            if (node_info.round_robin_index < 0) {
                node_info.round_robin_index = 0;
            }
            if (node_info.etcd_node.node_discovery.gateway_size() > 0) {
                if (node_info.round_robin_index > node_info.etcd_node.node_discovery.gateway_size()) {
                    node_info.round_robin_index %= node_info.etcd_node.node_discovery.gateway_size();
                    return &node_info.etcd_node.node_discovery.gateway(node_info.round_robin_index ++);
                }
            }

            if (node_info.etcd_node.node_discovery.listen_size() >= 0) {
                if (node_info.round_robin_index > node_info.etcd_node.node_discovery.listen_size()) {
                    node_info.round_robin_index %= node_info.etcd_node.node_discovery.listen_size();
                    return &node_info.etcd_node.node_discovery.listen(node_info.round_robin_index ++);
                }
            }

            return NULL;
        }

        static int listen_address_size(atproxy_manager::node_info_t& node_info) {
            if (node_info.etcd_node.node_discovery.gateway_size() > 0) {
                return node_info.etcd_node.node_discovery.gateway_size();
            }

            return node_info.etcd_node.node_discovery.listen_size();
        }

        atproxy_manager::atproxy_manager() {}

        int atproxy_manager::init() {
            if (!get_app()) {
                FWLOGERROR("app not found");
                return -1;
            }

            std::shared_ptr<::atapp::etcd_module> etcd_mod = get_app()->get_etcd_module();
            if (!etcd_mod) {
                FWLOGERROR("etcd mod not found");
                return -1;
            }

            int ret = etcd_mod->add_watcher_by_type_name(get_app()->get_type_name(),
                                                                 std::bind(&atproxy_manager::on_watcher_notify, this, std::placeholders::_1));

            if (ret < 0) {
                FWLOGERROR("add watcher by type name {} failed, res: {}", get_app()->get_type_name(), ret);
                return ret;
            }

            FWLOGINFO("watch atproxy by_type path: {}", etcd_mod->get_by_type_name_watcher_path(get_app()->get_type_name()));

            return 0;
        }

        int atproxy_manager::tick() {
            time_t now = util::time::time_utility::get_sys_now();

            int ret = 0;
            do {
                if (check_list_.empty()) {
                    break;
                }

                check_info_t ci = check_list_.front();
                if (now <= ci.timeout_sec) {
                    break;
                }
                check_list_.pop_front();

                // skip self
                if (ci.proxy_id == get_app()->get_id()) {
                    continue;
                }

                std::map< ::atapp::app::app_id_t, node_info_t>::iterator iter = proxy_set_.find(ci.proxy_id);
                // already removed, skip
                if (iter == proxy_set_.end()) {
                    continue;
                }

                // if has no listen addrs, skip
                if (listen_address_size(iter->second) <= 0) {
                    continue;
                }

                // has another pending check info
                if (iter->second.next_action_time > ci.timeout_sec) {
                    continue;
                }

                if (get_app()->get_bus_node()) {
                    // set next_action_time first
                    iter->second.next_action_time = 0;

                    // already connected, skip
                    if (NULL != get_app()->get_bus_node()->get_endpoint(ci.proxy_id)) {
                        continue;
                    }

                    if (false == iter->second.is_available) {
                        continue;
                    }

                    const std::string* select_address = NULL;
                    {
                        int check_size = listen_address_size(iter->second);
                        for (int i = 0; NULL == select_address && i < check_size; ++i) {
                            // support more protocols
                            const std::string* try_addr = next_listen_address(iter->second);
                            if (NULL == try_addr) {
                                continue;
                            }
                            uint32_t address_type = get_app()->get_address_type(*try_addr);
                            if (address_type & atapp::app::address_type_t::EN_ACAT_LOCAL_HOST) {
                                continue;
                            }

                            select_address = try_addr;
                        }
                    }

                    if (NULL == select_address) {
                        continue;
                    }

                    // try to connect to brother proxy
                    int res = get_app()->get_bus_node()->connect(select_address->c_str());
                    if (res >= 0) {
                        ++ret;
                    } else {
                        FWLOGERROR("try to connect to proxy: {:#x}, address: {} failed, res: {}", iter->second.etcd_node.node_discovery.id(),
                                  *select_address, res);
                    }

                    // recheck some time later
                    ci.timeout_sec = now + get_app()->get_bus_node()->get_conf().retry_interval;
                    if (ci.timeout_sec <= now) {
                        ci.timeout_sec = now + 1;
                    }
                    // try to reconnect later
                    iter->second.next_action_time = ci.timeout_sec;
                    check_list_.push_back(ci);

                } else {
                    ci.timeout_sec = now + 1;
                    // try to reconnect later
                    iter->second.next_action_time = ci.timeout_sec;
                    check_list_.push_back(ci);
                }

            } while (true);

            return ret;
        }

        const char *atproxy_manager::name() const { return "atproxy manager"; }

        int atproxy_manager::set(atapp::etcd_module::node_info_t &etcd_node) {
            // TODO Support name only node
            check_info_t ci;
            ci.timeout_sec = util::time::time_utility::get_sys_now();
            ci.proxy_id    = etcd_node.node_discovery.id();

            proxy_set_t::iterator iter = proxy_set_.find(etcd_node.node_discovery.id());
            if (iter != proxy_set_.end()) {
                // already has pending action, just skipped
                if (iter->second.next_action_time >= ci.timeout_sec) {
                    return 0;
                } else {
                    iter->second.next_action_time = ci.timeout_sec;
                }
                iter->second.etcd_node = etcd_node;
            } else {
                node_info_t &proxy_info      = proxy_set_[etcd_node.node_discovery.id()];
                proxy_info.next_action_time  = ci.timeout_sec;
                proxy_info.etcd_node         = etcd_node;
                proxy_info.is_available      = check_available(etcd_node);
                proxy_info.round_robin_index = 0;
                FWLOGINFO("new atproxy {:#x} found", etcd_node.node_discovery.id());
            }

            // push front and check it on next loop
            check_list_.push_front(ci);
            return 0;
        }

        int atproxy_manager::remove(::atapp::app::app_id_t id) {
            proxy_set_t::iterator iter = proxy_set_.find(id);
            if (iter != proxy_set_.end()) {
                FWLOGINFO("lost atproxy {:#x}", id);
                proxy_set_.erase(iter);
            }
            return 0;
        }

        int atproxy_manager::reset(node_list_t &all_proxys) {
            proxy_set_.clear();
            check_list_.clear();

            for (std::list<node_info_t>::iterator iter = all_proxys.nodes.begin(); iter != all_proxys.nodes.end(); ++iter) {

                // skip all empty
                // TODO Support gateway
                if (iter->etcd_node.node_discovery.listen_size() == 0) {
                    continue;
                }

                check_info_t ci;
                ci.timeout_sec           = util::time::time_utility::get_sys_now();
                ci.proxy_id              = iter->etcd_node.node_discovery.id();
                (*iter).next_action_time = ci.timeout_sec;
                (*iter).is_available     = check_available((*iter).etcd_node);

                // copy proxy info
                proxy_set_[ci.proxy_id] = *iter;

                // push front and check it on next loop
                check_list_.push_front(ci);
            }

            return 0;
        }

        int atproxy_manager::on_connected(const ::atapp::app &, ::atapp::app::app_id_t) { return 0; }

        int atproxy_manager::on_disconnected(const ::atapp::app &app, ::atapp::app::app_id_t id) {
            proxy_set_t::iterator iter = proxy_set_.find(id);
            if (proxy_set_.end() != iter) {
                check_info_t ci;

                // when stoping bus noe may be unavailable
                if (!app.check_flag(::atapp::app::flag_t::STOPING)) {
                    if (app.get_bus_node() && app.get_bus_node()->get_conf().retry_interval > 0) {
                        ci.timeout_sec = util::time::time_utility::get_sys_now() + app.get_bus_node()->get_conf().retry_interval;
                    } else {
                        ci.timeout_sec = util::time::time_utility::get_sys_now() + 1;
                    }
                } else {
                    ci.timeout_sec = util::time::time_utility::get_sys_now() - 1;
                }

                if (iter->second.next_action_time < ci.timeout_sec) {
                    iter->second.next_action_time = ci.timeout_sec;
                    ci.proxy_id                   = id;
                    check_list_.push_back(ci);
                }
            }

            return 0;
        }

        void atproxy_manager::swap(node_info_t &l, node_info_t &r) {
            using std::swap;
            l.etcd_node.node_discovery.Swap(&r.etcd_node.node_discovery);
            swap(l.etcd_node.action, r.etcd_node.action);

            swap(l.next_action_time, r.next_action_time);
            swap(l.is_available, r.is_available);
            swap(l.round_robin_index, r.round_robin_index);
        }

        void atproxy_manager::on_watcher_notify(atapp::etcd_module::watcher_sender_one_t &sender) {
            if (sender.node.get().action == node_action_t::EN_NAT_DELETE) {
                // trigger manager
                remove(sender.node.get().node_discovery.id());
            } else {
                // trigger manager
                set(sender.node);
            }
        }

        bool atproxy_manager::check_available(const atapp::etcd_module::node_info_t& node_event) const {
            uint64_t atbus_protocol_version = 0;
            uint64_t atbus_protocol_min_version = 0;
            if (get_app() && get_app()->get_bus_node()) {
                atbus_protocol_version       = get_app()->get_bus_node()->get_protocol_version();
                atbus_protocol_min_version   = get_app()->get_bus_node()->get_protocol_minimal_version();
            } else {
                atbus_protocol_version     = atbus::protocol::ATBUS_PROTOCOL_VERSION;
                atbus_protocol_min_version = atbus::protocol::ATBUS_PROTOCOL_MINIMAL_VERSION;
            }

            return node_event.node_discovery.atbus_protocol_version() >= atbus_protocol_min_version &&
                atbus_protocol_version >= node_event.node_discovery.atbus_protocol_min_version();
        }
    } // namespace proxy
} // namespace atframe