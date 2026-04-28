#include "cluster_catalog.pseudo.h"

namespace atorbit {
namespace dsm {
namespace topology {

namespace {

static bool same_region_key(const char* left, const char* right) {
  return left == right;
}

static bool matches_query_filter(const controller_record_t& controller, const inventory_query_t& query) {
  if (query.has_controller_filter && controller.controller_id != query.controller_id) {
    return false;
  }

  if (query.has_region_filter && !same_region_key(controller.region, query.region)) {
    return false;
  }

  return true;
}

static bool matches_query_filter(const ds_record_t& ds_record, const inventory_query_t& query) {
  if (query.has_controller_filter && ds_record.controller_id != query.controller_id) {
    return false;
  }

  if (query.has_region_filter && !same_region_key(ds_record.region, query.region)) {
    return false;
  }

  return true;
}

}  // namespace

result_code_t cluster_catalog::upsert_controller(const controller_registration_t& registration) {
  if (0 == registration.controller_id || nullptr == registration.region) {
    return -1;
  }

  auto* controller = find_controller(registration.controller_id);
  if (nullptr == controller) {
    controller = allocate_controller(registration.controller_id);
  }

  if (nullptr == controller) {
    return -2;
  }

  controller->controller_id = registration.controller_id;
  controller->region = registration.region;
  controller->controller_addr = registration.controller_addr;
  controller->occupied = true;
  return 0;
}

result_code_t cluster_catalog::update_controller_snapshot(const controller_snapshot_t& snapshot) {
  if (0 == snapshot.controller_id) {
    return -1;
  }

  auto* controller = find_controller(snapshot.controller_id);
  if (nullptr == controller) {
    return -3;
  }

  controller->current_agent_count = snapshot.agent_count;
  controller->current_ds_count = snapshot.ds_count;
  controller->current_session_count = snapshot.session_count;
  controller->current_inflight_count = snapshot.inflight_count;
  controller->draining = snapshot.draining;
  return 0;
}

result_code_t cluster_catalog::replace_controller_ds_inventory(unsigned long long controller_id,
                                                               const ds_snapshot_record_t ds_records[],
                                                               unsigned long long count) {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return -3;
  }

  clear_controller_ds_inventory(controller_id);
  for (unsigned long long index = 0; index < count; ++index) {
    if (0 == ds_records[index].dsa_id || 0 == ds_records[index].ds_id) {
      return -1;
    }

    auto* output_record = allocate_ds_record();
    if (nullptr == output_record) {
      return -2;
    }

    output_record->controller_id = controller_id;
    output_record->region = controller->region;
    output_record->dsa_id = ds_records[index].dsa_id;
    output_record->ds_id = ds_records[index].ds_id;
    output_record->owner_unique_id = ds_records[index].owner_unique_id;
    output_record->running_state = ds_records[index].running_state;
    output_record->exit_reason = ds_records[index].exit_reason;
    output_record->last_active_millis = ds_records[index].last_active_millis;
    output_record->occupied = true;
  }

