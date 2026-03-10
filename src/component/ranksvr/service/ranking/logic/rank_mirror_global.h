#pragma once

#include <list>

#include <design_pattern/singleton.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "logic/rank_mirror_manager.h"
#include "rpc/rpc_context.h"

class rank_mirror_global : public util::design_pattern::singleton<rank_mirror_global> {
 public:
  int init();
  void tick();

  void add_dump_task(const dump_mirror_task_ptr& task);
  bool is_empty() const { return task_list_.empty(); }

 private:
  void add_failed_task(const dump_mirror_task_ptr& task);

  void async_tick_dump(rpc::context& ctx);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type tick_dump(rpc::context& ctx);

  bool is_dump_task_running();

 private:
  std::list<dump_mirror_task_ptr> task_list_;
  task_type_trait::task_type dump_task_;
};