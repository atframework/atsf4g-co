#include <assert.h>

#include <vector>
#include <algorithm>

#include <common/string_oprs.h>

#include <config/ini_loader.h>

#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <config/excel_config_const_index.h>

#include "ini_loader_helper.h"

namespace detail {
    /*
    static void load_field_item(util::config::ini_value& parent, const ::google::protobuf::Message& src, const ::google::protobuf::FieldDescriptor* fds) {
        if (NULL == fds) {
            return;
        }

        auto child_iter = parent.get_children().find(fds->name());
        // skip if not found
        if (child_iter == parent.get_children().end()) {
            return;
        }

        switch(fds->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
                if (fds->is_repeated()) {
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedInt32(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetInt32(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedInt64(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetInt64(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedUInt32(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetUInt32(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedUInt64(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetUInt64(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
                std::string empty;
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidjson::Value v;
                        load_field_string_filter(src.GetReflection()->GetRepeatedStringReference(src, fds, i, &empty), v, doc, options);
                        rapidsjon_helper_append_to_list(ls, std::move(v), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidjson::Value v;
                    load_field_string_filter(src.GetReflection()->GetStringReference(src, fds, &empty), v, doc, options);
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(v), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                if (fds->is_repeated()) {
                    rapidjson::Value ls;
                    ls.SetArray();
                    const ::google::protobuf::RepeatedPtrField<::google::protobuf::Message>& data = src.GetReflection()->GetRepeatedPtrField<::google::protobuf::Message>(src, fds);
                    if (ls.IsArray()) {
                        for (int i = 0; i < data.size(); ++ i) {
                            ls.PushBack(rapidjson::kObjectType, doc.GetAllocator());
                            rapidsjon_helper_load_from(ls[ls.Size() - 1], doc, data.Get(i), options);
                        }                    
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidjson::Value obj;
                    obj.SetObject();
                    if (obj.IsObject()) {
                        rapidsjon_helper_load_from(obj, doc, src.GetReflection()->GetMessage(src, fds), options);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(obj), doc);
                }
                
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedDouble(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetDouble(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedFloat(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetFloat(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedBool(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetBool(src, fds), doc);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                if (fds->is_repeated()) {
                    int len = src.GetReflection()->FieldSize(src, fds);
                    rapidjson::Value ls;
                    ls.SetArray();
                    for (int i = 0; i < len; ++ i) {
                        rapidsjon_helper_append_to_list(ls, src.GetReflection()->GetRepeatedEnumValue(src, fds, i), doc);
                    }
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), std::move(ls), doc);
                } else {
                    rapidsjon_helper_mutable_set_member(parent, fds->name().c_str(), src.GetReflection()->GetEnumValue(src, fds), doc);
                }
                break;
            };
            default: {
                WLOGERROR("%s in ConstSettings with type=%s is not supported now", fds->name().c_str(), fds->type_name());
                break;
            }
        }
    }
    */

