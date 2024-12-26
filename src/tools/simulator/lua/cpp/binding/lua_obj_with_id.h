#ifndef SCRIPT_BINDING_LUAOBJWITHID
#define SCRIPT_BINDING_LUAOBJWITHID

#pragma once

#include <stdint.h>
#include <map>

#include <lock/lock_holder.h>
#include <lock/spin_lock.h>

namespace script {
namespace binding {

template <typename TOBJ>
class lua_obj_with_id {
 public:
  using object_type = TOBJ;
  using self_type = lua_obj_with_id<object_type>;
  using value_type = self_type *;

 protected:
  lua_obj_with_id() {
    atfw::util::lock::lock_holder slh(id_locker_);

    while (true) {
      id_ = ++id_alloc_;
      if (0 == id_) continue;

      if (id_mgr_.end() != id_mgr_.find(id_)) continue;

      break;
    }

    id_mgr_[id_] = this;
  }

  virtual ~lua_obj_with_id() {
    atfw::util::lock::lock_holder slh(id_locker_);

    id_mgr_.erase(id_);
  }

 public:
  uint64_t id() const { return id_; }

  static object_type *findByID(uint64_t id) {
    atfw::util::lock::lock_holder slh(id_locker_);

    typename std::map<uint64_t, value_type>::iterator iter = id_mgr_.find(id);
    if (id_mgr_.end() == iter) return nullptr;

    return static_cast<object_type *>(iter->second);
  }

 private:
  uint64_t id_;
  static atfw::util::lock::spin_lock id_locker_;
  static uint64_t id_alloc_;
  static std::map<uint64_t, value_type> id_mgr_;
};

template <typename TOBJ>
::util::lock::spin_lock lua_obj_with_id<TOBJ>::id_locker_;

template <typename TOBJ>
uint64_t lua_obj_with_id<TOBJ>::id_alloc_ = 0;

template <typename TOBJ>
std::map<uint64_t, typename lua_obj_with_id<TOBJ>::value_type> lua_obj_with_id<TOBJ>::id_mgr_;

}  // namespace binding
}  // namespace script

#endif
