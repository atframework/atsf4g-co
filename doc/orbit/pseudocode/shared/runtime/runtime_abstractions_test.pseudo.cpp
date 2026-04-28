#include "runtime_abstractions.pseudo.h"

CASE_TEST(runtime_abstractions, build_runtime_creates_all_core_handles) {
  auto runtime = atorbit::shared::runtime::runtime_environment::build_runtime(0, nullptr);

  CASE_EXPECT_TRUE(runtime.app_handle().exists());
  CASE_EXPECT_TRUE(runtime.allocate_request_id() > 0);
  CASE_EXPECT_TRUE(runtime.allocate_sequence() > 0);
}

CASE_TEST(runtime_abstractions, build_shared_context_binds_discovery_and_metrics) {
  auto runtime = atorbit::shared::runtime::runtime_environment::build_runtime(0, nullptr);
  auto context = atorbit::shared::runtime::service_shared_context::build_shared_context(runtime);

  CASE_EXPECT_EQ(0, context.shutdown());
}
