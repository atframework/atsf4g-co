// Copyright 2026 atframework
// Created by owent on 2026-10-30.
//

#include "logic/mail/mail_utils.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <string/string_format.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/com.struct.mail.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/excel/config_easy_api.h>
#include <config/excel/config_manager.h>
// #include <config/excel_config_condition_index.h>
#include <config/excel_config_const_index.h>
#include <config/logic_config.h>
// #include <logic/random_pool_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

// #include <logic/misc/logic_item.h>

#include <algorithm>
#include <list>
#include <string>
#include <vector>

#ifdef max
#  undef max
#endif

namespace atframework {
namespace mail {

using mail_template_engine_t = ::excel::config_group_t::mail_template_engine_t;

namespace {
struct ATFW_UTIL_SYMBOL_LOCAL mail_template_render_call_i18n {
  std::string text;
  explicit mail_template_render_call_i18n(const std::string& in) : text(in) {}

  bool operator()(std::ostream& os, mail_render_data_type&) {
    os << ::excel::get_i18n_text(text);
    return true;
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL mail_template_render_call_attachments_count {
  int mail_idx;
  explicit mail_template_render_call_attachments_count(const std::string& in) : mail_idx(0) {
    ::atfw::util::string::str2int(mail_idx, in.c_str());
  }

  bool operator()(std::ostream& os, mail_render_data_type& priv_data) {
    if (nullptr == priv_data.mail_content) {
      return false;
    }

    if (mail_idx < priv_data.mail_content->attachments_size()) {
      os << priv_data.mail_content->attachments(mail_idx).count();
    }

    return true;
  }
};

static std::string get_item_name(int32_t item_id) {
  // TODO(any): 接入服务端渲染多语言版本的道具名
  return atfw::util::string::format("[Item: {}]", item_id);  // logic_item::get_item_name(item_id);
}

struct ATFW_UTIL_SYMBOL_LOCAL mail_template_render_call_attachments_name {
  int mail_idx;
  explicit mail_template_render_call_attachments_name(const std::string& in) : mail_idx(0) {
    ::atfw::util::string::str2int(mail_idx, in.c_str());
  }

  bool operator()(std::ostream& os, mail_render_data_type& priv_data) {
    if (nullptr == priv_data.mail_content) {
      return false;
    }

    if (mail_idx < priv_data.mail_content->attachments_size()) {
      int32_t item_id = priv_data.mail_content->attachments(mail_idx).type_id();
      auto item_name = get_item_name(item_id);  // logic_item::get_item_name(item_id);
      if (item_name.empty() || atfw::util::string::trim(item_name.data(), item_name.size()).second == 0) {
        os << "[MISSING NAME:" << item_id << "]";
      } else {
        os << excel::get_i18n_text(static_cast<std::string>(item_name));
      }
    }

    return true;
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL mail_template_render_call_attachments_list {
  struct fmt_t {
    enum class type : uint8_t {
      kRaw = 0,
      kName,
      kCount,
    };

    fmt_t(type _t, const char* b, size_t len) : t(_t), raw(b, len) {}

    type t;
    std::string raw;
  };

  std::string seperator;
  std::list<fmt_t> formats;
  mail_template_render_call_attachments_list(const std::string& sep, const std::string& fmt) : seperator(sep) {
    size_t begin = 0;
    size_t end = 0;

    while (end < fmt.size()) {
      while (end < fmt.size() && fmt[end] != '%') {
        ++end;
      }

      if (end >= fmt.size()) {
        break;
      }

      if (end > begin) {
        formats.emplace_back(fmt_t::type::kRaw, &fmt[begin], end - begin);
        begin = end;
      }

      // % => convert data
      ++end;
      if (end >= fmt.size()) {
        begin = end;
        break;
      }

      if (fmt[end] == 'n') {
        formats.emplace_back(fmt_t::type::kName, &fmt[begin], end - begin + 1);
      } else if (fmt[end] == 'c') {
        formats.emplace_back(fmt_t::type::kCount, &fmt[begin], end - begin + 1);
      }

      ++end;
      begin = end;
    }

    if (end > begin && begin < fmt.size()) {
      formats.push_back(fmt_t(fmt_t::type::kRaw, &fmt[begin], end - begin));
    }
  }

  bool operator()(std::ostream& os, mail_render_data_type& priv_data) {
    if (nullptr == priv_data.mail_content) {
      return false;
    }

    for (int i = 0; i < priv_data.mail_content->attachments_size(); ++i) {
      if (i != 0) {
        os << seperator;
      }

      for (const auto& fmt : formats) {
        switch (fmt.t) {
          case fmt_t::type::kRaw: {
            os << fmt.raw;
            break;
          }
          case fmt_t::type::kName: {
            int32_t item_id = priv_data.mail_content->attachments(i).type_id();
            auto item_name = get_item_name(item_id);
            if (item_name.empty() || atfw::util::string::trim(item_name.data(), item_name.size()).second == 0) {
              os << "[MISSING NAME:" << item_id << "]";
            } else {
              os << excel::get_i18n_text(static_cast<std::string>(item_name));
            }
            break;
          }
          case fmt_t::type::kCount: {
            os << priv_data.mail_content->attachments(i).count();
            break;
          }
          default: {
            os << fmt.raw;
            break;
          }
        }
      }
    }

    return true;
  }
};

struct ATFW_UTIL_SYMBOL_LOCAL mail_template_render_call_extension {
  std::string extension_name;
  explicit mail_template_render_call_extension(const std::string& in) : extension_name(in) {}

  bool operator()(std::ostream& os, mail_render_data_type& priv_data) {
    if (nullptr == priv_data.extensions) {
      os << "[MISSING EXTENSION:" << extension_name << "]";
      return true;
    }

    auto iter = priv_data.extensions->find(extension_name);
    if (iter == priv_data.extensions->end()) {
      os << "[MISSING EXTENSION:" << extension_name << "]";
    } else {
      os.write(iter->second.data(), static_cast<std::streamsize>(iter->second.size()));
    }

    return true;
  }
};

static bool mail_template_render_getter_attachments_number(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  os << priv_data.mail_content->attachments_size();
  return true;
}

static bool mail_template_render_getter_sender_nick_name(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.sender) {
    return false;
  }

  if (!priv_data.sender->profile().custom_nick_name().empty()) {
    os << priv_data.sender->profile().custom_nick_name();
  } else if (!priv_data.sender->profile().nick_name().empty()) {
    os << priv_data.sender->profile().nick_name();
  } else {
    os << priv_data.sender->profile().user_id();
  }

  return true;
}

static bool mail_template_render_getter_receiver_nick_name(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.receiver) {
    return false;
  }

  if (!priv_data.receiver->profile().custom_nick_name().empty()) {
    os << priv_data.receiver->profile().custom_nick_name();
  } else if (!priv_data.receiver->profile().nick_name().empty()) {
    os << priv_data.receiver->profile().nick_name();
  } else {
    os << priv_data.receiver->profile().user_id();
  }

  return true;
}

static bool mail_template_render_getter_sender_logo_url(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.sender) {
    return false;
  }

  os << priv_data.sender->profile().logo_url();
  return true;
}

static bool mail_template_render_getter_receiver_logo_url(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.receiver) {
    return false;
  }

