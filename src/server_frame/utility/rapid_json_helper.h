// Copyright 2022
// Created by owent on 2019/07/29.
//

#ifndef UTILITY_RAPIDJSON_HELPER_H
#define UTILITY_RAPIDJSON_HELPER_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include <rapidjson/document.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

namespace google {
namespace protobuf {
class Message;
}
}  // namespace google

struct rapidsjon_helper_string_mode {
  enum type {
    RAW = 0,
    URI,
    URI_COMPONENT,
  };
};

struct rapidsjon_helper_load_options {
  bool reserve_empty = false;
  bool convert_large_number_to_string = false;  // it's friendly to JSON.parse(...) in javascript
  rapidsjon_helper_string_mode::type string_mode = rapidsjon_helper_string_mode::RAW;

  inline rapidsjon_helper_load_options()
      : reserve_empty(false), convert_large_number_to_string(false), string_mode(rapidsjon_helper_string_mode::RAW) {}
};

struct rapidsjon_helper_dump_options {
  rapidsjon_helper_string_mode::type string_mode = rapidsjon_helper_string_mode::RAW;
  bool convert_number_from_string = false;  // it's friendly to JSON.parse(...) in javascript

  inline rapidsjon_helper_dump_options()
      : string_mode(rapidsjon_helper_string_mode::RAW), convert_number_from_string(false) {}
};

std::string rapidsjon_helper_stringify(const rapidjson::Document& doc, size_t more_reserve_size = 0);
bool rapidsjon_helper_unstringify(rapidjson::Document& doc, const std::string& json);
gsl::string_view rapidsjon_helper_get_type_name(rapidjson::Type t);

std::string rapidsjon_helper_stringify(const ::google::protobuf::Message& src,
                                       const rapidsjon_helper_load_options& options);
bool rapidsjon_helper_parse(::google::protobuf::Message& dst, const std::string& src,
                            const rapidsjon_helper_dump_options& options);

void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, rapidjson::Value&& val,
                                         rapidjson::Document& doc, bool overwrite = true);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, const rapidjson::Value& val,
                                         rapidjson::Document& doc, bool overwrite = true);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, gsl::string_view val,
                                         rapidjson::Document& doc, bool overwrite = true);

template <class TVAL>
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, TVAL&& val, rapidjson::Document& doc);

void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, const std::string& val, rapidjson::Document& doc);
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, std::string& val, rapidjson::Document& doc);
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, gsl::string_view val, rapidjson::Document& doc);

void rapidsjon_helper_dump_to(const rapidjson::Document& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options);

void rapidsjon_helper_load_from(rapidjson::Document& dst, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options);

void rapidsjon_helper_dump_to(const rapidjson::Value& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options);

void rapidsjon_helper_load_from(rapidjson::Value& dst, rapidjson::Document& doc, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options);

// ============ template implement ============

template <class TVAL, class = typename std::enable_if<
                          !std::is_convertible<typename std::decay<TVAL>::type, gsl::string_view>::value>::type>
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, gsl::string_view key, TVAL&& val,
                                         rapidjson::Document& doc, bool overwrite = true) {
  if (!parent.IsObject()) {
    parent.SetObject();
  }

  rapidjson::Value::MemberIterator iter = parent.FindMember(key.data());
  if (iter != parent.MemberEnd()) {
    if (overwrite) {
      iter->value.Set(std::forward<TVAL>(val), doc.GetAllocator());
    }
  } else {
    rapidjson::Value k;
    k.SetString(key.data(), static_cast<rapidjson::SizeType>(key.size()), doc.GetAllocator());
    parent.AddMember(k, std::forward<TVAL>(val), doc.GetAllocator());
  }
}

template <class TVAL>
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, TVAL&& val, rapidjson::Document& doc) {
  if (list_parent.IsArray()) {
    list_parent.PushBack(std::forward<TVAL>(val), doc.GetAllocator());
  } else {
    FWLOGERROR("parent should be a array, but we got {}.", rapidsjon_helper_get_type_name(list_parent.GetType()));
  }
}

#endif