    static void dump_pick_field(const util::config::ini_value& val, ::google::protobuf::Message& dst, const ::google::protobuf::FieldDescriptor* fds, size_t index) {
        if (NULL == fds) {
            return;
        }

        switch(fds->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddInt32(&dst, fds, val.as_int32(index));
                } else {
                    dst.GetReflection()->SetInt32(&dst, fds, val.as_int32(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddInt64(&dst, fds, val.as_int64(index));
                } else {
                    dst.GetReflection()->SetInt64(&dst, fds, val.as_int64(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddUInt32(&dst, fds, val.as_uint32(index));
                } else {
                    dst.GetReflection()->SetUInt32(&dst, fds, val.as_uint32(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddUInt64(&dst, fds, val.as_uint64(index));
                } else {
                    dst.GetReflection()->SetUInt64(&dst, fds, val.as_uint64(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddString(&dst, fds, val.as_cpp_string(index));
                } else {
                    dst.GetReflection()->SetString(&dst, fds, val.as_cpp_string(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                // ç‰¹æ®Šmessage
                if (fds->message_type()->full_name() == ::google::protobuf::Duration::descriptor()->full_name()) {
                    const std::string& value = val.as_cpp_string(index);
                    if (fds->is_repeated()) {
                        excel::parse_duration(value, *static_cast<::google::protobuf::Duration*>(dst.GetReflection()->AddMessage(&dst, fds)));
                    } else {
                        excel::parse_duration(value, *static_cast<::google::protobuf::Duration*>(dst.GetReflection()->MutableMessage(&dst, fds)));
                    }
                    break;
                } else if (fds->message_type()->full_name() == ::google::protobuf::Timestamp::descriptor()->full_name()) {
                    const std::string& value = val.as_cpp_string(index);
                    if (fds->is_repeated()) {
                        excel::parse_timepoint(value, *static_cast<::google::protobuf::Timestamp*>(dst.GetReflection()->AddMessage(&dst, fds)));
                    } else {
                        excel::parse_timepoint(value, *static_cast<::google::protobuf::Timestamp*>(dst.GetReflection()->MutableMessage(&dst, fds)));
                    }
                    break;
                }

                if (fds->is_repeated()) {
                    // repeated message is not supported by ini_loader
                    break;
                } else {
                    ::google::protobuf::Message* submsg = dst.GetReflection()->MutableMessage(&dst, fds);
                    if (NULL != submsg) {
                        ini_loader_helper_dump_to(val, *submsg);
                    }
                }
                
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddDouble(&dst, fds, val.as_double(index));
                } else {
                    dst.GetReflection()->SetDouble(&dst, fds, val.as_double(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT: {
                if (fds->is_repeated()) {
                    dst.GetReflection()->AddFloat(&dst, fds, val.as_float(index));
                } else {
                    dst.GetReflection()->SetFloat(&dst, fds, val.as_float(index));
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
                bool jval = true;
                std::string trans = val.as_cpp_string(index);
                std::transform(trans.begin(), trans.end(), trans.begin(), util::string::tolower<char>);

                if ("0" == trans || "false" == trans || "no" == trans || "disable" == trans || "disabled" == trans || "" == trans) {
                    jval = false;
                }

                if (fds->is_repeated()) {
                    dst.GetReflection()->AddBool(&dst, fds, jval);
                } else {
                    dst.GetReflection()->SetBool(&dst, fds, jval);
                }
                break;
            };
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                const std::string& name = val.as_cpp_string(index);
                const google::protobuf::EnumValueDescriptor* jval = NULL;
                if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
                    jval = fds->enum_type()->FindValueByNumber(val.as_int32(index));
                } else {
                    jval = fds->enum_type()->FindValueByName(name);
                }
                
                if (jval == NULL) {
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
                WLOGERROR("%s in %s with type=%s is not supported now", fds->name().c_str(), dst.GetDescriptor()->full_name().c_str(), fds->type_name());
                break;
            }
        }
    }

    static void dump_field_item(const util::config::ini_value& src, ::google::protobuf::Message& dst, const ::google::protobuf::FieldDescriptor* fds) {
        if (NULL == fds) {
            return;
        }

        auto child_iter = src.get_children().find(fds->name());
        // skip if not found, just skip
        if (child_iter == src.get_children().end()) {
            return;
        }

        if (fds->is_repeated()) {
            size_t arrsz = src.size();
            for (size_t i = 0; i < arrsz; ++ i) {
                dump_pick_field(child_iter->second, dst, fds, i);
            }     
        } else {

            dump_pick_field(child_iter->second, dst, fds, 0);
        }
    }
}

void ini_loader_helper_dump_to(const util::config::ini_value& src, ::google::protobuf::Message& dst) {
    const ::google::protobuf::Descriptor* desc = dst.GetDescriptor();
    if (NULL == desc) {
        return;
    }

    for (int i = 0 ;i < desc->field_count(); ++ i) {
        detail::dump_field_item(src, dst, desc->field(i));
    }
}

/*
void ini_loader_helper_load_from(util::config::ini_value& dst, const ::google::protobuf::Message& src) {
    if (options.reserve_empty) {
        const ::google::protobuf::Descriptor* desc = src.GetDescriptor();
        if (NULL == desc) {
            return;
        }

        for (int i = 0 ;i < desc->field_count(); ++ i) {
            detail::load_field_item(dst, src, desc->field(i), doc, options);
        }
    } else {
        std::vector<const ::google::protobuf::FieldDescriptor*> fields_with_data;
        src.GetReflection()->ListFields(src, &fields_with_data);
        for(size_t i = 0; i < fields_with_data.size(); ++ i) {
            detail::load_field_item(dst, src, fields_with_data[i], doc, options);
        }
    }
}
*/