  os << priv_data.receiver->profile().logo_url();
  return true;
}

static bool mail_template_render_getter_sender_user_id(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.sender) {
    return false;
  }

  os << priv_data.sender->profile().user_id();
  return true;
}

static bool mail_template_render_getter_receiver_user_id(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.receiver) {
    return false;
  }

  os << priv_data.receiver->profile().user_id();
  return true;
}

static bool mail_template_render_getter_title(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  os << priv_data.mail_content->title();
  return true;
}

static bool mail_template_render_getter_show_time(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  char time_text[32] = {0};
  struct tm tout;
  time_t tin = static_cast<time_t>(priv_data.mail_content->show_time().seconds());
  UTIL_STRFUNC_LOCALTIME_S(&tin, &tout);
  strftime(time_text, sizeof(time_text) - 1, "%Y-%m-%d %H:%M:%S", &tout);
  os << &time_text[0];
  return true;
}

static bool mail_template_render_getter_start_time(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  char time_text[32] = {0};
  struct tm tout;
  time_t tin = static_cast<time_t>(priv_data.mail_content->start_time().seconds());
  UTIL_STRFUNC_LOCALTIME_S(&tin, &tout);
  strftime(time_text, sizeof(time_text) - 1, "%Y-%m-%d %H:%M:%S", &tout);
  os << &time_text[0];
  return true;
}

static bool mail_template_render_getter_expired_time(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  char time_text[32] = {0};
  struct tm tout;
  time_t tin = static_cast<time_t>(priv_data.mail_content->expired_time().seconds());
  UTIL_STRFUNC_LOCALTIME_S(&tin, &tout);
  strftime(time_text, sizeof(time_text) - 1, "%Y-%m-%d %H:%M:%S", &tout);
  os << &time_text[0];
  return true;
}

static bool mail_template_render_getter_delivery_time(std::ostream& os, mail_render_data_type& priv_data) {
  if (nullptr == priv_data.mail_content) {
    return false;
  }

  char time_text[32] = {0};
  struct tm tout;
  time_t tin = static_cast<time_t>(priv_data.mail_content->delivery_time().seconds());
  UTIL_STRFUNC_LOCALTIME_S(&tin, &tout);
  strftime(time_text, sizeof(time_text) - 1, "%Y-%m-%d %H:%M:%S", &tout);
  os << &time_text[0];
  return true;
}

static mail_template_engine_t::getter_fn_t template_getter_creator_for_mail(const std::string& name, const char*,
                                                                            const char*) {
  // TODO(owentou): 以后可以优化下直接静态索引
  if (0 == UTIL_STRFUNC_STRCASE_CMP("sender.nick_name", name.c_str())) {
    return mail_template_render_getter_sender_nick_name;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("receiver.nick_name", name.c_str())) {
    return mail_template_render_getter_receiver_nick_name;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("show_time", name.c_str())) {
    return mail_template_render_getter_show_time;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("start_time", name.c_str())) {
    return mail_template_render_getter_start_time;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("expired_time", name.c_str())) {
    return mail_template_render_getter_expired_time;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("delivery_time", name.c_str())) {
    return mail_template_render_getter_delivery_time;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("title", name.c_str())) {
    return mail_template_render_getter_title;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("sender.logo_url", name.c_str())) {
    return mail_template_render_getter_sender_logo_url;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("receiver.logo_url", name.c_str())) {
    return mail_template_render_getter_receiver_logo_url;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("sender.user_id", name.c_str())) {
    return mail_template_render_getter_sender_user_id;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("receiver.profile().user_id", name.c_str())) {
    return mail_template_render_getter_receiver_user_id;
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("attachments.count", name.c_str())) {
    return mail_template_render_getter_attachments_number;
  }

  return nullptr;
}

static mail_template_engine_t::call_fn_t template_call_creator_for_mail(const std::string& name,
                                                                        const std::vector<std::string>& params,
                                                                        const char*, const char*) {
  if (0 == UTIL_STRFUNC_STRCASE_CMP("i18n", name.c_str())) {
    if (params.empty()) {
      FWLOGERROR("i18n(KEY) can not be called without parameter");
      return nullptr;
    }

    return mail_template_render_call_i18n(params[0]);
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("attachments.count", name.c_str())) {
    if (params.empty()) {
      FWLOGERROR("attachments.count(N) can not be called without parameter");
      return nullptr;
    }

    return mail_template_render_call_attachments_count(params[0]);
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("attachments.name", name.c_str())) {
    if (params.empty()) {
      FWLOGERROR("attachments.name(N) can not be called without parameter");
      return nullptr;
    }

    return mail_template_render_call_attachments_name(params[0]);
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("attachments.list", name.c_str())) {
    if (params.size() < 2) {
      FWLOGERROR("attachments.list(SEP, FMT) require 2 parameters");
      return nullptr;
    }

    return mail_template_render_call_attachments_list(params[0], params[1]);
  }

  if (0 == UTIL_STRFUNC_STRCASE_CMP("extension", name.c_str())) {
    if (params.empty()) {
      FWLOGERROR("extension(NAME) can not be called without parameter");
      return nullptr;
    }

    return mail_template_render_call_extension(params[0]);
  }

  return nullptr;
}
}  // namespace

