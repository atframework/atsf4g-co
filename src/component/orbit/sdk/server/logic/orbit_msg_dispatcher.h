#pragma once

#include <config/compiler_features.h>
#include <design_pattern/singleton.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/descriptor.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <string>

#include <dispatcher/dispatcher_implement.h>
#include <dispatcher/dispatcher_type_defines.h>
#include <protocol/common/orbit.common.pb.h>
#include <rpc/rpc_common_types.h>

#ifndef ORBIT_SERVER_SERVICE_API
#  define ORBIT_SERVER_SERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class orbit_msg_dispatcher : public dispatcher_implement {
 public:
  using msg_raw_t = dispatcher_implement::msg_raw_t;

#if defined(ORBIT_SERVER_SDK_DLL) && ORBIT_SERVER_SDK_DLL
#  if defined(ORBIT_SERVER_SDK_NATIVE) && ORBIT_SERVER_SDK_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(orbit_msg_dispatcher)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(orbit_msg_dispatcher)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(orbit_msg_dispatcher)
#endif

 private:
  ORBIT_SERVER_SERVICE_API orbit_msg_dispatcher();

 public:
  ORBIT_SERVER_SERVICE_API virtual ~orbit_msg_dispatcher();

  ORBIT_SERVER_SERVICE_API int32_t init() override;

  ORBIT_SERVER_SERVICE_API const char *name() const override;

  ORBIT_SERVER_SERVICE_API uint64_t pick_msg_task_id(msg_raw_t &raw_msg) override;

  ORBIT_SERVER_SERVICE_API const std::string &pick_rpc_name(msg_raw_t &raw_msg) override;
  ORBIT_SERVER_SERVICE_API const std::string &pick_rpc_name(const orbit::OrbitRpcMessage &orbit_msg);

  ORBIT_SERVER_SERVICE_API int32_t dispatch(rpc::context &ctx, const std::string &client_id, const std::string &data);

  ORBIT_SERVER_SERVICE_API void on_create_task_failed(dispatcher_start_data_type &start_data,
                                                      int32_t error_code) override;

  ORBIT_SERVER_SERVICE_API uint64_t allocate_sequence();

  ORBIT_SERVER_SERVICE_API int32_t send_to_client_no_wait(rpc::context &ctx, const std::string &client_id,
                                                          orbit::OrbitRpcMessage &orbit_msg);
  ORBIT_SERVER_SERVICE_API rpc::result_code_type send_to_client(rpc::context &ctx, const std::string &client_id,
                                                                orbit::OrbitRpcMessage &orbit_msg);

  ORBIT_SERVER_SERVICE_API static void *make_private_data(const std::string &client_id) noexcept;
  ORBIT_SERVER_SERVICE_API static const std::string &get_source_client_id(const void *private_data) noexcept;

 private:
  uint64_t sequence_allocator_;
};