// Copyright 2021 atframework
// Created by owent on 2019/07/05.
//

#ifndef LOGIC_SIMPLE_TEMPLATE_CORE_H
#define LOGIC_SIMPLE_TEMPLATE_CORE_H

#pragma once

#include <design_pattern/noncopyable.h>

#include <functional>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

class simple_template_core : public ::util::design_pattern::noncopyable {
 public:
  using ptr_t = std::unique_ptr<simple_template_core>;
  using getter_fn_t = std::function<bool(std::ostream&, void*)>;
  using call_fn_t = std::function<bool(std::ostream&, void*)>;
  using getter_creator_fn_t =
      std::function<getter_fn_t(const std::string&, const char*, const char*)>; /** name, token start, token end **/
  using call_creator_fn_t = std::function<call_fn_t(const std::string&, const std::vector<std::string>&, const char*,
                                                    const char*)>; /** name, parameters, token start, token end **/

  using code_block_fn_t = std::function<bool(std::ostream&, std::ostream&, void*)>;
  using code_block_fn_ptr_t = std::shared_ptr<code_block_fn_t>;

  struct config_t {
    getter_creator_fn_t getter_fn_creator;
    call_creator_fn_t call_creator_fn;

    bool enable_share_functions;
    std::unordered_map<std::string, code_block_fn_ptr_t> var_getter_cache;
    std::unordered_map<std::string, code_block_fn_ptr_t> func_call_cache;

    config_t();

    inline void clear_cache() {
      var_getter_cache.clear();
      func_call_cache.clear();
    }
  };

 private:
  simple_template_core();

 public:
  bool render(void* priv_data, std::string& output, std::string* errmsg = nullptr);

  static ptr_t compile(const char* const in, const size_t insz, config_t& conf, std::string* errmsg = nullptr);

 private:
  struct print_const_string {
    print_const_string(const std::string&, size_t, size_t, const std::string&);
    bool operator()(std::ostream&, std::ostream&, void*);
    std::string raw_block;
    size_t line;
    size_t col;
    std::string data;
  };

  struct getter_fn_wrapper {
    getter_fn_wrapper(const std::string&, size_t, size_t, getter_fn_t);
    bool operator()(std::ostream&, std::ostream&, void*);
    std::string raw_block;
    size_t line;
    size_t col;
    getter_fn_t fn;
  };

  struct call_fn_wrapper {
    call_fn_wrapper(const std::string&, size_t, size_t, call_fn_t);
    bool operator()(std::ostream&, std::ostream&, void*);
    std::string raw_block;
    size_t line;
    size_t col;
    call_fn_t fn;
  };

  std::list<code_block_fn_ptr_t> blocks_;
};

template <typename TPRIVATE_DATA>
class simple_template {
 public:
  using private_data_t = TPRIVATE_DATA;
  using self_type = simple_template<private_data_t>;
  using ptr_t = std::unique_ptr<self_type>;
  using shared_ptr_t = std::shared_ptr<self_type>;
  using getter_fn_t = std::function<bool(std::ostream&, private_data_t&)>;
  using call_fn_t = std::function<bool(std::ostream&, private_data_t&)>;
  using getter_creator_fn_t =
      std::function<getter_fn_t(const std::string&, const char*, const char*)>; /** name, token start, token end **/
  using call_creator_fn_t = std::function<call_fn_t(const std::string&, const std::vector<std::string>&, const char*,
                                                    const char*)>; /** name, parameters, token start, token end **/

  struct config_t {
    getter_creator_fn_t getter_fn_creator;
    call_creator_fn_t call_creator_fn;

    simple_template_core::config_t core;

    inline void clear_cache() { core.clear_cache(); }
  };

 private:
  simple_template() {}

  struct getter_fn_t_wrapper {
    explicit getter_fn_t_wrapper(getter_fn_t f) : fn(f) {}
    getter_fn_t fn;

    bool operator()(std::ostream& os, void* priv_data) {
      if (!fn || priv_data == nullptr) {
        return false;
      }

      return fn(os, *reinterpret_cast<private_data_t*>(priv_data));
    }
  };

  struct call_fn_t_wrapper {
    explicit call_fn_t_wrapper(call_fn_t f) : fn(f) {}
    call_fn_t fn;

    bool operator()(std::ostream& os, void* priv_data) {
      if (!fn || priv_data == nullptr) {
        return false;
      }

      return fn(os, *reinterpret_cast<private_data_t*>(priv_data));
    }
  };

  struct getter_creator_fn_t_wrapper {
    explicit getter_creator_fn_t_wrapper(getter_creator_fn_t f) : fn(f) {}
    getter_creator_fn_t fn;

    simple_template_core::getter_fn_t operator()(const std::string& name, const char* token_start,
                                                 const char* token_end) {
      if (!fn) {
        return nullptr;
      }

      getter_fn_t res = fn(name, token_start, token_end);
      if (!res) {
        return nullptr;
      }

      return getter_fn_t_wrapper(res);
    }
  };

  struct call_creator_fn_t_wrapper {
    explicit call_creator_fn_t_wrapper(call_creator_fn_t f) : fn(f) {}
    call_creator_fn_t fn;

    simple_template_core::call_fn_t operator()(const std::string& name, const std::vector<std::string>& params,
                                               const char* token_start, const char* token_end) {
      if (!fn) {
        return nullptr;
      }

      call_fn_t res = fn(name, params, token_start, token_end);
      if (!res) {
        return nullptr;
      }

      return call_fn_t_wrapper(res);
    }
  };

 public:
  bool render(private_data_t& priv_data, std::string& output, std::string* errmsg = nullptr) {
    if (!core_) {
      return false;
    }

    return core_->render(reinterpret_cast<void*>(&priv_data), output, errmsg);
  }

  static ptr_t compile(const char* const in, const size_t insz, config_t& conf, std::string* errmsg = nullptr) {
    ptr_t ret = ptr_t(new self_type());
    if (!ret) {
      return nullptr;
    }

    if (conf.getter_fn_creator) {
      conf.core.getter_fn_creator = getter_creator_fn_t_wrapper(conf.getter_fn_creator);
    }

    if (conf.call_creator_fn) {
      conf.core.call_creator_fn = call_creator_fn_t_wrapper(conf.call_creator_fn);
    }

    ret->core_ = simple_template_core::compile(in, insz, conf.core, errmsg);

    if (!ret->core_) {
      return nullptr;
    }

    return ret;
  }

 private:
  simple_template_core::ptr_t core_;
};

#endif
