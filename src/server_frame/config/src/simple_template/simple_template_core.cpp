// Copyright 2022 atframework
// Created by owent on 2019-07-05.
//

#include "config/simple_template/simple_template_core.h"

#include <string/utf8_char_t.h>

#include <memory/object_allocator.h>

#include <sstream>
#include <utility>

namespace excel {

namespace details {
struct token_data_t {
  size_t line;
  size_t column;
  const char* block_begin;
  const char* block_end;
};

static bool is_eol(const char& c) { return c == '\r' || c == '\n'; }

static bool is_blank(const char& c) { return c == ' ' || c == '\t' || is_eol(c); }

static bool is_ascii_identify(const char& c) {
  return c && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$');
}

static bool is_sub_identify(const char& c) { return c && c == '.'; }

static bool is_function_begin(const char& c) { return c && c == '('; }

static bool is_function_end(const char& c) { return c && c == ')'; }

static bool is_function_param_split(const char& c) { return c && c == ','; }

static const char* take_blanks(const char* start, token_data_t& out) {
  char prechar = 0;
  out.block_begin = start;
  while (start && *start && is_blank(*start)) {
    if (is_eol(*start)) {
      // 兼容Windows换行符
      if (!(prechar == '\r' && *start == '\n')) {
        out.column = 1;
        ++out.line;
      }
    } else {
      ++out.column;
    }

    prechar = *start;
    ++start;
  }

  out.block_end = start;
  return start;
}

static std::pair<bool, const char*> take_utf8_char(const char* start, size_t sz) {
  if (!start || !*start) {
    if (sz > 0) {
      return std::make_pair(false, start + 1);
    } else {
      return std::make_pair(false, start);
    }
  }

  size_t len = atfw::util::string::utf8_char_t::length(start);
  if (len > sz) {
    return std::make_pair(false, start + sz);
  }

  return std::make_pair(true, start + len);
}

static std::string make_identify(const char* begin, const char* end) {
  if (nullptr == begin || nullptr == end || end <= begin) {
    return std::string();
  }

  if ((*begin == '\'' || *begin == '"') && end > begin + 1 && *(end - 1) == *begin) {
    ++begin;
    --end;
    std::string ret;
    ret.reserve(static_cast<size_t>(end - begin));
    for (const char* cur = begin; cur < end; ++cur) {
      // 转义符
      if (*cur == '\\') {
        ++cur;
        if (cur < end) {
          switch (*cur) {
            case '0':
              ret.push_back('\0');
              break;
            case 'a':
              ret.push_back('\a');
              break;
            case 'b':
              ret.push_back('\b');
              break;
            case 'f':
              ret.push_back('\f');
              break;
            case 'v':
              ret.push_back('\v');
              break;
            case 't':
              ret.push_back('\t');
              break;
            case 'r':
              ret.push_back('\r');
              break;
            case 'n':
              ret.push_back('\n');
              break;
            default:
              ret.push_back(*cur);
              break;
          }
        }
      } else {
        ret.push_back(*cur);
      }
    }

    return ret;
  }

  return std::string(begin, end);
}

static std::pair<bool, const char*> take_identify(const char* const start, size_t sz, token_data_t& out) {
  bool ret = true;
  out.block_begin = start;
  out.block_end = start;
  if (start && (*start == '\'' || *start == '"')) {
    char strclose = *start;
    char prechar = *start;
    bool is_convert = false;
    out.block_begin = start + 1;
    out.block_end = out.block_begin;
    while (*out.block_end && *out.block_end != strclose && false == is_convert) {
      std::pair<bool, const char*> cres =
          take_utf8_char(out.block_end, sz - static_cast<size_t>(out.block_end - start));
      if (is_eol(*out.block_end)) {
        // 兼容Windows换行符
        if (!(prechar == '\r' && *out.block_end == '\n')) {
          out.column = 1;
          ++out.line;
        }
      } else {
        ++out.column;
      }
      prechar = *out.block_end;
      out.block_end = cres.second;

      if (prechar == '\\' && false == is_convert) {
        is_convert = true;
      } else {
        is_convert = false;
      }
    }

    if (*out.block_end == strclose) {
      ++out.block_end;
      ++out.column;
    } else {
      // start = out.block_end;
      ret = false;
    }
  } else {
    while (out.block_end && *out.block_end) {
      std::pair<bool, const char*> cres =
          take_utf8_char(out.block_end, sz - static_cast<size_t>(out.block_end - start));
      if (!cres.first) {
        if (cres.second > out.block_end) {
          ++out.column;
          out.block_end = cres.second;
        }
        ret = false;
        break;
      }

      if (cres.second - out.block_end <= 1 && !is_ascii_identify(*out.block_end)) {
        break;
      }

      ++out.column;
      out.block_end = cres.second;
    }
  }

  if (ret) {
    ret = out.block_end > out.block_begin;
  }
  return std::make_pair(ret, out.block_end);
}

static const char* take_block(const char* const start, size_t sz, std::string& expression,
                              std::vector<std::string>& params, bool& is_func, token_data_t& out, std::ostream& errs) {
  if (!start || !*start || sz == 0) {
    return start;
  }

  std::stringstream expr_ss;
  token_data_t segment = out;
  const char* next_token = take_blanks(start, segment);
  bool next_ident = true;
  bool first_ident = true;
  out.block_begin = next_token;

  while (next_ident && *next_token) {
    next_token = take_blanks(next_token, segment);

    std::pair<bool, const char*> ident_res =
        take_identify(next_token, sz - static_cast<size_t>(next_token - start), segment);
    if (!ident_res.first || next_token >= ident_res.second) {
      errs << "Identify " << next_token << " end at line: " << segment.line << ", column: " << segment.column
           << " is not a valid" << std::endl;
    } else {
      if (!first_ident) {
        expr_ss << ".";
      }
      expr_ss << make_identify(next_token, ident_res.second);
      first_ident = false;
    }
    if (next_token < ident_res.second) {
      next_token = ident_res.second;
    } else {
      ++segment.column;
      ++next_token;
    }

    next_token = take_blanks(next_token, segment);
    next_ident = is_sub_identify(*next_token);
    if (next_ident) {
      ++segment.column;
      ++next_token;
    }
  }
  expr_ss.str().swap(expression);

  if (is_function_begin(*next_token)) {
    is_func = true;
    ++segment.column;
    ++next_token;

    bool next_param = true;
    params.clear();
    while (next_param && *next_token) {
      next_token = take_blanks(next_token, segment);

      std::pair<bool, const char*> ident_res =
          take_identify(next_token, sz - static_cast<size_t>(next_token - start), segment);
      if (!ident_res.first || next_token >= ident_res.second) {
        errs << "Parameter " << next_token << " end at line: " << segment.line << ", column: " << segment.column
             << " is not a valid UTF-8 string" << std::endl;
      } else {
        params.push_back(make_identify(next_token, ident_res.second));
      }
      if (next_token < ident_res.second) {
        next_token = ident_res.second;
      } else {
        ++segment.column;
        ++next_token;
      }

      next_token = take_blanks(next_token, segment);
      next_param = is_function_param_split(*next_token);
      if (next_param) {
        ++segment.column;
        ++next_token;
      }
    }

    if (is_function_end(*next_token)) {
      ++segment.column;
      ++next_token;

      // consume tail spaces
      next_token = take_blanks(next_token, segment);
    } else {
      errs << "Function " << expression << " end at line: " << segment.line << ", column: " << segment.column
           << " missing closing \')\'" << std::endl;
    }
  } else {
    is_func = false;
  }

  out.block_end = next_token;
  out.line = segment.line;
  out.column = segment.column;

  return next_token;
}
}  // namespace details

SERVER_FRAME_CONFIG_API simple_template_core::config_t::config_t() : enable_share_functions(true) {}

SERVER_FRAME_CONFIG_API void simple_template_core::config_t::clear_cache() {
  var_getter_cache.clear();
  func_call_cache.clear();
}

simple_template_core::simple_template_core() {}

SERVER_FRAME_CONFIG_API bool simple_template_core::render(void* priv_data, std::string& output, std::string* errmsg) {
  bool ret = true;
  std::stringstream output_ss;
  std::stringstream errmsg_ss;
  for (auto& rb : blocks_) {
    if (rb && !(*rb)(output_ss, errmsg_ss, priv_data)) {
      ret = false;
    }
  }

  if (errmsg != nullptr) {
    errmsg_ss.str().swap(*errmsg);
  }
  output_ss.str().swap(output);
  return ret;
}

SERVER_FRAME_CONFIG_API simple_template_core::ptr_t simple_template_core::compile(const char* const in,
                                                                                  const size_t insz, config_t& conf,
                                                                                  std::string* errmsg) {
  std::stringstream errmsg_ss;
  ptr_t ret = ptr_t(new simple_template_core());

  do {
    if (nullptr == in || !*in || insz == 0) {
      return ret;
    }

    details::token_data_t token_position;
    token_position.block_begin = in;
    token_position.block_end = in;
    token_position.line = 1;
    token_position.column = 1;
    size_t previous_block_line = token_position.line;
    size_t previous_block_column = token_position.column;
    char prechar = 0;
    const char* next_token = in;
    while (static_cast<size_t>(next_token - in) < insz) {
      std::pair<bool, const char*> cres =
          details::take_utf8_char(next_token, insz - static_cast<size_t>(next_token - in));
      if (!cres.first) {
        errmsg_ss << "Character " << next_token << " at line: " << token_position.line
                  << ", column: " << token_position.column << " is not a valid UTF-8 charater." << std::endl;
      }

      if (details::is_eol(*next_token)) {
        // 兼容Windows换行符
        if (!(prechar == '\r' && *next_token == '\n')) {
          token_position.column = 1;
          ++token_position.line;
        }
      } else {
        ++token_position.column;
      }

      bool start_expression = '{' == *next_token && '{' == prechar;
      prechar = *next_token;
      if (next_token < cres.second) {
        next_token = cres.second;
      } else {
        ++next_token;
      }

      if (start_expression) {
        token_position.block_end = next_token;
        // prefix const data block
        if (token_position.block_end > token_position.block_begin + 2) {
          std::string code =
              std::string(token_position.block_begin, token_position.block_end - 2);  // block_end[-2:0] is "{{"
          ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
              print_const_string(code, previous_block_line, previous_block_column, code)));
        }

        token_position.block_begin = next_token;
        previous_block_line = token_position.line;
        previous_block_column = token_position.column;

        std::string expression;
        std::vector<std::string> params;
        bool is_func = false;
        const char* token_start = next_token;
        next_token = take_block(next_token, insz - static_cast<size_t>(next_token - in), expression, params, is_func,
                                token_position, errmsg_ss);
        std::string code = std::string(token_start, next_token);
        do {
          if (is_func) {
            std::string func_full_expr;
            if (conf.enable_share_functions) {
              std::stringstream func_full_expr_ss;
              func_full_expr_ss << expression << "(";
              for (size_t i = 0; i < params.size(); ++i) {
                if (i != 0) {
                  func_full_expr_ss << ",";
                }
                func_full_expr_ss << params[i];
              }
              func_full_expr_ss << ")";
              func_full_expr_ss.str().swap(func_full_expr);

              auto cache_func = conf.func_call_cache.find(func_full_expr);
              if (cache_func != conf.func_call_cache.end()) {
                ret->blocks_.push_back(cache_func->second);
                break;
              }
            }

            if (!conf.call_creator_fn) {
              errmsg_ss << "Function creator is undefined, we just use the code \"" << code
                        << "\" at line: " << previous_block_line << ", column: " << previous_block_column << std::endl;
              ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
                  print_const_string(code, previous_block_line, previous_block_column, code)));
              break;
            }

            call_fn_t fn = conf.call_creator_fn(expression, params, token_start, next_token);
            if (!fn) {
              errmsg_ss << "Function \"" << expression
                        << "\" is not supported by now, we just use the raw data for fallback with code \"" << code
                        << "\" at line: " << previous_block_line << ", column: " << previous_block_column << std::endl;
              ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
                  print_const_string(code, previous_block_line, previous_block_column, code)));
              break;
            }

