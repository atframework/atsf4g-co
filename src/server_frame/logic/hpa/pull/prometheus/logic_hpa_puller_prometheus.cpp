// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/pull/prometheus/logic_hpa_puller_prometheus.h"

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/telemetry/rpc_global_service.h>

#include "logic/hpa/logic_hpa_controller.h"
#include "logic/hpa/logic_hpa_policy.h"
#include "logic/hpa/pull/prometheus/logic_hpa_data_type_prometheus.h"

SERVER_FRAME_API logic_hpa_puller_prometheus::logic_hpa_puller_prometheus(
    logic_hpa_policy& policy, std::shared_ptr<rpc::telemetry::group_type>& telemetry_group,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_cfg& /*hpa_cfg*/,
    const PROJECT_NAMESPACE_ID::config::logic_hpa_policy& /*policy_cfg*/)
    : logic_hpa_puller(policy), stoping_(false) {
  make_query(telemetry_group);
}

SERVER_FRAME_API logic_hpa_puller_prometheus::~logic_hpa_puller_prometheus() {
  if (pull_request_) {
    pull_request_->stop();
    pull_request_->set_priv_data(nullptr);
    pull_request_->set_on_complete(nullptr);
    pull_request_.reset();
  }
}

SERVER_FRAME_API int logic_hpa_puller_prometheus::tick(util::time::time_utility::raw_time_t /*now*/) {
  if (pull_request_ && !pull_request_->is_running()) {
    pull_request_.reset();
  }

  return 0;
}

SERVER_FRAME_API void logic_hpa_puller_prometheus::stop() {
  stoping_ = true;
  if (pull_request_) {
    pull_request_->stop();
    pull_request_->set_priv_data(nullptr);
    pull_request_->set_on_complete(nullptr);
    pull_request_.reset();
  }
}

SERVER_FRAME_API bool logic_hpa_puller_prometheus::do_pull() {
  if (pull_url_.empty()) {
    return false;
  }

  if (stoping_) {
    return false;
  }

  pull_request_ = get_owner().get_controller().create_http_request(pull_url_);
  if (!pull_request_) {
    return false;
  }

  for (auto& header_kv : pull_http_headers_) {
    pull_request_->append_http_header(header_kv.c_str());
  }

  pull_request_->set_priv_data(this);
  pull_request_->set_on_complete([](util::network::http_request& request) {
    logic_hpa_puller_prometheus* self = reinterpret_cast<logic_hpa_puller_prometheus*>(request.get_priv_data());
    if (nullptr == self) {
      FWLOGWARNING("[HPA]: Got unbinded http response from {}", request.get_url());
      return 0;
    }

    // Parse result and trigger callbacks
    do {
      std::string data = request.get_response_stream().str();
      if (data.empty()) {
        FWLOGDEBUG("[HPA]: Policy {} got empty query response\n\tquery: {}", self->get_owner().get_metrics_name(),
                   self->get_owner().get_query());
        break;
      }

      FWLOGDEBUG("[HPA]: Policy {} got query response\n\tquery: {}\n\t{}", self->get_owner().get_metrics_name(),
                 self->get_owner().get_query(), data);

      logic_hpa_pull_result_prometheus result;
      if (!result.parse(data)) {
        FWLOGWARNING("[HPA]: Policy {} unknown query response\n\tquery: {}\n\t{}", self->get_owner().get_metrics_name(),
                     self->get_owner().get_query(), data);
        break;
      }

      self->get_owner().trigger_event_on_pull_result(result);
    } while (false);

    if (self->pull_request_.get() == &request) {
      self->pull_request_.reset();
    }
    return 0;
  });

  int curl_result = pull_request_->start(get_owner().get_controller().get_pull_http_method());
  FWLOGDEBUG("[HPA]: Policy {} start query request to {}\n\tquery: {}", get_owner().get_metrics_name(), pull_url_,
             get_owner().get_query());

  return CURLM_OK == curl_result;
}

SERVER_FRAME_API bool logic_hpa_puller_prometheus::is_pulling() const noexcept {
  if (pull_request_ && pull_request_->is_running()) {
    return true;
  }

  return false;
}

SERVER_FRAME_API bool logic_hpa_puller_prometheus::is_stopped() const noexcept {
  if (!stoping_) {
    return false;
  }

  if (pull_request_ && pull_request_->is_running()) {
    return false;
  }

  return true;
}

SERVER_FRAME_API bool logic_hpa_puller_prometheus::can_pulling_available() const noexcept {
  if (pull_url_.empty()) {
    return false;
  }

  return true;
}

void logic_hpa_puller_prometheus::make_query(std::shared_ptr<rpc::telemetry::group_type>& telemetry_group) {
  auto metrics_configure = rpc::telemetry::global_service::get_metrics_configure(telemetry_group);
  if (metrics_configure.has_exporters() && metrics_configure.exporters().has_prometheus_http_api()) {
    for (auto& header_kv : metrics_configure.exporters().prometheus_http_api().headers()) {
      if (header_kv.key().empty()) {
        continue;
      }
      pull_http_headers_.push_back(util::log::format("{}: {}", header_kv.key(), header_kv.value()));
    }

    gsl::string_view url = metrics_configure.exporters().prometheus_http_api().url();
    while (!url.empty() && '/' == url.back()) {
      url = url.substr(0, url.size() - 1);
    }
    if (!url.empty()) {
      pull_url_ = atfw::util::log::format(
          "{}/query?query={}", url,
          atfw::util::uri::encode_uri_component(get_owner().get_query().c_str(), get_owner().get_query().size()));
    }
  }

  FWLOGINFO("[HPA]: Policy {} set pull URL: {}\n\tquery: {}", get_owner().get_metrics_name(), pull_url_,
            get_owner().get_query());
}
