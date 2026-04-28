#include "disconnect_cleanup.pseudo.h"

CASE_TEST(disconnect_cleanup, cleanup_ds_exit_releases_owner_mapping_and_returns_owner) {
  atorbit::dsc::session::session_router session_router;
  atorbit::dsc::registry::disconnect_cleanup cleanup;
  atorbit::dsc::session::ds_composite_key_t ds_key;

  session_router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 42;
  session_router.bind_ds_owner(9527, ds_key);

  auto result = cleanup.cleanup_ds_exit(session_router, ds_key);

  CASE_EXPECT_TRUE(result.found_owner);
  CASE_EXPECT_EQ(9527, result.owner_unique_id);
  CASE_EXPECT_FALSE(session_router.validate_ds_owner(9527, ds_key));
}