LOBBY_RPC_API int mail_setup_template_engine(::excel::config_group_t& group) {
  group.mail_template_engine_conf.core.enable_share_functions = true;
  group.mail_template_engine_conf.clear_cache();
  group.mail_template_engine.clear();
  group.mail_template_engine_conf.getter_fn_creator = template_getter_creator_for_mail;
  group.mail_template_engine_conf.call_creator_fn = template_call_creator_for_mail;

  int ret = 0;

  // 模板渲染流程
  // FIXME(owent):先不迁移多语言表

  // auto i18n_default_lang = excel::get_i18n_descriptor(group);
  // int32_t i18n_default_lang_id = 0;
  // if (nullptr != i18n_default_lang) {
  //   i18n_default_lang_id = i18n_default_lang->number();
  // }

  for (const auto& tmpl : group.ExcelMailTemplate.get_all_of_type_id()) {
    if (!tmpl.second || tmpl.second->type_id() <= 0) {
      continue;
    }

    ::excel::config_group_t::mail_template_lang_set_t& tmpl_lang_set =
        group.mail_template_engine[tmpl.second->type_id()];
    // for (auto& lang : excel::get_i18n_all_descriptors(group)) {
    int32_t lang_id = 0;
    gsl::string_view lang_name = "default";
    ::excel::config_group_t::mail_template_set_t& tmpl_set = tmpl_lang_set[lang_id];
    std::string error_msg;
    if (atfw::util::string::trim(tmpl.second->config().title().c_str(), tmpl.second->config().title().size()).second >
        0) {
      gsl::string_view i18n_title = excel::get_i18n_text(group, tmpl.second->config().title());
      tmpl_set.title = mail_template_engine_t::compile(i18n_title.data(), i18n_title.size(),
                                                       group.mail_template_engine_conf, &error_msg);
      if (!error_msg.empty()) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_SYNTAX_ERROR;
        FWLOGERROR("mail template {} has some syntax error in title for language {} :\n{}", tmpl.second->type_id(),
                   lang_name, error_msg);
      }
    }

    if (::atfw::util::string::trim(tmpl.second->config().content().c_str(), tmpl.second->config().content().size())
            .second > 0) {
      error_msg.clear();
      gsl::string_view i18n_content = excel::get_i18n_text(group, tmpl.second->config().content());
      tmpl_set.content = mail_template_engine_t::compile(i18n_content.data(), i18n_content.size(),
                                                         group.mail_template_engine_conf, &error_msg);
      if (!error_msg.empty()) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_SYNTAX_ERROR;
        FWLOGERROR("mail template {} has some syntax error in content for language {} :\n{}", tmpl.second->type_id(),
                   lang_name, error_msg);
      }
    }

    // if (lang->number() == i18n_default_lang_id) {
    //   if (tmpl_set.title) {
    //     tmpl_lang_set[0].title = tmpl_set.title;
    //   }
    //   if (tmpl_set.content) {
    //     tmpl_lang_set[0].content = tmpl_set.content;
    //   }
    // }
    // }
  }

  return ret;
}

