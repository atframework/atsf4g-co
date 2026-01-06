#include <cstdio>
#include <cstdlib>

#include <common/string_oprs.h>
#include <time/time_utility.h>

#include <uv.h>

#include <log/log_wrapper.h>

#include <cli/cmd_option.h>
#include <cli/cmd_option_phoenix.h>

#include <atframe/etcdcli/etcd_cluster.h>

#define ETCD_MODULE_BY_ID_DIR "by_id"
#define ETCD_MODULE_BY_TYPE_ID_DIR "by_type_id"
#define ETCD_MODULE_BY_TYPE_NAME_DIR "by_type_name"
#define ETCD_MODULE_BY_NAME_DIR "by_name"

static bool is_run = true;
static int wait_for_close = 0;
static std::string etcd_host;
static std::string prefix = ETCD_MODULE_BY_ID_DIR;
static std::string authorization;
static const char *exec_path = nullptr;
static int init_wait_ticks = 200;

static void tick_timer_callback(uv_timer_t *handle) {
  atfw::util::time::time_utility::update();

  atfw::atapp::etcd_cluster *ec = reinterpret_cast<atfw::atapp::etcd_cluster *>(handle->data);
  ec->tick();

  --init_wait_ticks;
  if (init_wait_ticks < 0) {
    is_run = false;
    uv_stop(handle->loop);
  }
}

