#include "heartbeat_monitor.pseudo.h"

#include "../agent/controller_reporter.pseudo.h"
#include "../agent/local_channel_service.pseudo.h"

CASE_TEST(heartbeat_monitor, refresh_heartbeat_deadline_updates_actual_usage_in_ledger) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);
	atorbit::dsa::agent::local_channel_service local_channel_service;
	atorbit::dsa::agent::controller_reporter controller_reporter;
	atorbit::dsa::heartbeat::heartbeat_monitor monitor(&ledger, &local_channel_service, &controller_reporter);

	ledger.reserve(2001, 1.0, 1024.0);
	ledger.commit_running(2001);
	monitor.track_ds(2001, 4321, 1000, 30000);

	auto refresh_result = monitor.refresh_heartbeat_deadline(2001, 2000, 30000, 1.5, 1536.0);
	auto snapshot = ledger.build_snapshot();

	CASE_EXPECT_EQ(0, refresh_result);
	CASE_EXPECT_EQ(1, snapshot.running_ds_count);
	CASE_EXPECT_EQ(1.5, snapshot.cpu_used);
	CASE_EXPECT_EQ(1536.0, snapshot.memory_used_mb);
}

CASE_TEST(heartbeat_monitor, scan_timeout_releases_timed_out_ds_resources_and_channel) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);
	atorbit::dsa::agent::local_channel_service local_channel_service;
	atorbit::dsa::agent::controller_reporter controller_reporter;
	atorbit::dsa::heartbeat::heartbeat_monitor monitor(&ledger, &local_channel_service, &controller_reporter);

	ledger.reserve(2001, 1.0, 1024.0);
	ledger.commit_running(2001);
	local_channel_service.register_ds(2001, "local://dsa_ds_channel", 4321);
	monitor.track_ds(2001, 4321, 1000, 100);

	auto scan_result = monitor.scan_timeout(1200);
	auto snapshot = ledger.build_snapshot();

	CASE_EXPECT_EQ(0, scan_result);
	CASE_EXPECT_EQ(0, snapshot.running_ds_count);
	CASE_EXPECT_TRUE(!local_channel_service.has_ds(2001));
}
