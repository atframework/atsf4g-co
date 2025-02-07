// Copyright 2025 atframework

#pragma once

#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <config/compile_optimize.h>
#include <gsl/select-gsl.h>

#include <config/excel_type_trait_setting.h>

#include <assert.h>
#include <memory>

class player;

using player_ptr_t = std::shared_ptr<player>;
using player_weak_ptr_t = std::weak_ptr<player>;