// NOLINTBEGIN(bugprone-signed-bitwise)

LOBBY_RPC_API int32_t mail_render_template(
    rpc::context& ctx, int32_t mail_template_id, PROJECT_NAMESPACE_ID::DMailContent& out,
    const PROJECT_NAMESPACE_ID::DMailUserInfo& receiver, const PROJECT_NAMESPACE_ID::DMailUserInfo* sender,
    const std::unordered_map<std::string, gsl::string_view>* extensions, bool render_by_client,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset>* item_offest,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>* item_detail
    //, google::protobuf::RepeatedPtrField<tgf::DConditionRule>* extra_mail_limit
) {
  // 模板渲染流程
  // FIXME(owent):先不迁移多语言表

  int ret = 0;

  auto group = ::excel::config_manager::me()->get_current_config_group();
  if (!group) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_NOT_FOUND;
  }

  mail_render_data_type conf;
  conf.template_cfg = group->ExcelMailTemplate.get_by_type_id(mail_template_id);
  if (!conf.template_cfg) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_NOT_FOUND;
  }

  if (item_offest != nullptr && item_detail != nullptr) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_MAIL_ITEM_INVALID;
  }

  conf.sender = sender;
  conf.receiver = &receiver;
  conf.mail_content = &out;
  conf.extensions = extensions;
  PROJECT_NAMESPACE_ID::DMailUserInfo sys_admin;

  if (nullptr == conf.sender && conf.template_cfg->config().is_admin_sender()) {
    conf.sender = &sys_admin;
    sys_admin.mutable_profile()->set_gender(PROJECT_NAMESPACE_ID::EN_GENDER_TYPE_UNKNOWN);
    sys_admin.mutable_profile()->set_nick_name(group->const_settings.i18n_system_admin());
  }

  if (nullptr == conf.sender) {
    conf.sender = &receiver;
  }

  // 类型和时间
  out.set_major_type(conf.template_cfg->config().major_type());
  out.set_minor_type(conf.template_cfg->config().minor_type());
  protobuf_copy_message(*out.mutable_sender(), *conf.sender);

  // TODO(any): 限制条件
  /**
  excel::PROJECT_NAMESPACE_ID::config::config_set_ExcelConditionPool::condition_id_value_type condition_cfg = nullptr;
  do {
    int32_t condition_id = conf.template_cfg->config().condition_id();
    if (condition_id <= 0) {
      break;
    }
    condition_cfg = group->ExcelConditionPool.get_by_condition_id(condition_id);
    if (!condition_cfg) {
      FCTXLOGERROR(ctx, "condition {} for mail template {} not found", condition_id, mail_template_id);
      break;
    }

    protobuf_copy_message(*out.mutable_mail_limit(), condition_cfg->basic_limit().rule());
  } while (false);
  **/

  if (out.delivery_time().seconds() <= 0) {
    *out.mutable_delivery_time() = protobuf_from_system_clock(ctx.logical_now());
  }
  if (out.show_time().seconds() <= 0) {
    if (conf.template_cfg->config().show_time().seconds() > 0) {
      protobuf_copy_message(*out.mutable_show_time(), conf.template_cfg->config().show_time());
      // TODO(any): 限制条件
      // } else if (condition_cfg && condition_cfg->basic_limit().show_time().seconds() > 0) {
      //   protobuf_copy_message(*out.mutable_show_time(), condition_cfg->basic_limit().show_time());
    } else {
      protobuf_copy_message(*out.mutable_show_time(), out.delivery_time());
    }
  }

  // TODO(any): 限制条件
  // const PROJECT_NAMESPACE_ID::DConditionRuleRangeDatetime* current_valid_time = nullptr;
  // if (condition_cfg) {
  //   current_valid_time =
  //       excel::get_condition_pool_current_valid_time_range(condition_cfg->basic_limit(), now.seconds(), now.nanos());
  // }
  if (out.start_time().seconds() <= 0) {
    if (conf.template_cfg->config().start_time().seconds() > 0) {
      protobuf_copy_message(*out.mutable_start_time(), conf.template_cfg->config().start_time());
      // } else if (current_valid_time != nullptr && current_valid_time->begin_time().seconds() > 0) {
      //   out.set_start_time(current_valid_time->begin_time().seconds());
    } else {
      protobuf_copy_message(*out.mutable_start_time(), out.delivery_time());
    }
  }
  if (out.expired_time().seconds() <= 0) {
    std::chrono::system_clock::time_point expired_time{};
    if (conf.template_cfg->config().expired_time().seconds() > 0) {
      expired_time = protobuf_to_system_clock(conf.template_cfg->config().expired_time());
    } else if (conf.template_cfg->config().expired_duration().seconds() > 0) {
      expired_time = protobuf_to_system_clock(out.start_time()) +
                     protobuf_to_system_clock(conf.template_cfg->config().expired_duration());
      // } else if (current_valid_time != nullptr && current_valid_time->end_time().seconds() > 0) {
      //   expired_time = current_valid_time->end_time().seconds();
    } else {
      expired_time = protobuf_to_system_clock(out.start_time()) +
                     protobuf_to_system_clock(group->const_settings.mail_default_expire());
    }

    if (conf.template_cfg->config().is_padding_expired_time()) {
      expired_time = mail_padding_day_time(expired_time);
    }
    *out.mutable_expired_time() = protobuf_from_system_clock(expired_time);
  }
  if (conf.template_cfg->config().resolve_expired_time().seconds() > 0) {
    protobuf_copy_message(*out.mutable_resolve_expired_time(), conf.template_cfg->config().resolve_expired_time());
  }

  {
    int32_t flags = 0;
    if (conf.template_cfg->config().keep_after_take_attachments()) {
      flags |= PROJECT_NAMESPACE_ID::EN_MAIL_FLAG_KEEP_AFTER_TAKE_ATTACHMENTS;
    }
    out.set_flags(flags);
  }
  out.set_mail_template_id(mail_template_id);

  // TODO(any): 附件的随机池支持
  if (conf.template_cfg->config().attachments_size() > 0) {
    out.mutable_attachments()->Reserve(conf.template_cfg->config().attachments_size());
    for (const auto& item_cfg : conf.template_cfg->config().attachments()) {
      if (item_cfg.type_id() <= 0 || item_cfg.count() <= 0) {
        continue;
      }
      auto* unit = out.add_attachments();
      protobuf_copy_message(*unit, item_cfg);
    }
  }

  /**
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset> resolve_items;
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemOffset> offest_items;

  if (item_offest == nullptr && item_detail == nullptr) {
    // 填充模板内的道具
    protobuf_copy_message(offest_items, conf.template_cfg->config().attachments());
  }

  if (item_offest != nullptr) {
    protobuf_copy_message(offest_items, *item_offest);
  }

  int32_t index = 0;

  if (offest_items.size() != 0) {
    for (int i = 0; i < offest_items.size(); ++i) {
      auto& attachment_cfg = offest_items.Get(i);
      if (attachment_cfg.type_id() <= 0 || attachment_cfg.count() <= 0) {
        continue;
      }

      int32_t res = random_pool_manager::me()->append_random_with_pool(attachment_cfg, resolve_items);
      if (res < 0) {
        FCTXLOGERROR(ctx,
            "render mail attachment (type_id={}, count={}) of template {} for user {}({}) failed, "
            "res: "
            "{}({})",
            attachment_cfg.type_id(), attachment_cfg.count(), mail_template_id, receiver.profile().open_id(),
            receiver.profile().user_id(), res, protobuf_mini_dumper_get_error_msg(res));
      }
    }

    if (!resolve_items.empty()) {
      for (const auto& item : resolve_items) {
        auto unit = out.mutable_attachments()->Add();
        unit->set_index(index);
        protobuf_copy_message(*unit->mutable_item(), item);
        index++;
      }
    }
  }

  if (item_detail != nullptr) {
    for (const auto& item : *item_detail) {
      if (::ItemTypeConfig::is_EN_ITEM_TYPE_RANDOM_POOL_range(item.basic().type_id())) {
        return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_ATTACHMENTS_INVALID;
      }
      auto unit = out.mutable_attachments_detail()->Add();
      unit->set_index(index);
      protobuf_copy_message(*unit->mutable_item(), item);
      index++;
    }
  }
  **/

  // 如果由客户端渲染，只需要透传模板ID和参数即可
  if (render_by_client) {
    if (nullptr != conf.extensions) {
      for (const auto& ext_kv : *conf.extensions) {
        (*out.mutable_mail_template_extensions())[ext_kv.first] = static_cast<std::string>(ext_kv.second);
      }
    }
  } else {
    // auto i18n_lang = excel::get_i18n_descriptor(*group);
    int32_t i18n_lang_id = 0;
    // if (nullptr != i18n_lang) {
    //   i18n_lang_id = i18n_lang->number();
    // }

    // 渲染标题和正文模板
    auto tmpl_iter = group->mail_template_engine.find(mail_template_id);
    if (tmpl_iter == group->mail_template_engine.end()) {
      FCTXLOGERROR(ctx, "mail {} has configure but without a template", mail_template_id);
      out.set_title(static_cast<std::string>(excel::get_i18n_text(*group, conf.template_cfg->config().title())));
      out.set_content(static_cast<std::string>(excel::get_i18n_text(*group, conf.template_cfg->config().content())));
    } else {
      ::excel::config_group_t::mail_template_set_t* tmpl_set = nullptr;
      {
        auto lang_iter = tmpl_iter->second.find(i18n_lang_id);
        if (lang_iter == tmpl_iter->second.end()) {
          tmpl_set = &tmpl_iter->second[0];
        } else {
          tmpl_set = &lang_iter->second;
        }
      }

      if (tmpl_set->title) {
        std::string error_text;
        if (!tmpl_set->title->render(conf, *out.mutable_title(), &error_text)) {
          FCTXLOGERROR(ctx, "render mail title of template {} for user {}({}) failed: {}", mail_template_id,
                       receiver.profile().open_id(), receiver.profile().user_id(), error_text);

          ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_RENDER_FAILED;
        }
      }

      if (tmpl_set->content) {
        std::string error_text;
        if (!tmpl_set->content->render(conf, *out.mutable_content(), &error_text)) {
          FCTXLOGERROR(ctx, "render mail content of template {} for user {}({}) failed: {}", mail_template_id,
                       receiver.profile().open_id(), receiver.profile().user_id(), error_text);

          ret = PROJECT_NAMESPACE_ID::EN_ERR_MAIL_TEMPLATE_RENDER_FAILED;
        }
      }
    }
  }

  return ret;
}

