#include <cstdio>
#include <cstdlib>

#include <time/time_utility.h>

#include <uv.h>

#include <log/log_wrapper.h>

#include <atframe/etcdcli/etcd_cluster.h>
#include <atframe/etcdcli/etcd_keepalive.h>
#include <atframe/etcdcli/etcd_watcher.h>

static bool is_run = true;
static int wait_for_close = 0;
static void tick_timer_callback(uv_timer_t *handle) {
  atfw::util::time::time_utility::update();

  atfw::atapp::etcd_cluster *ec = reinterpret_cast<atfw::atapp::etcd_cluster *>(handle->data);
  ec->tick();
}

static void signal_callback(uv_signal_t *handle, int signum) {
  is_run = false;
  uv_stop(handle->loop);
}

static void close_callback(uv_handle_t *handle) { --wait_for_close; }

static void log_callback(const atfw::util::log::log_wrapper::caller_info_t &caller, const char *content,
                         size_t content_size) {
  puts(content);
}

struct check_keepalive_data_callback {
  check_keepalive_data_callback(const std::string &d) : data(d) {}

  bool operator()(const std::string &checked) {
    if (checked.empty()) {
      return true;
    }

    if (checked != data) {
      WLOGERROR("Expect keepalive data is %s but real is %s, stopped\n", data.c_str(), checked.c_str());
      is_run = false;
      uv_stop(uv_default_loop());
      return false;
    }

    return true;
  }

  std::string data;
};

/**
./etcd-watcher http://127.0.0.1:2379 /atapp/services/by_id /atapp/services/by_id/123456 123456
./etcd-watcher http://127.0.0.1:2379 /atapp/services/by_id /atapp/services/by_id/456789 456789
curl http://127.0.0.1:2379/v3alpha/watch -XPOST -d '{"create_request":  {"key": "L2F0YXBwL3Byb3h5L3NlcnZpY2Vz",
"range_end": "L2F0YXBwL3Byb3h5L3NlcnZpY2V0", "prev_kv": true} }'
**/

int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("Usage: %s <init host> <watch path> <keepalive path> <keepalive value>\n", argv[0]);
    printf("  Example: %s http://127.0.0.1:2379 /atapp/services/by_id /atapp/services/by_id/123456 \"{}\"\n", argv[0]);
    return 0;
  }

  atfw::util::time::time_utility::update();
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->init();
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->set_prefix_format("[%L][%F %T.%f][%k:%n(%C)]: ");
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->add_sink(log_callback);
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)
      ->set_stacktrace_level(util::log::log_formatter::level_t::LOG_LW_ERROR);

  atfw::util::network::http_request::curl_m_bind_ptr_t curl_mgr;
  atfw::util::network::http_request::create_curl_multi(uv_default_loop(), curl_mgr);
  atfw::atapp::etcd_cluster ec;
  ec.init(curl_mgr);
  std::vector<std::string> hosts;
  hosts.push_back(argv[1]);
  ec.set_conf_hosts(hosts);

  if (argc > 2) {
    atfw::atapp::etcd_watcher::ptr_t p = atfw::atapp::etcd_watcher::create(ec, argv[2]);
    if (p) {
      ec.add_watcher(p);
    }
  }

  if (argc > 4) {
    atfw::atapp::etcd_keepalive::ptr_t p = atfw::atapp::etcd_keepalive::create(ec, argv[3]);
    if (p) {
      p->set_checker(check_keepalive_data_callback(argv[4]));
      p->set_value(argv[4]);
      ec.add_keepalive(p);
    }
  }

  uv_timer_t tick_timer;
  uv_timer_init(uv_default_loop(), &tick_timer);
  tick_timer.data = &ec;
  uv_timer_start(&tick_timer, tick_timer_callback, 1000, 1000);

  uv_signal_t sig;
  uv_signal_init(uv_default_loop(), &sig);
  uv_signal_start(&sig, signal_callback, SIGINT);

  while (is_run) {
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  }

  wait_for_close = 2;

  uv_timer_stop(&tick_timer);
  uv_close((uv_handle_t *)&tick_timer, close_callback);

  uv_signal_stop(&sig);
  uv_close((uv_handle_t *)&sig, close_callback);

  atfw::util::network::http_request::destroy_curl_multi(curl_mgr);

  while (wait_for_close > 0) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);
  }

  return 0;
}
