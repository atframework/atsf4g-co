// Copyright 2026 atframework

#include "logic/action/task_action_filter_text.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "logic/text_filter_manager.h"

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::task_action_filter_text(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::~task_action_filter_text() {}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API const char* task_action_filter_text::name() const {
  return "task_action_filter_text";
}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API task_action_filter_text::result_type
task_action_filter_text::operator()() {
  const auto& request = get_request_body();
  auto& response = get_response_body();
  response.Clear();
  if (request.items().empty()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  auto dictionary = compliance::text_filter_manager::me()->get_dictionary();
  if (!dictionary) {
    set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  using automaton_t = compliance::text_filter_manager::dictionary_t;
  automaton_t::match_options options;
  options.allow_overlap = false;
  options.invalid_input = automaton_t::invalid_input_policy::kReject;
  automaton_t::match_context context;
  std::vector<automaton_t::match_t> matches;
  response.mutable_results()->Reserve(request.items_size());
  for (const auto& item : request.items()) {
    matches.clear();
    auto report = dictionary->match(item.text(), matches, options, &context);
    if (report.code != automaton_t::status::kOk || report.stopped != automaton_t::stop_reason::kEndOfInput) {
      response.Clear();
      set_response_code(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
    auto* result = response.add_results();
    if (item.settings().return_original_text()) {
      result->set_original_text(item.text());
    }
    if (matches.empty()) {
      result->set_filtered_text(item.text());
      result->set_status(atframework::compliance::EN_TEXT_FILTER_STATUS_OK);
      continue;
    }
    result->set_status(atframework::compliance::EN_TEXT_FILTER_STATUS_MATCHED);
    std::sort(matches.begin(), matches.end(), [](const automaton_t::match_t& left, const automaton_t::match_t& right) {
      return left.start < right.start || (left.start == right.start && left.length > right.length);
    });
    std::string* output = result->mutable_filtered_text();
    size_t copied = 0;
    for (size_t index = 0; index < matches.size();) {
      size_t begin = matches[index].start;
      size_t end = begin + matches[index].length;
      ++index;
      while (index < matches.size() && matches[index].start < end) {
        end = std::max(end, matches[index].start + matches[index].length);
        ++index;
      }
      if (begin > copied) {
        output->append(item.text(), copied, begin - copied);
      }
      output->append(item.settings().replacement_text());
      copied = end;
    }
    if (item.text().size() > copied) {
      output->append(item.text(), copied, std::string::npos);
    }
  }
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int task_action_filter_text::on_success() { return get_result(); }

ATFRAMEWORK_COMPLIANCE_COMPLIANCESERVICE_API int task_action_filter_text::on_failed() { return get_result(); }