LOBBY_RPC_API int32_t mail_is_valid(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record) {
  if (record.mail_id() == 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
  }

  if (record.major_type() == 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_INVALID_MAJOR_TYPE;
  }

  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (ctx.logical_now() > protobuf_to_system_clock(record.expired_time()) + time_tolerate) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_EXPIRED;
  }

  if (0 != (record.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
  }

  return 0;
}

LOBBY_RPC_API int32_t mail_is_valid(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content,
                                    std::chrono::system_clock::time_point expired_time) {
  if (content.mail_id() == 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
  }

  if (content.major_type() == 0) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_INVALID_MAJOR_TYPE;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now > expired_time + time_tolerate) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_EXPIRED;
  }

  if (now + time_tolerate < protobuf_to_system_clock(content.start_time())) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_START;
  }

  if (0 != (content.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return PROJECT_NAMESPACE_ID::EN_ERR_MAIL_NOT_FOUND;
  }

  return 0;
}

LOBBY_RPC_API bool mail_is_expired_or_removed(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record) {
  if (record.mail_id() == 0) {
    return true;
  }

  if (record.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now > protobuf_to_system_clock(record.expired_time()) + time_tolerate) {
    return true;
  }

  if (0 != (record.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return true;
  }

  return false;
}

LOBBY_RPC_API bool mail_is_expired_or_removed(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content) {
  if (content.mail_id() == 0) {
    return true;
  }

  if (content.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now > protobuf_to_system_clock(content.expired_time()) + time_tolerate) {
    return true;
  }

  if (0 != (content.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return true;
  }

  return false;
}

LOBBY_RPC_API bool mail_is_removed(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record) {
  if (record.mail_id() == 0) {
    return true;
  }

  if (record.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now > protobuf_to_system_clock(record.remove_time()) + time_tolerate) {
    return true;
  }

  if (0 != (record.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return true;
  }

  return false;
}

LOBBY_RPC_API bool mail_is_removed(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content) {
  if (content.mail_id() == 0) {
    return true;
  }

  if (content.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now > protobuf_to_system_clock(content.remove_time()) + time_tolerate) {
    return true;
  }

  if (0 != (content.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return true;
  }

  return false;
}

LOBBY_RPC_API bool mail_is_history_removable(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailRecord& record) {
  if (record.mail_id() == 0) {
    return true;
  }

  if (record.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now >
      (std::max)(protobuf_to_system_clock(record.expired_time()), protobuf_to_system_clock(record.remove_time())) +
          time_tolerate) {
    return true;
  }

  // 非全服邮件标记移除了就可以移除历史记录
  if (!record.is_global_mail() && 0 != (record.status() & PROJECT_NAMESPACE_ID::EN_MAIL_ST_REMOVED)) {
    return true;
  }

  return false;
}

// NOLINTEND(bugprone-signed-bitwise)

LOBBY_RPC_API bool mail_is_history_removable(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DMailContent& content) {
  if (content.mail_id() == 0) {
    return true;
  }

  if (content.major_type() == 0) {
    return true;
  }

  auto now = ctx.logical_now();
  auto time_tolerate = std::chrono::seconds(logic_config::me()->get_const_settings().time_tolerate());
  if (now >
      (std::max)(protobuf_to_system_clock(content.expired_time()), protobuf_to_system_clock(content.remove_time())) +
          time_tolerate) {
    return true;
  }

  return false;
}

LOBBY_RPC_API void mail_merge_content_and_record(rpc::context& /*ctx*/, PROJECT_NAMESPACE_ID::DMailContent& out,
                                                 const PROJECT_NAMESPACE_ID::DMailContent& content,
                                                 const PROJECT_NAMESPACE_ID::DMailRecord& record) {
  using std::max;

  if (&out != &content) {
    protobuf_copy_message(out, content);
  }

  out.set_status(record.status());
  // 时间要使用record里的
  protobuf_copy_message(*out.mutable_delivery_time(), record.delivery_time());
  protobuf_copy_message(*out.mutable_start_time(), record.start_time());
  protobuf_copy_message(*out.mutable_show_time(), record.show_time());
  protobuf_copy_message(*out.mutable_expired_time(), record.expired_time());
  protobuf_copy_message(*out.mutable_resolve_expired_time(), record.resolve_expired_time());
  protobuf_copy_message(*out.mutable_remove_time(),
                        protobuf_from_system_clock((std::max)(protobuf_to_system_clock(record.expired_time()),
                                                              protobuf_to_system_clock(record.remove_time()))));
}

LOBBY_RPC_API void mail_fill_admin_sender(PROJECT_NAMESPACE_ID::DMailUserInfo& sender) {
  sender.mutable_profile()->set_gender(PROJECT_NAMESPACE_ID::EN_GENDER_TYPE_UNKNOWN);

  auto group = ::excel::config_manager::me()->get_current_config_group();
  if (group) {
    sender.mutable_profile()->set_nick_name(group->const_settings.i18n_system_admin());
  }
}

LOBBY_RPC_API std::chrono::system_clock::time_point mail_padding_day_time(std::chrono::system_clock::time_point in,
                                                                          std::chrono::system_clock::duration off) {
  time_t in_unix_sec =
      static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(in.time_since_epoch()).count());
  time_t off_sec = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(off).count());
  time_t ret = ::atfw::util::time::time_utility::get_any_day_offset(in_unix_sec, off_sec);
  if (ret < in_unix_sec) {
    ret += ::atfw::util::time::time_utility::DAY_SECONDS;
  }

  return std::chrono::system_clock::from_time_t(ret) + (off - std::chrono::seconds(off_sec));
}

}  // namespace mail
}  // namespace atframework