            code_block_fn_ptr_t block_fn = atfw::memory::stl::make_shared<code_block_fn_t>(
                call_fn_wrapper(code, previous_block_line, previous_block_column, fn));
            ret->blocks_.push_back(block_fn);

            if (conf.enable_share_functions) {
              conf.func_call_cache[func_full_expr] = block_fn;
            }

          } else {
            if (conf.enable_share_functions) {
              auto cache_func = conf.var_getter_cache.find(expression);
              if (cache_func != conf.var_getter_cache.end()) {
                ret->blocks_.push_back(cache_func->second);
                break;
              }
            }

            if (!conf.getter_fn_creator) {
              errmsg_ss << "Variable getter creator is undefined, we just use the code \"" << code
                        << "\" at line: " << previous_block_line << ", column: " << previous_block_column << std::endl;
              ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
                  print_const_string(code, previous_block_line, previous_block_column, code)));
              break;
            }

            getter_fn_t fn = conf.getter_fn_creator(expression, token_start, next_token);
            if (!fn) {
              errmsg_ss << "Variable getter \"" << expression
                        << "\" is not supported by now, we just use the raw data for fallback with code \"" << code
                        << "\" at line: " << previous_block_line << ", column: " << previous_block_column << std::endl;
              ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
                  print_const_string(code, previous_block_line, previous_block_column, code)));
              break;
            }

            code_block_fn_ptr_t block_fn = atfw::memory::stl::make_shared<code_block_fn_t>(
                getter_fn_wrapper(code, previous_block_line, previous_block_column, fn));
            ret->blocks_.push_back(block_fn);

            if (conf.enable_share_functions) {
              conf.var_getter_cache[expression] = block_fn;
            }
          }
        } while (false);

        // tail }} identify
        next_token = details::take_blanks(next_token, token_position);
        if (*next_token && '}' == *next_token && '}' == *(next_token + 1)) {
          next_token += 2;
          token_position.column += 2;
          prechar = '}';  // reset block status
        } else {
          if (*next_token) {
            errmsg_ss << "Expect \"}}\" in code \"" << expression << "\" at line: " << token_position.line
                      << ", column: " << token_position.column << ", but we got \"" << *next_token << "\"" << std::endl;
          } else {
            errmsg_ss << "Expect \"}}\" in code \"" << expression << "\" at line: " << token_position.line
                      << ", column: " << token_position.column << ", but we got nullptr" << std::endl;
          }
          prechar = *next_token;  // reset block status
        }

        token_position.block_begin = next_token;
        token_position.block_end = next_token;
        previous_block_line = token_position.line;
        previous_block_column = token_position.column;
      }
    }

    // tail const block
    token_position.block_end = next_token;
    if (token_position.block_end > token_position.block_begin) {
      std::string code = std::string(token_position.block_begin, token_position.block_end);
      ret->blocks_.push_back(atfw::memory::stl::make_shared<code_block_fn_t>(
          print_const_string(code, previous_block_line, previous_block_column, code)));
    }
  } while (false);

  if (errmsg != nullptr) {
    errmsg_ss.str().swap(*errmsg);
  }

  return ret;
}

