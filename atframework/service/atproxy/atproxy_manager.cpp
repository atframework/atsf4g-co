#include <algorithm>

#include "atproxy_manager.h"
#include <time/time_utility.h>

static void next_listen_address(std::list<std::string> &listens) {
    size_t sz = listens.size();
    if (sz <= 1) {
        return;
    }

    if (sz == 2) {
        listens.front().swap(listens.back());
    } else if (sz > 2) {
        listens.push_back(listens.front());
        listens.pop_front();
    }
}

namespace atframe {
    namespace proxy {
        atproxy_manager::atproxy_manager(etcd_mod_ptr etcd_mod) : binded_etcd_mod_(etcd_mod) {}

        int atproxy_manager::init() {
            if (!binded_etcd_mod_) {
                WLOGERROR("etcd mod not found");
                return -1;
            }

            int ret = binded_etcd_mod_->add_watcher_by_type_name(get_app()->get_type_name(),
                                                                 std::bind(&atproxy_manager::on_watcher_notify, this, std::placeholders::_1));

            if (ret < 0) {
                WLOGERROR("add watcher by type name %s failed, res: %d", get_app()->get_type_name().c_str(), ret);
                return ret;
            }

            WLOGINFO("watch atproxy by_type path: %s", binded_etcd_mod_->get_by_type_name_watcher_path(get_app()->get_type_name()).c_str());

            return 0;
        }

        int atproxy_manager::tick() {
            time_t now = util::time::time_utility::get_now();

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
                if (iter->second.etcd_node.listens.empty()) {
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

                    {
                        size_t check_size = iter->second.etcd_node.listens.size();
                        for (size_t i = 0; i < check_size; ++i) {
                            if (!get_app()->is_remote_address_available(iter->second.etcd_node.hostname, iter->second.etcd_node.listens.front())) {
                                next_listen_address(iter->second.etcd_node.listens);
                            }
                        }
                    }

                    // try to connect to brother proxy
                    int res = get_app()->get_bus_node()->connect(iter->second.etcd_node.listens.front().c_str());
                    if (res >= 0) {
                        ++ret;
                    } else {
                        WLOGERROR("try to connect to proxy: %llx, address: %s failed, res: %d", static_cast<unsigned long long>(iter->second.etcd_node.id),
                                  iter->second.etcd_node.listens.front().c_str(), res);
                    }

                    // recheck some time later
                    ci.timeout_sec = now + get_app()->get_bus_node()->get_conf().retry_interval;
                    if (ci.timeout_sec <= now) {
                        ci.timeout_sec = now + 1;
                    }
                    // try to reconnect later
                    iter->second.next_action_time = ci.timeout_sec;
                    check_list_.push_back(ci);

                    // if failed and there is more than one listen address, use next address next time.
                    next_listen_address(iter->second.etcd_node.listens);
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

        int atproxy_manager::set(atframe::component::etcd_module::node_info_t &etcd_node) {
            check_info_t ci;
            ci.timeout_sec = util::time::time_utility::get_now();
            ci.proxy_id    = etcd_node.id;

            proxy_set_t::iterator iter = proxy_set_.find(etcd_node.id);
            if (iter != proxy_set_.end()) {
                // already has pending action, just skipped
                if (iter->second.next_action_time >= ci.timeout_sec) {
                    return 0;
                } else {
                    iter->second.next_action_time = ci.timeout_sec;
                }
                iter->second.etcd_node = etcd_node;
            } else {
                node_info_t &proxy_info     = proxy_set_[etcd_node.id];
                proxy_info.next_action_time = ci.timeout_sec;
                proxy_info.etcd_node        = etcd_node;
                proxy_info.is_available     = check_available(etcd_node);
                WLOGINFO("new atproxy %llx found", static_cast<unsigned long long>(etcd_node.id));
            }

            // push front and check it on next loop
            check_list_.push_front(ci);
            return 0;
        }

        int atproxy_manager::remove(::atapp::app::app_id_t id) {
            proxy_set_t::iterator iter = proxy_set_.find(id);
            if (iter != proxy_set_.end()) {
                WLOGINFO("lost atproxy %llx", static_cast<unsigned long long>(id));
                proxy_set_.erase(iter);
            }
            return 0;
        }

        int atproxy_manager::reset(node_list_t &all_proxys) {
            proxy_set_.clear();
            check_list_.clear();

            for (std::list<node_info_t>::iterator iter = all_proxys.nodes.begin(); iter != all_proxys.nodes.end(); ++iter) {

                // skip all empty
                if (iter->etcd_node.listens.empty()) {
                    continue;
                }

                check_info_t ci;
                ci.timeout_sec           = util::time::time_utility::get_now();
                ci.proxy_id              = iter->etcd_node.id;
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
                        ci.timeout_sec = util::time::time_utility::get_now() + app.get_bus_node()->get_conf().retry_interval;
                    } else {
                        ci.timeout_sec = util::time::time_utility::get_now() + 1;
                    }
                } else {
                    ci.timeout_sec = util::time::time_utility::get_now() - 1;
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
            swap(l.etcd_node.id, r.etcd_node.id);
            swap(l.etcd_node.name, r.etcd_node.name);
            swap(l.etcd_node.hostname, r.etcd_node.hostname);
            swap(l.etcd_node.listens, r.etcd_node.listens);
            swap(l.etcd_node.hash_code, r.etcd_node.hash_code);
            swap(l.etcd_node.type_id, r.etcd_node.type_id);
            swap(l.etcd_node.type_name, r.etcd_node.type_name);
            swap(l.etcd_node.action, r.etcd_node.action);
            swap(l.etcd_node.version, r.etcd_node.version);
            swap(l.etcd_node.custom_data, r.etcd_node.custom_data);
            swap(l.etcd_node.atbus_protocol_version, r.etcd_node.atbus_protocol_version);
            swap(l.etcd_node.atbus_protocol_min_version, r.etcd_node.atbus_protocol_min_version);

            swap(l.next_action_time, r.next_action_time);
            swap(l.is_available, r.is_available);
        }

        void atproxy_manager::on_watcher_notify(atframe::component::etcd_module::watcher_sender_one_t &sender) {
            if (sender.node.get().action == node_action_t::EN_NAT_DELETE) {
                // trigger manager
                remove(sender.node.get().id);
            } else {
                // trigger manager
                set(sender.node);
            }
        }

        bool atproxy_manager::check_available(const atframe::component::etcd_module::node_info_t& node_event) const {
            uint64_t atbus_protocol_version = 0;
            uint64_t atbus_protocol_min_version = 0;
            if (get_app() && get_app()->get_bus_node()) {
                atbus_protocol_version       = get_app()->get_bus_node()->get_protocol_version();
                atbus_protocol_min_version   = get_app()->get_bus_node()->get_protocol_minimal_version();
            } else {
                atbus_protocol_version     = atbus::protocol::ATBUS_PROTOCOL_VERSION;
                atbus_protocol_min_version = atbus::protocol::ATBUS_PROTOCOL_MINIMAL_VERSION;
            }

            return node_event.atbus_protocol_version >= atbus_protocol_min_version &&
                atbus_protocol_version >= node_event.atbus_protocol_min_version;
        }
    } // namespace proxy
} // namespace atframe