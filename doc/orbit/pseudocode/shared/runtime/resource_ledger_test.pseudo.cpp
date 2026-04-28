#include "resource_ledger.pseudo.h"

CASE_TEST(resource_ledger, reserve_commit_and_release_updates_snapshot) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);

	auto reserve_result = ledger.reserve(2001, 1.0, 1024.0);
	auto commit_result = ledger.commit_running(2001);
	auto snapshot_after_commit = ledger.build_snapshot();
	auto release_result = ledger.release(2001);
	auto snapshot_after_release = ledger.build_snapshot();

	CASE_EXPECT_EQ(static_cast<int>(atorbit::shared::runtime::ledger_result_code_t::k_ok),
								 static_cast<int>(reserve_result));
	CASE_EXPECT_EQ(static_cast<int>(atorbit::shared::runtime::ledger_result_code_t::k_ok),
								 static_cast<int>(commit_result));
	CASE_EXPECT_EQ(1, snapshot_after_commit.running_ds_count);
	CASE_EXPECT_EQ(static_cast<int>(atorbit::shared::runtime::ledger_result_code_t::k_ok),
								 static_cast<int>(release_result));
	CASE_EXPECT_EQ(0, snapshot_after_release.running_ds_count);
}

CASE_TEST(resource_ledger, apply_actual_usage_can_make_ledger_oversubscribed) {
	atorbit::shared::runtime::resource_ledger ledger(2.0, 2048.0);

	ledger.reserve(2001, 1.0, 1024.0);
	ledger.commit_running(2001);
	auto apply_result = ledger.apply_actual_usage(2001, 3.0, 3072.0);

	CASE_EXPECT_EQ(static_cast<int>(atorbit::shared::runtime::ledger_result_code_t::k_ok),
								 static_cast<int>(apply_result));
	CASE_EXPECT_TRUE(ledger.is_oversubscribed());
}

CASE_TEST(resource_ledger, select_oom_candidate_returns_largest_running_ds) {
	atorbit::shared::runtime::resource_ledger ledger(8.0, 8192.0);

	ledger.reserve(2001, 1.0, 1024.0);
	ledger.commit_running(2001);
	ledger.apply_actual_usage(2001, 1.0, 1536.0);
	ledger.reserve(2002, 1.0, 1024.0);
	ledger.commit_running(2002);
	ledger.apply_actual_usage(2002, 1.0, 2048.0);

	auto candidate = ledger.select_oom_candidate();

	CASE_EXPECT_TRUE(candidate.found);
	CASE_EXPECT_EQ(2002, candidate.ds_id);
}