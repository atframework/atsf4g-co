#include <cstring>
#include <sstream>

#include <algorithm/base64.h>
#include <common/string_oprs.h>

#include "etcd_packer.h"

#include <config/compiler/migrate_prefix.h>

namespace atframe {
    namespace component {
        bool etcd_packer::parse_object(rapidjson::Document &doc, const char *data) {
            try {
                doc.Parse(data);
                return doc.IsObject();
            } catch (...) {
                return false;
            }
        }

        void etcd_packer::pack(const etcd_key_value &etcd_val, rapidjson::Value &json_val, rapidjson::Document &doc) {
            if (0 != etcd_val.create_revision) {
                json_val.AddMember("create_revision", etcd_val.create_revision, doc.GetAllocator());
            }

            if (0 != etcd_val.mod_revision) {
                json_val.AddMember("mod_revision", etcd_val.mod_revision, doc.GetAllocator());
            }

            if (0 != etcd_val.version) {
                json_val.AddMember("version", etcd_val.version, doc.GetAllocator());
            }

            if (0 != etcd_val.lease) {
                json_val.AddMember("lease", etcd_val.lease, doc.GetAllocator());
            }

            if (!etcd_val.key.empty()) {
                pack_base64(json_val, "key", etcd_val.key, doc);
            }

            if (!etcd_val.value.empty()) {
                pack_base64(json_val, "value", etcd_val.value, doc);
            }
        }