simple_template_core::print_const_string::print_const_string(const std::string& r, size_t l, size_t c,
                                                             const std::string& s)
    : raw_block(r), line(l), col(c), data(s) {}

bool simple_template_core::print_const_string::operator()(std::ostream& os, std::ostream& /*es*/, void*) {
  os.write(data.c_str(), static_cast<std::streamsize>(data.size()));
  return true;
}

simple_template_core::getter_fn_wrapper::getter_fn_wrapper(const std::string& r, size_t l, size_t c, getter_fn_t f)
    : raw_block(r), line(l), col(c), fn(f) {}

bool simple_template_core::getter_fn_wrapper::operator()(std::ostream& os, std::ostream& es, void* priv_data) {
  if (!fn) {
    es << "variable block at line: " << line << ", column: " << col << " is not valid, code: " << raw_block
       << std::endl;
    return false;
  }

  if (fn(os, priv_data)) {
    return true;
  }

  es << "get variable block at line: " << line << ", column: " << col << " failed, code: " << raw_block << std::endl;
  return false;
}

simple_template_core::call_fn_wrapper::call_fn_wrapper(const std::string& r, size_t l, size_t c, call_fn_t f)
    : raw_block(r), line(l), col(c), fn(f) {}

bool simple_template_core::call_fn_wrapper::operator()(std::ostream& os, std::ostream& es, void* priv_data) {
  if (!fn) {
    es << "function block at line: " << line << ", column: " << col << " is not valid, code: " << raw_block
       << std::endl;
    return false;
  }

  if (fn(os, priv_data)) {
    return true;
  }

  es << "run function block at line: " << line << ", column: " << col << " with error, code: " << raw_block
     << std::endl;
  return false;
}

}  // namespace excel
