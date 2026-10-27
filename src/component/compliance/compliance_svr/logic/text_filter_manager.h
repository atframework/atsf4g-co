// Copyright 2026 atframework

#pragma once

#include <design_pattern/singleton.h>
#include <string/ac_automation_v2.h>

#include <memory>
#include <mutex>

namespace compliance {

class text_filter_manager : public atfw::util::design_pattern::singleton<text_filter_manager> {
 public:
  using dictionary_t = atfw::util::string::ac_automation_v2;

 protected:
  text_filter_manager();
  ~text_filter_manager();

 public:
  int init();
  int reload();
  void cleanup();

  std::shared_ptr<const dictionary_t> get_dictionary() const;

 private:
  std::shared_ptr<const dictionary_t> dictionary_;
  mutable std::mutex dictionary_mutex_;
};

}  // namespace compliance