  return 0;
}

result_code_t cluster_catalog::build_inventory_summary(const inventory_query_t& query, inventory_summary_t& output) const {
  output = inventory_summary_t{};
  output.region = query.region;

  if (query.has_controller_filter) {
    const auto* controller = find_controller(query.controller_id);
    if (nullptr == controller) {
      return -3;
    }

    if (!matches_query_filter(*controller, query)) {
      return -4;
    }
  }

  for (const auto& controller : controllers_) {
    if (!controller.occupied || !matches_query_filter(controller, query)) {
      continue;
    }

    ++output.controller_count;
    output.agent_count += controller.current_agent_count;
    output.ds_count += controller.current_ds_count;
    output.session_count += controller.current_session_count;
    output.inflight_count += controller.current_inflight_count;
    if (controller.draining) {
      ++output.draining_controller_count;
    }

    if (nullptr == output.region) {
      output.region = controller.region;
    }
  }

  return 0;
}

unsigned long long cluster_catalog::list_controllers_by_query(const inventory_query_t& query,
                                                              controller_record_t output_records[],
                                                              unsigned long long capacity) const {
  if (nullptr == output_records || 0 == capacity) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (const auto& controller : controllers_) {
    if (!controller.occupied || !matches_query_filter(controller, query)) {
      continue;
    }

    output_records[output_count++] = controller;
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

unsigned long long cluster_catalog::count_ds_by_query(const inventory_query_t& query) const {
  unsigned long long output_count = 0;
  for (const auto& ds_record : ds_records_) {
    if (!ds_record.occupied || !matches_query_filter(ds_record, query)) {
      continue;
    }

    ++output_count;
  }

  return output_count;
}

unsigned long long cluster_catalog::list_ds_by_query(const inventory_query_t& query,
                                                     ds_record_t output_records[],
                                                     unsigned long long capacity) const {
  if (nullptr == output_records || 0 == capacity) {
    return 0;
  }

  unsigned long long output_count = 0;
  for (const auto& ds_record : ds_records_) {
    if (!ds_record.occupied || !matches_query_filter(ds_record, query)) {
      continue;
    }

    output_records[output_count++] = ds_record;
    if (output_count >= capacity) {
      break;
    }
  }

  return output_count;
}

bool cluster_catalog::has_controller(unsigned long long controller_id) const {
  return nullptr != find_controller(controller_id);
}

const char* cluster_catalog::get_controller_region(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return nullptr;
  }

  return controller->region;
}

unsigned long long cluster_catalog::get_controller_agent_count(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return 0;
  }

  return controller->current_agent_count;
}

unsigned long long cluster_catalog::get_controller_ds_count(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return 0;
  }

  return controller->current_ds_count;
}

unsigned long long cluster_catalog::get_controller_session_count(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return 0;
  }

  return controller->current_session_count;
}

unsigned long long cluster_catalog::get_controller_inflight_count(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return 0;
  }

  return controller->current_inflight_count;
}

bool cluster_catalog::is_controller_draining(unsigned long long controller_id) const {
  const auto* controller = find_controller(controller_id);
  if (nullptr == controller) {
    return false;
  }

  return controller->draining;
}

unsigned long long cluster_catalog::get_controller_count() const {
  unsigned long long count = 0;
  for (const auto& controller : controllers_) {
    if (controller.occupied) {
      ++count;
    }
  }

  return count;
}

unsigned long long cluster_catalog::get_region_controller_count(const char* region) const {
  if (nullptr == region) {
    return 0;
  }

  unsigned long long count = 0;
  for (const auto& controller : controllers_) {
    if (controller.occupied && same_region_key(controller.region, region)) {
      ++count;
    }
  }

  return count;
}

unsigned long long cluster_catalog::list_controllers(controller_record_t output_records[], unsigned long long capacity) const {
  if (nullptr == output_records || 0 == capacity) {
    return 0;
  }

  inventory_query_t query;
  return list_controllers_by_query(query, output_records, capacity);
}

controller_record_t* cluster_catalog::find_controller(unsigned long long controller_id) {
  for (auto& controller : controllers_) {
    if (controller.occupied && controller.controller_id == controller_id) {
      return &controller;
    }
  }

  return nullptr;
}

const controller_record_t* cluster_catalog::find_controller(unsigned long long controller_id) const {
  for (const auto& controller : controllers_) {
    if (controller.occupied && controller.controller_id == controller_id) {
      return &controller;
    }
  }

  return nullptr;
}

controller_record_t* cluster_catalog::allocate_controller(unsigned long long controller_id) {
  for (auto& controller : controllers_) {
    if (!controller.occupied) {
      controller.controller_id = controller_id;
      controller.occupied = true;
      return &controller;
    }
  }

  return nullptr;
}

void cluster_catalog::clear_controller_ds_inventory(unsigned long long controller_id) {
  for (auto& ds_record : ds_records_) {
    if (!ds_record.occupied || ds_record.controller_id != controller_id) {
      continue;
    }

    ds_record = ds_record_t{};
  }
}

ds_record_t* cluster_catalog::allocate_ds_record() {
  for (auto& ds_record : ds_records_) {
    if (!ds_record.occupied) {
      return &ds_record;
    }
  }

  return nullptr;
}

}  // namespace topology
}  // namespace dsm
}  // namespace atorbit