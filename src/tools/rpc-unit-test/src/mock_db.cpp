// Copyright 2026 atframework

#include <atframework/testing/mock_db.h>

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <algorithm>
#include <utility>

namespace atsf4g {
namespace testing {

namespace {
bool is_integer_field(const google::protobuf::FieldDescriptor &fd) noexcept {
  switch (fd.cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      return true;
    default:
      return false;
  }
}

int64_t get_integer_field(const google::protobuf::Message &msg, const google::protobuf::FieldDescriptor &fd) {
  const google::protobuf::Reflection *reflect = msg.GetReflection();  switch (fd.cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      return static_cast<int64_t>(reflect->GetInt32(msg, &fd));
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      return reflect->GetInt64(msg, &fd);
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      return static_cast<int64_t>(reflect->GetUInt32(msg, &fd));
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      return static_cast<int64_t>(reflect->GetUInt64(msg, &fd));
    default:
      return 0;
  }
}

void set_integer_field(google::protobuf::Message &msg, const google::protobuf::FieldDescriptor &fd, int64_t value) {
  const google::protobuf::Reflection *reflect = msg.GetReflection();
  switch (fd.cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      reflect->SetInt32(&msg, &fd, static_cast<int32_t>(value));
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      reflect->SetInt64(&msg, &fd, value);
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      reflect->SetUInt32(&msg, &fd, static_cast<uint32_t>(value));
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      reflect->SetUInt64(&msg, &fd, static_cast<uint64_t>(value));
      break;
    default:
      break;
  }
}
}  // namespace

mock_db::mock_db() = default;
mock_db::~mock_db() { unbind(); }

bool mock_db::is_active() const noexcept { return bound_; }

void mock_db::register_message_factory(gsl::string_view full_name, message_factory_t factory) {
  factories_[std::string{full_name}] = std::move(factory);
}

void mock_db::set_now(clock::time_point now) {
  clock_overridden_ = true;
  now_override_ = now;
}

void mock_db::advance(clock::duration offset) { set_now(now() + offset); }

mock_db::clock::time_point mock_db::now() const {
  if (clock_overridden_) {
    return now_override_;
  }
  return clock::now();
}

bool mock_db::is_expired(bool has_expire, clock::time_point expire_at) const {
  return has_expire && expire_at <= now();
}

mock_db::kv_record *mock_db::find_live_kv(gsl::string_view key) {
  auto iter = kv_records_.find(std::string{key});
  if (iter == kv_records_.end()) {
    return nullptr;
  }
  if (is_expired(iter->second.has_expire, iter->second.expire_at)) {
    kv_records_.erase(iter);
    return nullptr;
  }
  return &iter->second;
}

mock_db::kl_record *mock_db::find_live_kl(gsl::string_view key) {
  auto iter = kl_records_.find(std::string{key});
  if (iter == kl_records_.end()) {
    return nullptr;
  }
  if (is_expired(iter->second.has_expire, iter->second.expire_at)) {
    kl_records_.erase(iter);
    return nullptr;
  }
  return &iter->second;
}

bool mock_db::has_key(gsl::string_view key) const {
  // const cast is safe: expiry evaluation is a read-only view of the logical state.
  mock_db &self = const_cast<mock_db &>(*this);
  return nullptr != self.find_live_kv(key) || nullptr != self.find_live_kl(key);
}

size_t mock_db::key_count() const {
  size_t ret = 0;
  for (const auto &record : kv_records_) {
    if (!is_expired(record.second.has_expire, record.second.expire_at)) {
      ++ret;
    }
  }
  for (const auto &record : kl_records_) {
    if (!is_expired(record.second.has_expire, record.second.expire_at)) {
      ++ret;
    }
  }
  return ret;
}

uint64_t mock_db::get_version(gsl::string_view key) const {
  auto iter = kv_records_.find(std::string{key});
  if (iter == kv_records_.end() || is_expired(iter->second.has_expire, iter->second.expire_at)) {
    return 0;
  }
  return iter->second.version;
}

void mock_db::erase_key(gsl::string_view key) {
  kv_records_.erase(std::string{key});
  kl_records_.erase(std::string{key});
}

void mock_db::clear() {
  kv_records_.clear();
  kl_records_.clear();
  calls_.clear();
  diagnostic_.clear();
}

const db_request_record *mock_db::call_at(size_t index) const {
  if (index >= calls_.size()) {
    return nullptr;
  }
  return &calls_[index];
}

size_t mock_db::calls(op_type op) const {
  size_t ret = 0;
  for (const auto &call : calls_) {
    if (call.op == op) {
      ++ret;
    }
  }
  return ret;
}

void mock_db::bind() {
  if (bound_) {
    return;
  }
  rpc::db::hash_table::set_hash_table_hook_for_unit_test(
      [this](const rpc::db::hash_table::unit_test_request &req, int32_t &result_code) {
        return handle(req, result_code);
      });
  bound_ = true;
}

void mock_db::unbind() {
  if (!bound_) {
    return;
  }
  rpc::db::hash_table::set_hash_table_hook_for_unit_test(nullptr);
  bound_ = false;
  // Uniform engine lifecycle contract: unbind resets all state so a runtime restart starts clean
  // (same as mock_ss/mock_dns).
  kv_records_.clear();
  kl_records_.clear();
  calls_.clear();
  diagnostic_.clear();
  clock_overridden_ = false;
}

bool mock_db::handle(const rpc::db::hash_table::unit_test_request &req, int32_t &result_code) {
  int32_t res = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  switch (req.op) {
    case op_type::kv_get_all:
      res = on_kv_get_all(req);
      break;
    case op_type::kv_partly_get:
      res = on_kv_partly_get(req);
      break;
    case op_type::kv_set:
      res = on_kv_set(req);
      break;
    case op_type::kv_inc_field:
      res = on_kv_inc_field(req);
      break;
    case op_type::kl_get_all:
      res = on_kl_get_all(req);
      break;
    case op_type::kl_get_by_indexs:
      res = on_kl_get_by_indexs(req);
      break;
    case op_type::kl_update_by_index:
      res = on_kl_update_by_index(req);
      break;
    case op_type::kl_add_index:
      res = on_kl_add_index(req);
      break;
    case op_type::kl_remove_by_index:
      res = on_kl_remove_by_index(req);
      break;
    case op_type::remove_all:
      res = on_remove_all(req);
      break;
    case op_type::set_ttl:
      res = on_set_ttl(req);
      break;
    case op_type::remove_ttl:
      res = on_remove_ttl(req);
      break;
    default:
      res = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      break;
  }

  calls_.push_back(db_request_record{req.op, std::string{req.key}, res});
  result_code = res;
  return true;
}

int32_t mock_db::make_kv_output(const kv_record &record, rpc::context &ctx,
                                db_key_value_message_result_t *output) const {
  auto factory_iter = factories_.find(record.type_name);
  if (factory_iter == factories_.end() || !factory_iter->second) {
    FWLOGERROR("mock_db: no message factory registered for {}", record.type_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }
  auto message = factory_iter->second(ctx);
  if (!message->ParseFromString(record.data)) {
    FWLOGERROR("mock_db: parse stored data of {} failed", record.type_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }
  output->message = atfw::util::memory::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(
      std::move(message));
  output->version = record.version;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::make_kl_output(const kl_entry &entry, rpc::context &ctx,
                                db_key_list_message_result_t &output) const {
  output.list_index = entry.index;
  auto factory_iter = factories_.find(entry.type_name);
  if (factory_iter == factories_.end() || !factory_iter->second) {
    FWLOGERROR("mock_db: no message factory registered for {}", entry.type_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }
  auto message = factory_iter->second(ctx);
  if (!message->ParseFromString(entry.data)) {
    FWLOGERROR("mock_db: parse stored data of {} failed", entry.type_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
  }
  output.message = atfw::util::memory::make_strong_rc<rpc::shared_abstract_message<google::protobuf::Message>>(
      std::move(message));
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kv_get_all(const rpc::db::hash_table::unit_test_request &req) {
  kv_record *record = find_live_kv(req.key);
  if (nullptr == record || nullptr == req.kv_output || nullptr == req.ctx) {
    return PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND;
  }
  return make_kv_output(*record, *req.ctx, req.kv_output);
}

int32_t mock_db::on_kv_partly_get(const rpc::db::hash_table::unit_test_request &req) {
  kv_record *record = find_live_kv(req.key);
  if (nullptr == record || nullptr == req.kv_output || nullptr == req.ctx) {
    return PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND;
  }
  int32_t res = make_kv_output(*record, *req.ctx, req.kv_output);
  if (res < 0 || nullptr == req.partly_get_fields) {
    return res;
  }

  // Drop all fields that were not requested, keeping presence of the requested ones.
  google::protobuf::Message *message = req.kv_output->message->get();
  const google::protobuf::Descriptor *descriptor = message->GetDescriptor();
  const google::protobuf::Reflection *reflect = message->GetReflection();
  std::vector<const google::protobuf::FieldDescriptor *> requested;
  requested.reserve(static_cast<size_t>(req.partly_get_field_count));
  for (int32_t i = 0; i < req.partly_get_field_count; ++i) {
    const google::protobuf::FieldDescriptor *fd =
        descriptor->FindFieldByName({req.partly_get_fields[i].data(), req.partly_get_fields[i].size()});
    if (nullptr != fd) {
      requested.push_back(fd);
    }
  }
  for (int field_index = 0; field_index < descriptor->field_count(); ++field_index) {
    const google::protobuf::FieldDescriptor *fd = descriptor->field(field_index);
    if (std::find(requested.begin(), requested.end(), fd) == requested.end()) {
      reflect->ClearField(message, fd);
    }
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kv_set(const rpc::db::hash_table::unit_test_request &req) {
  if (nullptr == req.store) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  std::string data;
  if (!req.store->SerializeToString(&data)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }
  const absl::string_view store_full_name = req.store->GetDescriptor()->full_name();
  const std::string type_name{store_full_name.data(), store_full_name.size()};

  kv_record *record = find_live_kv(req.key);
  if (nullptr == req.version) {
    // Unconditional HSET: keep the CAS version untouched.
    uint64_t old_version = nullptr == record ? 0 : record->version;
    kv_record &target = kv_records_[std::string{req.key}];
    target.type_name = type_name;
    target.data = std::move(data);
    target.version = old_version;
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }

  // CAS script semantics: a record with no CAS_VERSION (absent, or written only by unversioned
  // HSET) accepts any expected version; otherwise the stored CAS_VERSION must match. On success the
  // stored version is bumped (real_version + 1), not the expected one.
  uint64_t current_version = nullptr == record ? 0 : record->version;
  if (current_version != 0 && current_version != *req.version) {
    *req.version = current_version;
    return PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION;
  }

  kv_record &target = kv_records_[std::string{req.key}];
  bool has_expire = nullptr == record ? false : record->has_expire;
  clock::time_point expire_at = nullptr == record ? clock::time_point{} : record->expire_at;
  target.type_name = type_name;
  target.data = std::move(data);
  target.version = current_version + 1;
  target.has_expire = has_expire;
  target.expire_at = expire_at;
  *req.version = target.version;
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kv_inc_field(const rpc::db::hash_table::unit_test_request &req) {
  if (nullptr == req.inc_message) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  const google::protobuf::FieldDescriptor *fd =
      req.inc_message->GetDescriptor()->FindFieldByName({req.inc_field.data(), req.inc_field.size()});
  if (nullptr == fd || !is_integer_field(*fd)) {
    FWLOGERROR("mock_db: inc field {} is not an integer field of {}", req.inc_field,
               req.inc_message->GetDescriptor()->full_name());
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  int64_t delta = get_integer_field(*req.inc_message, *fd);
  std::unique_ptr<google::protobuf::Message> instance{req.inc_message->New()};

  kv_record *record = find_live_kv(req.key);
  if (nullptr != record && !record->data.empty()) {
    if (!instance->ParseFromString(record->data)) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK;
    }
  }

  int64_t new_value = get_integer_field(*instance, *fd) + delta;
  set_integer_field(*instance, *fd, new_value);

  // HINCRBY creates the hash when missing and never touches CAS_VERSION.
  kv_record &target = kv_records_[std::string{req.key}];
  if (nullptr == record) {
    const absl::string_view inc_full_name = req.inc_message->GetDescriptor()->full_name();
    target.type_name = std::string{inc_full_name.data(), inc_full_name.size()};
    target.version = 0;
  }
  if (!instance->SerializeToString(&target.data)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  // Mirror the real wait path: the unpack-produced message (containing only the incremented field)
  // is swapped into the caller message, so every other field is dropped.
  set_integer_field(*req.inc_message, *fd, new_value);
  const google::protobuf::Descriptor *inc_descriptor = req.inc_message->GetDescriptor();
  const google::protobuf::Reflection *inc_reflect = req.inc_message->GetReflection();
  for (int field_index = 0; field_index < inc_descriptor->field_count(); ++field_index) {
    const google::protobuf::FieldDescriptor *other_fd = inc_descriptor->field(field_index);
    if (other_fd != fd) {
      inc_reflect->ClearField(req.inc_message, other_fd);
    }
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kl_get_all(const rpc::db::hash_table::unit_test_request &req) {
  kl_record *record = find_live_kl(req.key);
  if (nullptr == record || record->entries.empty() || nullptr == req.kl_output || nullptr == req.ctx) {
    return PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND;
  }

  req.kl_output->clear();
  req.kl_output->reserve(record->entries.size());
  for (const auto &entry : record->entries) {
    req.kl_output->emplace_back();
    int32_t res = make_kl_output(entry, *req.ctx, req.kl_output->back());
    if (res < 0) {
      return res;
    }
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kl_get_by_indexs(const rpc::db::hash_table::unit_test_request &req) {
  if (nullptr == req.kl_output || nullptr == req.ctx) {
    return PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND;
  }

  // One slot per requested index, in request order. A missing key behaves like the real HMGET: an
  // all-NIL reply, so every slot gets a null message and the call succeeds.
  kl_record *record = find_live_kl(req.key);
  req.kl_output->clear();
  req.kl_output->reserve(req.list_index.size());
  for (uint64_t index : req.list_index) {
    req.kl_output->emplace_back();
    db_key_list_message_result_t &slot = req.kl_output->back();
    slot.list_index = index;
    if (nullptr == record) {
      continue;
    }
    auto entry_iter = std::find_if(record->entries.begin(), record->entries.end(),
                                   [index](const kl_entry &entry) { return entry.index == index; });
    if (entry_iter == record->entries.end()) {
      continue;
    }
    int32_t res = make_kl_output(*entry_iter, *req.ctx, slot);
    if (res < 0) {
      return res;
    }
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kl_update_by_index(const rpc::db::hash_table::unit_test_request &req) {
  if (nullptr == req.store || req.list_index.empty()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  std::string data;
  if (!req.store->SerializeToString(&data)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }
  const absl::string_view store_full_name = req.store->GetDescriptor()->full_name();
  const std::string type_name{store_full_name.data(), store_full_name.size()};
  uint64_t index = req.list_index.front();

  kl_record &record = kl_records_[std::string{req.key}];
  auto entry_iter = std::find_if(record.entries.begin(), record.entries.end(),
                                 [index](const kl_entry &entry) { return entry.index == index; });
  if (entry_iter == record.entries.end()) {
    record.entries.push_back(kl_entry{index, type_name, std::move(data)});
  } else {
    entry_iter->type_name = type_name;
    entry_iter->data = std::move(data);
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kl_add_index(const rpc::db::hash_table::unit_test_request &req) {
  if (nullptr == req.store) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  std::string data;
  if (!req.store->SerializeToString(&data)) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PACK;
  }

  kl_record &record = kl_records_[std::string{req.key}];
  const absl::string_view store_full_name = req.store->GetDescriptor()->full_name();
  record.entries.push_back(
      kl_entry{record.next_index, std::string{store_full_name.data(), store_full_name.size()}, std::move(data)});
  ++record.next_index;
  while (req.max_list_length > 0 && record.entries.size() > req.max_list_length) {
    record.entries.pop_front();
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_kl_remove_by_index(const rpc::db::hash_table::unit_test_request &req) {
  kl_record *record = find_live_kl(req.key);
  if (nullptr == record) {
    return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  }
  record->entries.erase(std::remove_if(record->entries.begin(), record->entries.end(),
                                       [&req](const kl_entry &entry) {
                                         return std::find(req.list_index.begin(), req.list_index.end(),
                                                          entry.index) != req.list_index.end();
                                       }),
                        record->entries.end());
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_remove_all(const rpc::db::hash_table::unit_test_request &req) {
  erase_key(req.key);
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_set_ttl(const rpc::db::hash_table::unit_test_request &req) {
  clock::time_point expire_at = now() + std::chrono::seconds{req.ttl_second};
  kv_record *kv = find_live_kv(req.key);
  if (nullptr != kv) {
    kv->has_expire = true;
    kv->expire_at = expire_at;
  }
  kl_record *kl = find_live_kl(req.key);
  if (nullptr != kl) {
    kl->has_expire = true;
    kl->expire_at = expire_at;
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int32_t mock_db::on_remove_ttl(const rpc::db::hash_table::unit_test_request &req) {
  kv_record *kv = find_live_kv(req.key);
  if (nullptr != kv) {
    kv->has_expire = false;
  }
  kl_record *kl = find_live_kl(req.key);
  if (nullptr != kl) {
    kl->has_expire = false;
  }
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

}  // namespace testing
}  // namespace atsf4g
