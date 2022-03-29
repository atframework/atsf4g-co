//
// Created by owt50 on 2019-07-29.
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
  bool reserve_empty;
  rapidsjon_helper_string_mode::type string_mode;

  inline rapidsjon_helper_load_options() : reserve_empty(false), string_mode(rapidsjon_helper_string_mode::RAW) {}
};

struct rapidsjon_helper_dump_options {
  rapidsjon_helper_string_mode::type string_mode;
  inline rapidsjon_helper_dump_options() : string_mode(rapidsjon_helper_string_mode::RAW) {}
};

std::string rapidsjon_helper_stringify(const rapidjson::Document& doc, size_t more_reserve_size = 0);
bool rapidsjon_helper_unstringify(rapidjson::Document& doc, const std::string& json);
const char* rapidsjon_helper_get_type_name(rapidjson::Type t);

std::string rapidsjon_helper_stringify(const ::google::protobuf::Message& src,
                                       const rapidsjon_helper_load_options& options);
bool rapidsjon_helper_parse(::google::protobuf::Message& dst, const std::string& src,
                            const rapidsjon_helper_dump_options& options);

template <typename TVAL>
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, TVAL&& val,
                                         rapidjson::Document& doc);

void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, rapidjson::Value&& val,
                                         rapidjson::Document& doc);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, const rapidjson::Value& val,
                                         rapidjson::Document& doc);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, const std::string& val,
                                         rapidjson::Document& doc);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, std::string& val,
                                         rapidjson::Document& doc);
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, const char* val,
                                         rapidjson::Document& doc);

template <typename TVAL>
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, TVAL&& val, rapidjson::Document& doc);

void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, const std::string& val, rapidjson::Document& doc);
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, std::string& val, rapidjson::Document& doc);
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, const char* val, rapidjson::Document& doc);

void rapidsjon_helper_dump_to(const rapidjson::Document& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options);

void rapidsjon_helper_load_from(rapidjson::Document& dst, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options);

void rapidsjon_helper_dump_to(const rapidjson::Value& src, ::google::protobuf::Message& dst,
                              const rapidsjon_helper_dump_options& options);

void rapidsjon_helper_load_from(rapidjson::Value& dst, rapidjson::Document& doc, const ::google::protobuf::Message& src,
                                const rapidsjon_helper_load_options& options);

// ============ template implement ============

template <typename TVAL>
void rapidsjon_helper_mutable_set_member(rapidjson::Value& parent, const char* key, TVAL&& val,
                                         rapidjson::Document& doc) {
  if (!parent.IsObject()) {
    parent.SetObject();
  }

  rapidjson::Value::MemberIterator iter = parent.FindMember(key);
  if (iter != parent.MemberEnd()) {
    iter->value.Set(std::forward<TVAL>(val), doc.GetAllocator());
  } else {
    rapidjson::Value k;
    k.SetString(key, doc.GetAllocator());
    parent.AddMember(k, std::forward<TVAL>(val), doc.GetAllocator());
  }
}

template <typename TVAL>
void rapidsjon_helper_append_to_list(rapidjson::Value& list_parent, TVAL&& val, rapidjson::Document& doc) {
  if (list_parent.IsArray()) {
    list_parent.PushBack(std::forward<TVAL>(val), doc.GetAllocator());
  } else {
    WLOGERROR("parent should be a array, but we got %s.", rapidsjon_helper_get_type_name(list_parent.GetType()));
  }
}

#endif
