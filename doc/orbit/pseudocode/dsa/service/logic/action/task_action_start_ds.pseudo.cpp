#include "task_action_start_ds.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_INVALID_ARGUMENT = 1;
constexpr int ERROR_CODE_AGENT_DRAINING = 2;
constexpr int ERROR_CODE_CAPACITY_EXHAUSTED = 3;
constexpr int ERROR_CODE_START_DS_FAILED = 4;
constexpr int ERROR_CODE_HEARTBEAT_TRACK_FAILED = 5;
constexpr long long DEFAULT_HEARTBEAT_TIMEOUT_MS = 30000;

static bool is_agent_draining(runtime_handle_t& runtime) {
	(void)runtime;
	return false;
}

static long long get_monotonic_now_ms(runtime_handle_t& runtime) {
	(void)runtime;
	return 1000;
}

static rpc_result_t write_start_ds_error_response(rpc_context_t& rpc_context, int error_code) {
	(void)rpc_context;
	return error_code;
}

static rpc_result_t write_start_ds_success_response(rpc_context_t& rpc_context,
																										unsigned long long ds_id,
																										const process::start_ds_result_t& start_result) {
	// 真实代码阶段会把 ds_id、process_id 和 local_endpoint 写回 StartDSRsp。
	(void)rpc_context;
	(void)ds_id;
	(void)start_result;
	return 0;
}

static agent::load_report_t build_load_report(const agent::agent_load_snapshot_t& snapshot) {
	agent::load_report_t report;
	report.cpu_used = snapshot.cpu_used;
	report.memory_used_mb = snapshot.memory_used_mb;
	report.cpu_available = snapshot.cpu_available;
	report.memory_available_mb = snapshot.memory_available_mb;
	report.running_ds_count = snapshot.running_ds_count;
	return report;
}

}  // namespace

rpc_result_t run_task_action_start_ds(runtime_handle_t& runtime,
																			rpc_context_t& rpc_context,
																			const StartDSReq& request,
																			shared::runtime::resource_ledger& resource_ledger,
																			process::start_ds_process& start_ds_process,
																			heartbeat::heartbeat_monitor& heartbeat_monitor,
																			agent::controller_reporter& controller_reporter,
																			agent::load_reporter& load_reporter) {
	if (!request.has_meta() || request.expected_cpu() <= 0 || request.expected_memory_mb() <= 0) {
		return write_start_ds_error_response(rpc_context, ERROR_CODE_INVALID_ARGUMENT);
	}

	if (is_agent_draining(runtime)) {
		return write_start_ds_error_response(rpc_context, ERROR_CODE_AGENT_DRAINING);
	}

	auto request_id = runtime.allocate_request_id();
	auto ds_id = runtime.allocate_sequence();
	auto reserve_result = resource_ledger.reserve(ds_id, request.expected_cpu(), request.expected_memory_mb());
	if (reserve_result == shared::runtime::ledger_result_code_t::k_capacity_exhausted) {
		return write_start_ds_error_response(rpc_context, ERROR_CODE_CAPACITY_EXHAUSTED);
	}

	if (reserve_result != shared::runtime::ledger_result_code_t::k_ok) {
		return write_start_ds_error_response(rpc_context, ERROR_CODE_INVALID_ARGUMENT);
	}

	process::start_ds_request_t process_request;
	process_request.request_id = request_id;
	process_request.ds_id = ds_id;
	process_request.expected_cpu = request.expected_cpu();
	process_request.expected_memory_mb = request.expected_memory_mb();

	auto start_result = start_ds_process.launch(process_request);
	if (start_result.status != process::start_ds_status_t::k_ok) {
		return write_start_ds_error_response(rpc_context, ERROR_CODE_START_DS_FAILED);
	}

	auto track_result = heartbeat_monitor.track_ds(
			ds_id, start_result.process_id, get_monotonic_now_ms(runtime), DEFAULT_HEARTBEAT_TIMEOUT_MS);
	if (track_result != 0) {
		start_ds_process.rollback_failed_launch(ds_id);
		return write_start_ds_error_response(rpc_context, ERROR_CODE_HEARTBEAT_TRACK_FAILED);
	}

	agent::ds_started_report_t started_report;
	started_report.ds_id = ds_id;
	started_report.process_id = start_result.process_id;
	started_report.local_endpoint = start_result.local_endpoint;
	controller_reporter.notify_ds_started(started_report);

	auto snapshot = load_reporter.build_agent_load_snapshot();
	controller_reporter.report_agent_load(build_load_report(snapshot));
	return write_start_ds_success_response(rpc_context, ds_id, start_result);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
