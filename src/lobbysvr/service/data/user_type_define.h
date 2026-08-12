// Copyright 2025 atframework

#pragma once

#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <config/compile_optimize.h>
#include <gsl/select-gsl.h>

#include <config/excel_type_trait_setting.h>

#include <assert.h>
#include <memory>

class user;

using user_ptr_t = std::shared_ptr<user>;
using user_weak_ptr_t = std::weak_ptr<user>;
