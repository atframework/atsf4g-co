#pragma once

// Phase 5.2 / 5.4
// 目标: 固化 DSM 侧 Region / Controller / DS 级 inventory 聚合落点。
// 未来真实落点: src/dsm/topology/cluster_catalog.cpp

namespace atorbit {
namespace dsm {
namespace topology {

using result_code_t = int;

struct controller_registration_t {
  unsigned long long controller_id = 0;
  const char* region = nullptr;
  const char* controller_addr = nullptr;
};

struct controller_record_t {
  unsigned long long controller_id = 0;
  const char* region = nullptr;
  const char* controller_addr = nullptr;
  unsigned long long current_agent_count = 0;
  unsigned long long current_ds_count = 0;
  unsigned long long current_session_count = 0;
  unsigned long long current_inflight_count = 0;
  bool draining = false;
  bool occupied = false;
};

struct controller_snapshot_t {
  unsigned long long controller_id = 0;
  unsigned long long agent_count = 0;
  unsigned long long ds_count = 0;
  unsigned long long session_count = 0;
  unsigned long long inflight_count = 0;
  bool draining = false;
};

struct ds_snapshot_record_t {
  unsigned long long dsa_id = 0;
  unsigned long long ds_id = 0;
  unsigned long long owner_unique_id = 0;
  int running_state = 0;
  int exit_reason = 0;
  unsigned long long last_active_millis = 0;
};

struct ds_record_t {
  unsigned long long controller_id = 0;
  const char* region = nullptr;
  unsigned long long dsa_id = 0;
  unsigned long long ds_id = 0;
  unsigned long long owner_unique_id = 0;
  int running_state = 0;
  int exit_reason = 0;
  unsigned long long last_active_millis = 0;
  bool occupied = false;
};

struct inventory_query_t {
  bool has_region_filter = false;
  const char* region = nullptr;
  bool has_controller_filter = false;
  unsigned long long controller_id = 0;
};

struct inventory_summary_t {
  const char* region = nullptr;
  unsigned long long controller_count = 0;
  unsigned long long agent_count = 0;
  unsigned long long ds_count = 0;
  unsigned long long session_count = 0;
  unsigned long long inflight_count = 0;
  unsigned long long draining_controller_count = 0;
};

class cluster_catalog {
public:
  result_code_t upsert_controller(const controller_registration_t& registration);
  result_code_t update_controller_snapshot(const controller_snapshot_t& snapshot);
  result_code_t replace_controller_ds_inventory(unsigned long long controller_id,
                                                const ds_snapshot_record_t ds_records[],
                                                unsigned long long count);
  result_code_t build_inventory_summary(const inventory_query_t& query, inventory_summary_t& output) const;
  unsigned long long list_controllers_by_query(const inventory_query_t& query,
                                               controller_record_t output_records[],
                                               unsigned long long capacity) const;
  unsigned long long count_ds_by_query(const inventory_query_t& query) const;
  unsigned long long list_ds_by_query(const inventory_query_t& query,
                                      ds_record_t output_records[],
                                      unsigned long long capacity) const;
  bool has_controller(unsigned long long controller_id) const;
  const char* get_controller_region(unsigned long long controller_id) const;
  unsigned long long get_controller_agent_count(unsigned long long controller_id) const;
  unsigned long long get_controller_ds_count(unsigned long long controller_id) const;
  unsigned long long get_controller_session_count(unsigned long long controller_id) const;
  unsigned long long get_controller_inflight_count(unsigned long long controller_id) const;
  bool is_controller_draining(unsigned long long controller_id) const;
  unsigned long long get_controller_count() const;
  unsigned long long get_region_controller_count(const char* region) const;
  unsigned long long list_controllers(controller_record_t output_records[], unsigned long long capacity) const;

private:
  controller_record_t* find_controller(unsigned long long controller_id);
  const controller_record_t* find_controller(unsigned long long controller_id) const;
  controller_record_t* allocate_controller(unsigned long long controller_id);
  void clear_controller_ds_inventory(unsigned long long controller_id);
  ds_record_t* allocate_ds_record();

private:
  controller_record_t controllers_[64];
  ds_record_t ds_records_[512];
};

}  // namespace topology
}  // namespace dsm
}  // namespace atorbit