// Copyright 2023 atframework
//

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

#include "utility/rapid_json_helper.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/repeated_field.h>

#include <protocol/pbdesc/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <common/string_oprs.h>
#include <string/tquerystring.h>

#include <assert.h>

#include <utility>
#include <vector>

#if defined(GetMessage)
#  undef GetMessage
#endif

namespace detail {
static void load_field_string_filter(const std::string& input, rapidjson::Value& output, rapidjson::Document& doc,
                                     const rapidsjon_helper_load_options& options) {
  switch (options.string_mode) {
    case rapidsjon_helper_string_mode::URI: {
      std::string strv = util::uri::encode_uri(input.c_str(), input.size());
      output.SetString(strv.c_str(), static_cast<rapidjson::SizeType>(strv.size()), doc.GetAllocator());
      break;
    }
    case rapidsjon_helper_string_mode::URI_COMPONENT: {
      std::string strv = util::uri::encode_uri_component(input.c_str(), input.size());
      output.SetString(strv.c_str(), static_cast<rapidjson::SizeType>(strv.size()), doc.GetAllocator());
      break;
    }
    default: {
      output.SetString(input.c_str(), static_cast<rapidjson::SizeType>(input.size()), doc.GetAllocator());
      break;
    }
  }
}

static void load_field_item(rapidjson::Value& parent, const ::google::protobuf::Message& src,
                            const ::google::protobuf::FieldDescriptor* fds, rapidjson::Document& doc,
                            const rapidsjon_helper_load_options& options) {
  if (nullptr == fds) {
    return;
  }

  if (!parent.IsObject()) {
    parent.SetObject();
  }

  const char* key_name = fds->name().c_str();
  if (fds->options().HasExtension(atframework::field_json_options)) {
    const atframework::JsonOptions& field_json_options = fds->options().GetExtension(atframework::field_json_options);
    if (!field_json_options.alias_key_name().empty()) {
      key_name = field_json_options.alias_key_name().c_str();
    }
  }

  switch (fds->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedInt32(src, fds, i), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidsjon_helper_mutable_set_member(parent, key_name, src.GetReflection()->GetInt32(src, fds), doc);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          int64_t int_val = src.GetReflection()->GetRepeatedInt64(src, fds, i);
          if (options.convert_large_number_to_string && int_val > std::numeric_limits<int32_t>::max()) {
            char str_val[24] = {0};
            util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
            rapidjson::Value v;
            v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
            rapidsjon_helper_append_to_list(ls, std::move(v), doc);
          } else {
            rapidsjon_helper_append_to_list(ls, int_val, doc);
          }
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        int64_t int_val = src.GetReflection()->GetInt64(src, fds);
        if (options.convert_large_number_to_string && int_val > std::numeric_limits<int32_t>::max()) {
          char str_val[24] = {0};
          util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
          rapidjson::Value v;
          v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
          rapidsjon_helper_mutable_set_member(parent, key_name, std::move(v), doc);
        } else {
          rapidsjon_helper_mutable_set_member(parent, key_name, int_val, doc);
        }
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          uint32_t int_val = src.GetReflection()->GetRepeatedUInt32(src, fds, i);
          if (options.convert_large_number_to_string &&
              int_val > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            char str_val[24] = {0};
            util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
            rapidjson::Value v;
            v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
            rapidsjon_helper_append_to_list(ls, std::move(v), doc);
          } else {
            rapidsjon_helper_append_to_list(ls, int_val, doc);
          }
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        uint32_t int_val = src.GetReflection()->GetUInt32(src, fds);
        if (options.convert_large_number_to_string &&
            int_val > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
          char str_val[24] = {0};
          util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
          rapidjson::Value v;
          v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
          rapidsjon_helper_mutable_set_member(parent, key_name, std::move(v), doc);
        } else {
          rapidsjon_helper_mutable_set_member(parent, key_name, int_val, doc);
        }
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          uint64_t int_val = src.GetReflection()->GetRepeatedUInt64(src, fds, i);
          if (options.convert_large_number_to_string &&
              int_val > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            char str_val[24] = {0};
            util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
            rapidjson::Value v;
            v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
            rapidsjon_helper_append_to_list(ls, std::move(v), doc);
          } else {
            rapidsjon_helper_append_to_list(ls, int_val, doc);
          }
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        uint64_t int_val = src.GetReflection()->GetUInt64(src, fds);
        if (options.convert_large_number_to_string &&
            int_val > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
          char str_val[24] = {0};
          util::string::int2str(str_val, sizeof(str_val) - 1, int_val);
          rapidjson::Value v;
          v.SetString(str_val, static_cast<rapidjson::SizeType>(strlen(str_val)), doc.GetAllocator());
          rapidsjon_helper_mutable_set_member(parent, key_name, std::move(v), doc);
        } else {
          rapidsjon_helper_mutable_set_member(parent, key_name, int_val, doc);
        }
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
      // if type is bytes, skip display
      if (fds->type() == ::google::protobuf::FieldDescriptor::TYPE_BYTES) {
        break;
      }

      std::string empty;
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidjson::Value v;
          load_field_string_filter(src.GetReflection()->GetRepeatedStringReference(src, fds, i, &empty), v, doc,
                                   options);
          rapidsjon_helper_append_to_list(ls, std::move(v), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidjson::Value v;
        load_field_string_filter(src.GetReflection()->GetStringReference(src, fds, &empty), v, doc, options);
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(v), doc);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
      if (fds->is_map() && nullptr != fds->message_type()) {
        const ::google::protobuf::FieldDescriptor* map_key_fds = fds->message_type()->map_key();
        const ::google::protobuf::FieldDescriptor* map_value_fds = fds->message_type()->map_value();
        if (nullptr == map_key_fds || nullptr == map_value_fds) {
          break;
        }

        rapidjson::Value obj;
        obj.SetObject();

        ::google::protobuf::RepeatedFieldRef<::google::protobuf::Message> data =
            src.GetReflection()->GetRepeatedFieldRef<::google::protobuf::Message>(src, fds);
        if (obj.IsObject()) {
          for (int i = 0; i < data.size(); ++i) {
            rapidjson::Value map_key;
            rapidjson::Value map_value;
            load_field_item(obj, data.Get(i, nullptr), map_key_fds, doc, options);
            load_field_item(obj, data.Get(i, nullptr), map_value_fds, doc, options);
            if (!map_key.IsNull() && obj.MemberEnd() == obj.FindMember(map_key)) {
              obj.AddMember(map_key, map_value, doc.GetAllocator());
            }
          }
        }

        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(obj), doc);
      } else if (fds->is_repeated()) {
        rapidjson::Value ls;
        ls.SetArray();
        ::google::protobuf::RepeatedFieldRef<::google::protobuf::Message> data =
            src.GetReflection()->GetRepeatedFieldRef<::google::protobuf::Message>(src, fds);
        if (ls.IsArray()) {
          for (int i = 0; i < data.size(); ++i) {
            ls.PushBack(rapidjson::kObjectType, doc.GetAllocator());
            rapidsjon_helper_load_from(ls[ls.Size() - 1], doc, data.Get(i, nullptr), options);
          }
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidjson::Value obj;
        obj.SetObject();
        if (obj.IsObject()) {
          rapidsjon_helper_load_from(obj, doc, src.GetReflection()->GetMessage(src, fds), options);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(obj), doc);
      }

      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedDouble(src, fds, i), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidsjon_helper_mutable_set_member(parent, key_name, src.GetReflection()->GetDouble(src, fds), doc);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedFloat(src, fds, i), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidsjon_helper_mutable_set_member(parent, key_name, src.GetReflection()->GetFloat(src, fds), doc);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedBool(src, fds, i), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidsjon_helper_mutable_set_member(parent, key_name, src.GetReflection()->GetBool(src, fds), doc);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
      if (fds->is_repeated()) {
        int len = src.GetReflection()->FieldSize(src, fds);
        rapidjson::Value ls;
        ls.SetArray();
        for (int i = 0; i < len; ++i) {
          rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedEnumValue(src, fds, i), doc);
        }
        rapidsjon_helper_mutable_set_member(parent, key_name, std::move(ls), doc);
      } else {
        rapidsjon_helper_mutable_set_member(parent, key_name, src.GetReflection()->GetEnumValue(src, fds), doc);
      }
      break;
    };
    default: {
      WLOGERROR("%s in ConstSettings with type=%s is not supported now", key_name, fds->type_name());
      break;
    }
  }
}

