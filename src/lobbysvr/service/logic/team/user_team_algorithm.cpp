// Copyright 2026 atframework

#include "logic/team/user_team_algorithm.h"

#ifdef GetMessage
#  undef GetMessage
#endif

namespace {
// 由 module oneof 中被设置的 Message 字段取二级 data_type oneof 中被设置字段的 field number，
// 按 (module_id << 32) | data_type_id 打包共享数据 key(见 com.struct.team.shared.proto 的 Key 算法约定)。
// team 与 team member 的 module 消息结构一致，统一由此实现，避免两份反射逻辑漂移
int64_t make_shared_data_key_impl(const google::protobuf::Message& data, int32_t module_id) {
  int32_t data_type_id = 0;

  do {
    if (module_id == 0) {
      break;
    }

    const auto* fds = data.GetDescriptor()->FindFieldByNumber(module_id);
    if (fds == nullptr) {
      break;
    }

    // 二级仅允许Message类型，其他说明配置错误
    if (fds->is_repeated() || fds->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE ||
        fds->message_type() == nullptr) {
      break;
    }

    const auto& sub_msg = (data.GetReflection()->GetMessage)(data, fds);
    int32_t oneof_count = sub_msg.GetDescriptor()->oneof_decl_count();
    for (int32_t i = 0; i < oneof_count; ++i) {
      const auto* oneof_desc = sub_msg.GetDescriptor()->oneof_decl(i);
      if (oneof_desc == nullptr) {
        continue;
      }
      const auto* field_desc = sub_msg.GetReflection()->GetOneofFieldDescriptor(sub_msg, oneof_desc);
      if (field_desc == nullptr) {
        continue;
      }
      data_type_id = field_desc->number();
    }
  } while (false);

  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  return (static_cast<int64_t>(module_id) << 32) | static_cast<int64_t>(data_type_id);
}
}  // namespace

int64_t user_team_algorithm::make_team_shared_data_key(const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) {
  return make_shared_data_key_impl(data, data.module_type_case());
}

int64_t user_team_algorithm::make_team_member_shared_data_key(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data) {
  return make_shared_data_key_impl(data, data.module_type_case());
}

bool user_team_algorithm::allow_client_update_team_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamSharedDataModule& data) {
  switch (data.module_type_case()) {
    case PROJECT_NAMESPACE_ID::DTeamSharedDataModule::kBattle: {
      return data.battle().data_type_case() == PROJECT_NAMESPACE_ID::DTeamSharedDataTypeBattle::kMatching;
    }
    default:
      return false;
  }
}

bool user_team_algorithm::allow_client_update_team_member_shared_data(
    const PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule& data) {
  switch (data.module_type_case()) {
    case PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule::kBattle: {
      return data.battle().data_type_case() == PROJECT_NAMESPACE_ID::DTeamMemberSharedDataTypeBattle::kReady;
    }
    default:
      return false;
  }
}
