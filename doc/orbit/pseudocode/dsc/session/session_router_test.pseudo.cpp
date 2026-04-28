#include "session_router.pseudo.h"

CASE_TEST(session_router, connect_rejects_duplicate_live_connection_for_same_unique_id) {
  atorbit::dsc::session::session_router router;

  auto first_result = router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  auto duplicate_result = router.connect(9527, 1002, "dsc://region-cn-east/controller-a");

  CASE_EXPECT_EQ(0, first_result);
  CASE_EXPECT_EQ(-2, duplicate_result);
}

CASE_TEST(session_router, reconnect_reuses_existing_ds_mapping_when_old_connection_is_disconnected) {
  atorbit::dsc::session::session_router router;
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);
  router.mark_disconnected(9527, 1001);

  auto reconnect_result = router.reconnect(9527, 1002, "dsc://region-cn-east/controller-a", 101);

  CASE_EXPECT_EQ(0, reconnect_result);
  CASE_EXPECT_EQ(1, router.get_owned_ds_count(9527));
  CASE_EXPECT_TRUE(router.validate_ds_owner(9527, ds_key));
}

CASE_TEST(session_router, reconnect_rejects_route_mismatch_when_sticky_controller_changes) {
  atorbit::dsc::session::session_router router;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.mark_disconnected(9527, 1001);

  auto reconnect_result = router.reconnect(9527, 1002, "dsc://region-cn-east/controller-b", 101);

  CASE_EXPECT_EQ(-4, reconnect_result);
}

CASE_TEST(session_router, validate_ds_owner_rejects_owner_mismatch) {
  atorbit::dsc::session::session_router router;
  atorbit::dsc::session::ds_composite_key_t ds_key;
  ds_key.dsa_id = 7001;
  ds_key.ds_id = 8001;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.connect(9528, 1002, "dsc://region-cn-east/controller-a");
  router.bind_ds_owner(9527, ds_key);

  CASE_EXPECT_TRUE(router.validate_ds_owner(9527, ds_key));
  CASE_EXPECT_FALSE(router.validate_ds_owner(9528, ds_key));
}

CASE_TEST(session_router, is_connected_returns_false_after_mark_disconnected) {
  atorbit::dsc::session::session_router router;

  router.connect(9527, 1001, "dsc://region-cn-east/controller-a");
  router.mark_disconnected(9527, 1001);

  CASE_EXPECT_FALSE(router.is_connected(9527));
}