static std::string dump_pick_field_string_filter(const rapidjson::Value& val,
                                                 const rapidsjon_helper_dump_options& options) {
  if (!val.IsString()) {
    return std::string();
  }

  switch (options.string_mode) {
    case rapidsjon_helper_string_mode::URI:
      return util::uri::decode_uri(val.GetString(), val.GetStringLength());
    case rapidsjon_helper_string_mode::URI_COMPONENT:
      return util::uri::decode_uri_component(val.GetString(), val.GetStringLength());
    default:
      return std::string(val.GetString(), val.GetStringLength());
  }
}

static void dump_pick_field(const rapidjson::Value& val, ::google::protobuf::Message& dst,
                            const ::google::protobuf::FieldDescriptor* fds,
                            const rapidsjon_helper_dump_options& options) {
  if (nullptr == fds) {
    return;
  }

  switch (fds->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
      int32_t jval = 0;
      if (val.IsInt()) {
        jval = val.GetInt();
      } else if (val.IsString() && options.convert_number_from_string) {
        util::string::str2int(jval, val.GetString());
      }
      if (fds->is_repeated()) {
        dst.GetReflection()->AddInt32(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetInt32(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
      int64_t jval = 0;
      if (val.IsInt64()) {
        jval = val.GetInt64();
      } else if (val.IsString() && options.convert_number_from_string) {
        util::string::str2int(jval, val.GetString());
      }
      if (fds->is_repeated()) {
        dst.GetReflection()->AddInt64(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetInt64(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      uint32_t jval = 0;
      if (val.IsUint()) {
        jval = val.GetUint();
      } else if (val.IsString() && options.convert_number_from_string) {
        util::string::str2int(jval, val.GetString());
      }
      if (fds->is_repeated()) {
        dst.GetReflection()->AddUInt32(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetUInt32(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
      uint64_t jval = 0;
      if (val.IsUint64()) {
        jval = val.GetUint64();
      } else if (val.IsString() && options.convert_number_from_string) {
        util::string::str2int(jval, val.GetString());
      }
      if (fds->is_repeated()) {
        dst.GetReflection()->AddUInt64(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetUInt64(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
      // if type is bytes, skip display
      if (fds->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
        break;
      }

      if (fds->is_repeated()) {
        dst.GetReflection()->AddString(&dst, fds, dump_pick_field_string_filter(val, options));
      } else {
        dst.GetReflection()->SetString(&dst, fds, dump_pick_field_string_filter(val, options));
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
      if (!val.IsObject()) {
        // type error
        break;
      }

      rapidjson::Value& jval = const_cast<rapidjson::Value&>(val);
      if (fds->is_repeated()) {
        ::google::protobuf::Message* submsg = dst.GetReflection()->AddMessage(&dst, fds);
        if (nullptr != submsg) {
          rapidsjon_helper_dump_to(jval, *submsg, options);
        }
      } else {
        ::google::protobuf::Message* submsg = dst.GetReflection()->MutableMessage(&dst, fds);
        if (nullptr != submsg) {
          rapidsjon_helper_dump_to(jval, *submsg, options);
        }
      }

      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
      double jval = val.IsDouble() ? val.GetDouble() : 0;
      if (fds->is_repeated()) {
        dst.GetReflection()->AddDouble(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetDouble(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
      float jval = val.IsFloat() ? val.GetFloat() : 0;
      if (fds->is_repeated()) {
        dst.GetReflection()->AddFloat(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetFloat(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
      bool jval = val.IsBool() ? val.GetBool() : false;
      if (fds->is_repeated()) {
        dst.GetReflection()->AddBool(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetBool(&dst, fds, jval);
      }
      break;
    };
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
      int jval_num = val.IsInt() ? val.GetInt() : 0;
      const google::protobuf::EnumValueDescriptor* jval = fds->enum_type()->FindValueByNumber(jval_num);
      if (jval == nullptr) {
        // invalid value
        break;
      }
      // fds->enum_type
      if (fds->is_repeated()) {
        dst.GetReflection()->AddEnum(&dst, fds, jval);
      } else {
        dst.GetReflection()->SetEnum(&dst, fds, jval);
      }
      break;
    };
    default: {
      WLOGERROR("%s in %s with type=%s is not supported now", fds->name().c_str(),
                dst.GetDescriptor()->full_name().c_str(), fds->type_name());
      break;
    }
  }
}

static void dump_field_item(const rapidjson::Value& src, ::google::protobuf::Message& dst,
                            const ::google::protobuf::FieldDescriptor* fds,
                            const rapidsjon_helper_dump_options& options) {
  if (nullptr == fds) {
    return;
  }

  if (!src.IsObject()) {
    return;
  }

  const char* key_name = fds->name().c_str();
  if (fds->options().HasExtension(atframework::field_json_options)) {
    const atframework::JsonOptions& field_json_options = fds->options().GetExtension(atframework::field_json_options);
    if (!field_json_options.alias_key_name().empty()) {
      key_name = field_json_options.alias_key_name().c_str();
    }
  }
  rapidjson::Value::ConstMemberIterator iter = src.FindMember(key_name);
  if (iter == src.MemberEnd()) {
    // field not found, just skip
    return;
  }

  const rapidjson::Value& val = iter->value;
  if (val.IsArray() && !fds->is_repeated()) {
    // Type error
    return;
  }

  if (fds->is_map() && nullptr != fds->message_type() && val.IsObject()) {
    const ::google::protobuf::FieldDescriptor* map_key_fds = fds->message_type()->map_key();
    const ::google::protobuf::FieldDescriptor* map_value_fds = fds->message_type()->map_value();
    if (nullptr == map_key_fds || nullptr == map_value_fds) {
      return;
    }

    for (rapidjson::Value::ConstMemberIterator map_iter = val.MemberBegin(); map_iter != val.MemberEnd(); ++map_iter) {
      auto submsg = dst.GetReflection()->AddMessage(&dst, fds);
      if (nullptr == submsg) {
        break;
      }

      dump_pick_field(map_iter->name, *submsg, map_key_fds, options);
      dump_pick_field(map_iter->value, *submsg, map_value_fds, options);
    }

  } else if (fds->is_repeated()) {
    if (!val.IsArray()) {
      // Type error
      return;
    }

    size_t arrsz = val.Size();
    for (size_t i = 0; i < arrsz; ++i) {
      dump_pick_field(val[static_cast<rapidjson::SizeType>(i)], dst, fds, options);
    }
  } else {
    if (val.IsArray()) {
      return;
    }

    dump_pick_field(val, dst, fds, options);
  }
}
}  // namespace detail

std::string rapidsjon_helper_stringify(const rapidjson::Document& doc, size_t more_reserve_size) {
  // Stringify the DOM
  rapidjson::StringBuffer buffer{nullptr, 64 * 1024};
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  std::string ret;
  ret.reserve(buffer.GetSize() + more_reserve_size + 1);
  ret.assign(buffer.GetString(), buffer.GetSize());

  return ret;
}

bool rapidsjon_helper_unstringify(rapidjson::Document& doc, const std::string& json) {
  try {
    doc.Parse(json.c_str(), json.size());
  } catch (...) {
    return false;
  }

  if (!doc.IsObject() && !doc.IsArray()) {
    return false;
  }

  return true;
}

gsl::string_view rapidsjon_helper_get_type_name(rapidjson::Type t) {
  switch (t) {
    case rapidjson::kNullType:
      return "null";
    case rapidjson::kFalseType:
      return "boolean(false)";
    case rapidjson::kTrueType:
      return "boolean(true)";
    case rapidjson::kObjectType:
      return "object";
    case rapidjson::kArrayType:
      return "array";
    case rapidjson::kStringType:
      return "string";
    case rapidjson::kNumberType:
      return "number";
    default:
      return "UNKNOWN";
  }
}

std::string rapidsjon_helper_stringify(const ::google::protobuf::Message& src,
                                       const rapidsjon_helper_load_options& options) {
  rapidjson::Document doc;
  rapidsjon_helper_load_from(doc, src, options);
  return rapidsjon_helper_stringify(doc);
}

bool rapidsjon_helper_parse(::google::protobuf::Message& dst, const std::string& src,
                            const rapidsjon_helper_dump_options& options) {
  rapidjson::Document doc;
  if (!rapidsjon_helper_unstringify(doc, src)) {
    return false;
  }

  rapidsjon_helper_dump_to(doc, dst, options);
  return true;
}

void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, rapidjson::Value&& val,
                                         rapidjson::Document& doc) {
  if (!parent.IsObject()) {
    parent.SetObject();
  }

  rapidjson::Value::MemberIterator iter = parent.FindMember(rapidjson::StringRef(key.data(), key.size()));
  if (iter != parent.MemberEnd()) {
    iter->value.Swap(val);
  } else {
    rapidjson::Value k;
    rapidjson::Value v;
    k.SetString(key.data(), static_cast<rapidjson::SizeType>(key.size()), doc.GetAllocator());
    v.Swap(val);
    parent.AddMember(k, v, doc.GetAllocator());
  }
}

void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, const rapidjson::Value& val,
                                         rapidjson::Document& doc) {
  if (!parent.IsObject()) {
    parent.SetObject();
  }

  rapidjson::Value::MemberIterator iter = parent.FindMember(rapidjson::StringRef(key.data(), key.size()));
  if (iter != parent.MemberEnd()) {
    iter->value.CopyFrom(val, doc.GetAllocator());
  } else {
    rapidjson::Value k;
    rapidjson::Value v;
    k.SetString(key.data(), static_cast<rapidjson::SizeType>(key.size()), doc.GetAllocator());
    v.CopyFrom(val, doc.GetAllocator());
    parent.AddMember(k, v, doc.GetAllocator());
  }
}

void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, gsl::string_view val,
                                         rapidjson::Document& doc) {
  rapidjson::Value v;
  v.SetString(val.data(), static_cast<rapidjson::SizeType>(val.size()), doc.GetAllocator());
  rapidsjon_helper_mutable_set_member(parent, key, std::move(v), doc);
}

void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, const std::string& val, rapidjson::Document& doc) {
  rapidjson::Value v;
  v.SetString(val.c_str(), static_cast<rapidjson::SizeType>(val.size()), doc.GetAllocator());
  rapidsjon_helper_append_to_list(list_parent, std::move(v), doc);
}

void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, std::string& val, rapidjson::Document& doc) {
  rapidjson::Value v;
  v.SetString(val.c_str(), static_cast<rapidjson::SizeType>(val.size()), doc.GetAllocator());
  rapidsjon_helper_append_to_list(list_parent, std::move(v), doc);
}

void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, gsl::string_view val, rapidjson::Document& doc) {
  rapidjson::Value v;
  v.SetString(val.data(), static_cast<rapidjson::SizeType>(val.size()), doc.GetAllocator());
  rapidsjon_helper_append_to_list(list_parent, std::move(v), doc);
}

void rapidsjon_helper_dump_to(const rapidjson::Document& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options) {
  if (src.IsObject()) {
    rapidjson::Value& srcobj = const_cast<rapidjson::Document&>(src);
    rapidsjon_helper_dump_to(srcobj, dst, options);
  }
}

void rapidsjon_helper_load_from(rapidjson::Document& dst, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options) {
  if (!dst.IsObject()) {
    dst.SetObject();
  }
  rapidjson::Value& root = dst;
  rapidsjon_helper_load_from(root, dst, src, options);
}

void rapidsjon_helper_dump_to(const rapidjson::Value& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options) {
  const ::google::protobuf::Descriptor* desc = dst.GetDescriptor();
  if (nullptr == desc) {
    return;
  }

  for (int i = 0; i < desc->field_count(); ++i) {
    detail::dump_field_item(src, dst, desc->field(i), options);
  }
}

void rapidsjon_helper_load_from(rapidjson::Value& dst, rapidjson::Document& doc, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options) {
  if (options.reserve_empty) {
    const ::google::protobuf::Descriptor* desc = src.GetDescriptor();
    if (nullptr == desc) {
      return;
    }

    for (int i = 0; i < desc->field_count(); ++i) {
      detail::load_field_item(dst, src, desc->field(i), doc, options);
    }
  } else {
    std::vector<const ::google::protobuf::FieldDescriptor*> fields_with_data;
    src.GetReflection()->ListFields(src, &fields_with_data);
    for (size_t i = 0; i < fields_with_data.size(); ++i) {
      detail::load_field_item(dst, src, fields_with_data[i], doc, options);
    }
  }
}
