// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <atframe/atapp_config.h>

#include <dispatcher/task_type_traits.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rpc/rpc_common_types.h"

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

#if defined(RPC_UNIT_TEST_NATIVE) && RPC_UNIT_TEST_NATIVE
#  if defined(RPC_UNIT_TEST_DLL) && RPC_UNIT_TEST_DLL
#    define RPC_UNIT_TEST_API ATFW_UTIL_SYMBOL_EXPORT
#  else
#    define RPC_UNIT_TEST_API ATFW_UTIL_SYMBOL_VISIBLE
#  endif
#else
#  if defined(RPC_UNIT_TEST_DLL) && RPC_UNIT_TEST_DLL
#    define RPC_UNIT_TEST_API ATFW_UTIL_SYMBOL_IMPORT
#  else
#    define RPC_UNIT_TEST_API ATFW_UTIL_SYMBOL_VISIBLE
#  endif
#endif

namespace rpc {
class context;
}

namespace atframework {
namespace testing {

class runtime;
class mock_cs;
class mock_db;
class mock_resource;
class mock_discovery;
class mock_dns;
class mock_hpa;
class mock_router;
class mock_ss;
class raw_transport;

// Wired now: ss, dns, cs, db, router, resource, hpa. uuid flows through the db hook (uuid_allocator uses
// hash_table inc_field) and needs no runtime wiring. orbit is declared for the planned phase in
// IMPLEMENTATION_PLAN.md (phase 8). telemetry has no engine: it is handled by pure config materialization
// (see the telemetry_* options below and IMPLEMENTATION_PLAN.md 8.9).
enum class feature : int32_t {
  ss = 0,
  dns = 1,
  cs = 2,
  db = 3,
  uuid = 4,
  resource = 5,
  router = 6,
  orbit = 7,
  hpa = 8,
  telemetry = 9,
};

struct ATFW_UTIL_SYMBOL_VISIBLE runtime_options {
  std::vector<feature> features;
  std::string app_name = "rpc-unit-test";
  uint64_t app_id = 0x11000001;
  uint64_t type_id = 4096;
  std::string type_name = "rpc-unit-test";
  uint32_t zone_id = 1;
  uint32_t world_id = 1;
  // Directory used to materialize the generated YAML config. Must be inside the build tree.
  // When empty, the runtime resolves to: the RPC_UNIT_TEST_WORKDIR environment variable (set per target
  // by the CMake helper for ctest), then the build-tree default baked by the helper, then the current
  // working directory as a last resort. Leaving it empty keeps the generated config out of the repo root
  // even for direct binary invocations.
  std::string working_directory;
  // Hard deadline of the whole stop()/teardown procedure. Must be short and non-zero.
  std::chrono::system_clock::duration teardown_deadline = std::chrono::seconds{10};
  // Optional callback to add component modules/dispatchers/router managers before app init.
  std::function<int(runtime &)> setup_callback;

  // Telemetry (IMPLEMENTATION_PLAN.md 8.9): file-only exporters materialized into the generated YAML.
  // otlp_file base pattern; ".trace.log"/".metrics.log"/".logs.log" suffixes are appended. The path must
  // stay inside the build-tree working directory.
  std::string telemetry_otlp_file_pattern;
  // ostream debug exporter with the value "stderr" (trace/metrics/logs).
  bool telemetry_ostream_stderr = false;
  // Forbidden outbound/inbound exporters (otlp_grpc/otlp_http/prometheus_push/prometheus_pull). Setting any
  // of them makes start() fail fast with a diagnostic instead of opening a network connection.
  std::string telemetry_otlp_grpc_endpoint;
  std::string telemetry_otlp_http_endpoint;
  std::string telemetry_prometheus_push_host;
  std::string telemetry_prometheus_pull_url;

  bool has_feature(feature input) const noexcept;
};

struct ATFW_UTIL_SYMBOL_VISIBLE wait_result {
  int32_t result_code = 0;
  bool task_started = false;
  bool task_exited = false;
  bool task_timed_out = false;
  bool task_canceled = false;
  bool task_faulted = false;
  bool hard_timed_out = false;
  bool runtime_poisoned = false;
  std::chrono::system_clock::duration elapsed = std::chrono::system_clock::duration::zero();
  uint64_t task_id = 0;
  std::string diagnostic;
};

class ATFW_UTIL_SYMBOL_VISIBLE task_handle {
 public:
  task_handle() = default;

  bool empty() const noexcept;
  explicit operator bool() const noexcept { return !empty(); }
  uint64_t get_task_id() const noexcept;
  const std::string &get_diagnostic() const noexcept { return diagnostic_; }

 private:
  friend class runtime;

  task_type_trait::task_type task_;
  bool started_ = false;
  std::string diagnostic_;
};

class RPC_UNIT_TEST_API runtime {
 public:
  runtime();
  ~runtime();

  runtime(const runtime &) = delete;
  runtime &operator=(const runtime &) = delete;

  // Start the fixture. Returns 0 on success, negative error code on failure.
  // On failure, all completed stages are rolled back and the active-runtime guard is released.
  int start(const runtime_options &options);

  // Idempotent. Returns 0 on success, positive when the teardown deadline was exceeded.
  // A normal CASE_TEST must explicitly assert stop(); the destructor only handles abnormal exits.
  int stop() noexcept;

  bool is_running() const noexcept;
  bool is_poisoned() const noexcept;

  // The diagnostic of the last failed operation (start/stop/pump).
  const std::string &get_diagnostic() const noexcept;

  task_handle run_task(gsl::string_view name, std::chrono::system_clock::duration timeout,
                       std::function<rpc::result_code_type(rpc::context &)> fn);
  template <class TREP, class TPERIOD>
  ATFW_UTIL_SYMBOL_VISIBLE task_handle run_task(gsl::string_view name, std::chrono::duration<TREP, TPERIOD> timeout,
                                                std::function<rpc::result_code_type(rpc::context &)> fn) {
    return run_task(name, std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout), std::move(fn));
  }

  wait_result wait(const task_handle &task, std::chrono::system_clock::duration hard_timeout);
  template <class TREP, class TPERIOD>
  ATFW_UTIL_SYMBOL_VISIBLE wait_result wait(const task_handle &task,
                                            std::chrono::duration<TREP, TPERIOD> hard_timeout) {
    return wait(task, std::chrono::duration_cast<std::chrono::system_clock::duration>(hard_timeout));
  }

  // Advance one pump generation: refresh cached clock, run atapp event loop once, then deliver
  // due mock events of older generations and run the loop again to resume tasks.
  // Returns the result of the last app.run_noblock() (0: idle, 1: pending, <0: error and poisoned).
  int pump_once() noexcept;

  atfw::atapp::app *get_app() const noexcept;

  // Facades are valid for the whole lifetime of the runtime object. Their operations fail
  // gracefully while the runtime is not running.
  mock_discovery &discovery() noexcept;
  mock_dns &dns() noexcept;
  mock_db &db() noexcept;
  mock_resource &resource() noexcept;
  mock_hpa &hpa() noexcept;
  mock_cs &cs() noexcept;
  raw_transport &transport() noexcept;
  mock_ss &ss() noexcept;
  mock_router &router() noexcept;

 private:
  struct impl_data;
  std::unique_ptr<impl_data> impl_;
};

}  // namespace testing
}  // namespace atframework
