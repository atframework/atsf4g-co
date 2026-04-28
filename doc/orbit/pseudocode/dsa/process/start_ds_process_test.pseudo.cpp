#include "start_ds_process.pseudo.h"

#include "../agent/local_channel_service.pseudo.h"

CASE_TEST(start_ds_process, launch_commits_running_capacity_and_binds_local_channel) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);
	atorbit::dsa::agent::local_channel_service local_channel_service;
	atorbit::dsa::process::start_ds_process launcher(&ledger, &local_channel_service);
	atorbit::dsa::process::start_ds_request_t request;

	request.request_id = 1001;
	request.ds_id = 2001;
	request.expected_cpu = 1.0;
	request.expected_memory_mb = 1024.0;

	ledger.reserve(request.ds_id, request.expected_cpu, request.expected_memory_mb);
	auto result = launcher.launch(request);
	auto snapshot = ledger.build_snapshot();

	CASE_EXPECT_EQ(static_cast<int>(atorbit::dsa::process::start_ds_status_t::k_ok), static_cast<int>(result.status));
	CASE_EXPECT_EQ(1, snapshot.running_ds_count);
	CASE_EXPECT_TRUE(local_channel_service.has_ds(request.ds_id));
	CASE_EXPECT_EQ(4321, result.process_id);
}

CASE_TEST(start_ds_process, rollback_failed_launch_releases_reserved_capacity_and_local_channel) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);
	atorbit::dsa::agent::local_channel_service local_channel_service;
	atorbit::dsa::process::start_ds_process launcher(&ledger, &local_channel_service);

	ledger.reserve(2001, 1.0, 1024.0);
	local_channel_service.register_ds(2001, "local://dsa_ds_channel", 4321);

	launcher.rollback_failed_launch(2001);
	auto snapshot = ledger.build_snapshot();

	CASE_EXPECT_EQ(0, snapshot.running_ds_count);
	CASE_EXPECT_EQ(0, snapshot.reserved_ds_count);
	CASE_EXPECT_TRUE(!local_channel_service.has_ds(2001));
}
