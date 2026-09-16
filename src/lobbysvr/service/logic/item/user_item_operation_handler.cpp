// Copyright 2026 atframework

#include "logic/item/user_item_operation_handler.h"

item_operation_handler::~item_operation_handler() {}

int32_t item_operation_handler::on_item_not_enough(rpc::context&, user&, int32_t) const {
  return PROJECT_NAMESPACE_ID::EN_ERR_ITEM_NOT_ENOUGH;
}
