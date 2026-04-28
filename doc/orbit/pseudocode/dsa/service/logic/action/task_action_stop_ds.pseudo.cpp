#include "task_action_stop_ds.pseudo.h"

namespace atorbit {
namespace dsa {
namespace service {
namespace logic {
namespace action {

namespace {

constexpr int ERROR_CODE_INVALID_ARGUMENT = 1;
constexpr int ERROR_CODE_DS_NOT_FOUND = 2;
constexpr int ERROR_CODE_STOP_FORWARD_FAILED = 3;
constexpr int ERROR_CODE_STOP_CLEANUP_FAILED = 4;
constexpr int EXIT_REASON_GRACEFUL_STOP_TIMEOUT = 2;

static bool graceful_stop_timed_out(runtime_handle_t& runtime, unsigned long long ds_id) {
	(void)runtime;
	(void)ds_id;
	return true;
}

static rpc_result_t write_stop_ds_error_response(rpc_context_t& rpc_context, int error_code) {
	(void)rpc_context;
	return error_code;
}

static rpc_result_t write_stop_ds_success_response(rpc_context_t& rpc_context, unsigned long long ds_id) {
	// 真实代码阶段会把 ds_id 和 stop result 写回 StopDSRsp。
	(void)rpc_context;
	(void)ds_id;
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

static int cleanup_stopped_ds(unsigned long long ds_id,
															shared::runtime::resource_ledger& resource_ledger,
															agent::local_channel_service& local_channel_service,
															agent::controller_reporter& controller_reporter) {
	agent::ds_exit_report_t exit_report;
	exit_report.ds_id = ds_id;
	exit_report.exit_reason = EXIT_REASON_GRACEFUL_STOP_TIMEOUT;
	exit_report.exit_code = 0;
	exit_report.user_data = "graceful_stop_timeout";

	local_channel_service.remove_ds(ds_id);
	resource_ledger.release(ds_id);
	controller_reporter.notify_ds_exit(exit_report);
	return 0;
}

}  // namespace

rpc_result_t run_task_action_stop_ds(runtime_handle_t& runtime,
																		 rpc_context_t& rpc_context,
																		 const StopDSReq& request,
																		 agent::local_channel_service& local_channel_service,
																		 heartbeat::heartbeat_monitor& heartbeat_monitor,
																		 shared::runtime::resource_ledger& resource_ledger,
																		 agent::controller_reporter& controller_reporter,
																		 agent::load_reporter& load_reporter) {
	if (!request.has_ds() || 0 == request.ds().ds_id()) {
		return write_stop_ds_error_response(rpc_context, ERROR_CODE_INVALID_ARGUMENT);
	}

	auto ds_id = request.ds().ds_id();
	if (!local_channel_service.has_ds(ds_id)) {
		return write_stop_ds_error_response(rpc_context, ERROR_CODE_DS_NOT_FOUND);
	}

	agent::downstream_packet_t stop_packet;
	stop_packet.ds_id = ds_id;
	stop_packet.sequence = runtime.allocate_sequence();
	stop_packet.payload = "control://stop_self";

	auto forward_result = local_channel_service.forward_to_ds(stop_packet);
	if (forward_result != 0) {
		return write_stop_ds_error_response(rpc_context, ERROR_CODE_STOP_FORWARD_FAILED);
	}

	auto mark_result = heartbeat_monitor.mark_exit_notified(ds_id);
	if (mark_result != 0) {
		return write_stop_ds_error_response(rpc_context, ERROR_CODE_DS_NOT_FOUND);
	}

	if (graceful_stop_timed_out(runtime, ds_id)) {
		auto cleanup_result = cleanup_stopped_ds(ds_id, resource_ledger, local_channel_service, controller_reporter);
		if (cleanup_result != 0) {
			return write_stop_ds_error_response(rpc_context, ERROR_CODE_STOP_CLEANUP_FAILED);
		}
	}

	auto snapshot = load_reporter.build_agent_load_snapshot();
	controller_reporter.report_agent_load(build_load_report(snapshot));
	return write_stop_ds_success_response(rpc_context, ds_id);
}

}  // namespace action
}  // namespace logic
}  // namespace service
}  // namespace dsa
}  // namespace atorbit