static void signal_callback(uv_signal_t *handle, int signum) {
  is_run = false;
  uv_stop(handle->loop);
  WLOGERROR("Abort by user");
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

static int prog_option_handler_help(util::cli::callback_param params, atfw::util::cli::cmd_option_ci *cmd_mgr) {
  assert(cmd_mgr);
  atfw::util::cli::shell_stream shls(std::cout);

  shls() << atfw::util::cli::shell_font_style::SHELL_FONT_COLOR_YELLOW
         << atfw::util::cli::shell_font_style::SHELL_FONT_SPEC_BOLD << "Usage: " << exec_path << " [options...]"
         << std::endl;
  shls() << "  Example: " << exec_path << " -u http://127.0.0.1:2379/atapp/services -p by_id" << std::endl;
  shls() << "Options: " << std::endl;
  shls() << cmd_mgr->get_help_msg() << std::endl;
  is_run = false;
  return 0;
}

static int libcurl_callback_on_range_completed(util::network::http_request &req) {
  is_run = false;
  int *ret = reinterpret_cast<int *>(req.get_priv_data());
  if (nullptr == ret) {
    WLOGERROR("Etcd range request shouldn't has request without private data");
    return 0;
  }

  // 服务器错误则过一段时间后重试
  if (0 != req.get_error_code() ||
      atfw::util::network::http_request::status_code_t::EN_ECG_SUCCESS !=
          atfw::util::network::http_request::get_status_code_group(req.get_response_code())) {
    *ret = 4;
    WLOGERROR("Etcd range request failed, error code: %d, http code: %d\n%s", req.get_error_code(),
              req.get_response_code(), req.get_error_msg());

    return 0;
  }

  std::string http_content;
  req.get_response_stream().str().swap(http_content);

  rapidjson::Document doc;
  if (false == atfw::atapp::etcd_packer::parse_object(doc, http_content.c_str())) {
    WLOGERROR("Etcd range response error: %s", http_content.c_str());
    return 0;
  }

  rapidjson::Document::MemberIterator res = doc.FindMember("kvs");

  bool has_data = false;
  if (doc.MemberEnd() != res) {
    rapidjson::Document::Array all_events = res->value.GetArray();
    for (rapidjson::Document::Array::ValueIterator iter = all_events.Begin(); iter != all_events.End(); ++iter) {
      atfw::atapp::etcd_key_value kv_data;
      atfw::atapp::etcd_packer::unpack(kv_data, *iter);
      WLOGINFO("Path: %s, Lease: %lld, Created: %lld, Modify: %lld, Version: %lld", kv_data.key.c_str(),
               static_cast<long long>(kv_data.lease), static_cast<long long>(kv_data.create_revision),
               static_cast<long long>(kv_data.mod_revision), static_cast<long long>(kv_data.version));
      WLOGINFO("\tValue: %s", kv_data.value.c_str());
      has_data = true;
    }
  }

  if (!has_data) {
    WLOGINFO("Nothing found.");
  }

  return 0;
}

int main(int argc, char *argv[]) {
  exec_path = argv[0];
  atfw::util::cli::cmd_option_ci::ptr_type cmd_opts = atfw::util::cli::cmd_option_ci::create();
  std::string log_level_name;

  cmd_opts->bind_cmd("-h, --help, help", &prog_option_handler_help, cmd_opts.get())
      ->set_help_msg("-h. --help, help                                           show this help message.");
  cmd_opts->bind_cmd("-u, --url", atfw::util::cli::phoenix::assign(etcd_host))
      ->set_help_msg("-u, --url [base url with prefix]                           set base address.");
  {
    std::string msg = "-p, --prefix [prefix]                                      set list prefix(default: ";
    msg += prefix;
    msg += ", available: ";
    msg += ETCD_MODULE_BY_ID_DIR;
    msg += ", ";
    msg += ETCD_MODULE_BY_TYPE_ID_DIR;
    msg += ", ";
    msg += ETCD_MODULE_BY_TYPE_NAME_DIR;
    msg += ", ";
    msg += ETCD_MODULE_BY_NAME_DIR;
    msg += ")";
    cmd_opts->bind_cmd("-p, --prefix", atfw::util::cli::phoenix::assign(prefix))->set_help_msg(msg.c_str());
  }
  cmd_opts->bind_cmd("-a, --authorization", atfw::util::cli::phoenix::assign(authorization))
      ->set_help_msg("-a, --authorization [username:password]                    set authorization().");
  cmd_opts->bind_cmd("-l, --log-level", atfw::util::cli::phoenix::assign(log_level_name))
      ->set_help_msg("-l, --log-level <trace/debug/notice/info/warn/error/fatal> set log level(defult: info).");

  cmd_opts->start(argc - 1, argv + 1);
  if (!is_run) {
    return 0;
  }

  atfw::util::log::log_wrapper::level_t::type log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_INFO;
  if (0 == UTIL_STRFUNC_STRNCASE_CMP("fatal", log_level_name.c_str(), 5)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_FATAL;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("error", log_level_name.c_str(), 5)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_ERROR;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("warn", log_level_name.c_str(), 4)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_WARNING;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("warning", log_level_name.c_str(), 7)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_WARNING;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("info", log_level_name.c_str(), 4)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_INFO;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("notice", log_level_name.c_str(), 6)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_NOTICE;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("debug", log_level_name.c_str(), 5)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_DEBUG;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("trace", log_level_name.c_str(), 5)) {
    log_level = atfw::util::log::log_wrapper::level_t::LOG_LW_TRACE;
  }

  std::string::size_type pp = etcd_host.find("://");
  if (std::string::npos == pp) {
    std::cerr << "Invalid base url: " << etcd_host << std::endl;
    return 1;
  }
  pp = etcd_host.find('/', pp + 3);

  atfw::util::time::time_utility::update();
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->init(log_level);
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->set_prefix_format("[%L][%F %T.%f]: ");
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->add_sink(log_callback);
  WLOG_GETCAT(util::log::log_wrapper::categorize_t::DEFAULT)
      ->set_stacktrace_level(util::log::log_formatter::level_t::LOG_LW_DISABLED);

  atfw::util::network::http_request::curl_m_bind_ptr_t curl_mgr;
  atfw::util::network::http_request::create_curl_multi(uv_default_loop(), curl_mgr);
  atfw::atapp::etcd_cluster ec;
  ec.init(curl_mgr);
  std::vector<std::string> hosts;

  if (pp != std::string::npos) {
    hosts.push_back(etcd_host.substr(0, pp));
  } else {
    hosts.push_back(etcd_host);
  }
  ec.set_conf_hosts(hosts);
  ec.set_conf_authorization(authorization);

  uv_timer_t tick_timer;
  uv_timer_init(uv_default_loop(), &tick_timer);
  tick_timer.data = &ec;
  uv_timer_start(&tick_timer, tick_timer_callback, 50, 50);

  uv_signal_t sig;
  uv_signal_init(uv_default_loop(), &sig);
  uv_signal_start(&sig, signal_callback, SIGINT);

  while (is_run && init_wait_ticks > 0) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);

    if (ec.is_available()) {
      break;
    }
  }

  int ret = 0;
  do {
    if (!ec.is_available()) {
      WLOGERROR("Connect to %s failed", etcd_host.c_str());
      ret = 1;
      break;
    }

    atfw::util::network::http_request::ptr_t get_range_req;
    if (pp != std::string::npos) {
      get_range_req = ec.create_request_kv_get(etcd_host.substr(pp) + "/" + prefix, "+1");
    } else {
      get_range_req = ec.create_request_kv_get("/" + prefix, "+1");
    }

    if (!get_range_req) {
      WLOGERROR("Create range request to %s failed", etcd_host.c_str());
      ret = 2;
      break;
    }

    get_range_req->set_priv_data(&ret);
    get_range_req->set_on_complete(libcurl_callback_on_range_completed);
    ret = get_range_req->start(util::network::http_request::method_t::EN_MT_POST, false);
    if (ret != 0) {
      get_range_req->set_on_complete(nullptr);
      WLOGERROR("Start request to %s failed, res: %d", get_range_req->get_url().c_str(), ret);
    } else {
      WLOGDEBUG("Start request to %s success.", get_range_req->get_url().c_str());
    }

    while (is_run) {
      uv_run(uv_default_loop(), UV_RUN_ONCE);

      if (!ec.is_available()) {
        WLOGERROR("Some thing error");
        ret = 3;
        break;
      }
    }
  } while (false);

  wait_for_close = 2;

  uv_timer_stop(&tick_timer);
  uv_close((uv_handle_t *)&tick_timer, close_callback);

  uv_signal_stop(&sig);
  uv_close((uv_handle_t *)&sig, close_callback);

  atfw::util::network::http_request::destroy_curl_multi(curl_mgr);

  while (wait_for_close > 0) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);
  }

  return ret;
}