        void etcd_packer::unpack(etcd_key_value &etcd_val, rapidjson::Value &json_val) {
            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("create_revision");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.create_revision = 0;
                } else {
                    if (iter->value.IsInt64()) {
                        etcd_val.create_revision = iter->value.GetInt64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.create_revision, val);
                    } else {
                        etcd_val.create_revision = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("mod_revision");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.mod_revision = 0;
                } else {
                    if (iter->value.IsInt64()) {
                        etcd_val.mod_revision = iter->value.GetInt64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.mod_revision, val);
                    } else {
                        etcd_val.mod_revision = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("version");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.version = 0;
                } else {
                    if (iter->value.IsInt64()) {
                        etcd_val.version = iter->value.GetInt64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.version, val);
                    } else {
                        etcd_val.version = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("lease");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.lease = 0;
                } else {
                    if (iter->value.IsInt64()) {
                        etcd_val.lease = iter->value.GetInt64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.lease, val);
                    } else {
                        etcd_val.lease = 0;
                    }
                }
            }

            unpack_base64(json_val, "key", etcd_val.key);
            unpack_base64(json_val, "value", etcd_val.value);
        }

        void etcd_packer::pack(const etcd_response_header &etcd_val, rapidjson::Value &json_val, rapidjson::Document &doc) {
            if (0 != etcd_val.cluster_id) {
                json_val.AddMember("cluster_id", etcd_val.cluster_id, doc.GetAllocator());
            }

            if (0 != etcd_val.member_id) {
                json_val.AddMember("member_id", etcd_val.member_id, doc.GetAllocator());
            }

            if (0 != etcd_val.revision) {
                json_val.AddMember("revision", etcd_val.revision, doc.GetAllocator());
            }

            if (0 != etcd_val.raft_term) {
                json_val.AddMember("raft_term", etcd_val.raft_term, doc.GetAllocator());
            }
        }

        void etcd_packer::unpack(etcd_response_header &etcd_val, rapidjson::Value &json_val) {
            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("cluster_id");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.cluster_id = 0;
                } else {
                    if (iter->value.IsUint64()) {
                        etcd_val.cluster_id = iter->value.GetUint64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.cluster_id, val);
                    } else {
                        etcd_val.cluster_id = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("member_id");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.member_id = 0;
                } else {
                    if (iter->value.IsUint64()) {
                        etcd_val.member_id = iter->value.GetUint64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.member_id, val);
                    } else {
                        etcd_val.member_id = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("revision");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.revision = 0;
                } else {
                    if (iter->value.IsInt64()) {
                        etcd_val.revision = iter->value.GetInt64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.revision, val);
                    } else {
                        etcd_val.revision = 0;
                    }
                }
            }

            {
                rapidjson::Value::MemberIterator iter = json_val.FindMember("raft_term");
                if (iter == json_val.MemberEnd()) {
                    etcd_val.raft_term = 0;
                } else {
                    if (iter->value.IsUint64()) {
                        etcd_val.raft_term = iter->value.GetUint64();
                    } else if (iter->value.IsString()) {
                        const char *val = iter->value.GetString();
                        ::util::string::str2int(etcd_val.raft_term, val);
                    } else {
                        etcd_val.raft_term = 0;
                    }
                }
            }
        }

        void etcd_packer::pack_key_range(rapidjson::Value &json_val, const std::string &key, std::string range_end, rapidjson::Document &doc) {
            if ("+1" == range_end) {
                range_end      = key;
                bool need_plus = true;
                while (!range_end.empty() && need_plus) {
                    char c = range_end[range_end.size() - 1];
                    if (static_cast<unsigned char>(c) == 0xff) {
                        range_end.pop_back();
                    } else {
                        range_end[range_end.size() - 1] = c + 1;
                        need_plus                       = false;
                    }
                }

                if (range_end.empty() && need_plus && !key.empty()) {
                    range_end = "\0";
                }
            }

            if (!key.empty()) {
                pack_base64(json_val, "key", key, doc);
            }

            if (!range_end.empty()) {
                pack_base64(json_val, "range_end", range_end, doc);
            }
        }

        void etcd_packer::pack_string(rapidjson::Value &json_val, const char *key, const char *val, rapidjson::Document &doc) {
            rapidjson::Value k;
            rapidjson::Value v;
            k.SetString(key, doc.GetAllocator());
            v.SetString(val, doc.GetAllocator());
            json_val.AddMember(k, v, doc.GetAllocator());
        }

        bool etcd_packer::unpack_string(rapidjson::Value &json_val, const char *key, std::string &val) {
            rapidjson::Value::MemberIterator iter = json_val.FindMember(key);
            if (iter == json_val.MemberEnd()) {
                return false;
            }

            val = unpack_to_string(iter->value);
            return true;
        }

        std::string etcd_packer::unpack_to_string(const rapidjson::Value &json_val) {
            switch (json_val.GetType()) {
            case rapidjson::kNullType: {
                return "null";
            }
            case rapidjson::kFalseType: {
                return "false";
            }
            case rapidjson::kTrueType: {
                return "true";
            }
            case rapidjson::kObjectType: {
                return "[object object]";
            }
            case rapidjson::kArrayType: {
                return "[object array]";
            }
            case rapidjson::kStringType: {
                return json_val.GetString();
            }
            case rapidjson::kNumberType: {
                std::stringstream ss;
                if (json_val.IsDouble()) {
                    ss << json_val.GetDouble();
                } else if (json_val.IsInt()) {
                    ss << json_val.GetInt();
                } else if (json_val.IsUint()) {
                    ss << json_val.GetUint();
                } else if (json_val.IsInt64()) {
                    ss << json_val.GetInt64();
                } else {
                    ss << json_val.GetUint64();
                }
                return ss.str();
            }
            default:
                break;
            }

            return "";
        }

        void etcd_packer::pack_base64(rapidjson::Value &json_val, const char *key, const std::string &val, rapidjson::Document &doc) {
            std::string base64_val;
            util::base64_encode(base64_val, val);

            rapidjson::Value k;
            rapidjson::Value v;
            k.SetString(key, doc.GetAllocator());
            v.SetString(base64_val.c_str(), static_cast<rapidjson::SizeType>(base64_val.size()), doc.GetAllocator());
            json_val.AddMember(k, v, doc.GetAllocator());
        }

        bool etcd_packer::unpack_base64(rapidjson::Value &json_val, const char *key, std::string &val) {
            rapidjson::Value::MemberIterator iter = json_val.FindMember(key);
            if (iter == json_val.MemberEnd()) {
                return false;
            }

            if (!iter->value.IsString()) {
                return false;
            }

            const char *base64_val    = iter->value.GetString();
            size_t      base64_val_sz = strlen(base64_val);

            return 0 == util::base64_decode(val, reinterpret_cast<const unsigned char *>(base64_val), base64_val_sz);
        }

        void etcd_packer::unpack_int(rapidjson::Value &json_val, const char *key, int64_t &out) {
            rapidjson::Value::MemberIterator iter = json_val.FindMember(key);
            if (iter == json_val.MemberEnd()) {
                out = 0;
            } else {
                if (iter->value.IsInt64()) {
                    out = iter->value.GetInt64();
                } else if (iter->value.IsString()) {
                    const char *val = iter->value.GetString();
                    ::util::string::str2int(out, val);
                }
            }
        }

        void etcd_packer::unpack_int(rapidjson::Value &json_val, const char *key, uint64_t &out) {
            rapidjson::Value::MemberIterator iter = json_val.FindMember(key);
            if (iter == json_val.MemberEnd()) {
                out = 0;
            } else {
                if (iter->value.IsUint64()) {
                    out = iter->value.GetUint64();
                } else if (iter->value.IsString()) {
                    const char *val = iter->value.GetString();
                    ::util::string::str2int(out, val);
                }
            }
        }

        void etcd_packer::unpack_bool(rapidjson::Value &json_val, const char *key, bool &out) {
            rapidjson::Value::MemberIterator iter = json_val.FindMember(key);
            if (iter == json_val.MemberEnd()) {
                out = false;
            } else {
                if (iter->value.IsBool()) {
                    out = iter->value.GetBool();
                } else if (iter->value.IsUint64()) {
                    out = 0 != iter->value.GetUint64();
                } else if (iter->value.IsInt64()) {
                    out = 0 != iter->value.GetInt64();
                } else if (iter->value.IsString()) {
                    const char *val    = iter->value.GetString();
                    int         outint = 1;
                    ::util::string::str2int(outint, val);
                    out = 0 != outint;
                }
            }
        }
    } // namespace component
} // namespace atframe

#include <config/compiler/migrate_suffix.h>
