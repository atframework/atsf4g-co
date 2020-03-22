//
// Created by owt50 on 2016/10/9.
//

#ifndef LOGIC_SVR_APP_HANDLE_SS_MSG_H
#define LOGIC_SVR_APP_HANDLE_SS_MSG_H

#pragma once

#include <design_pattern/singleton.h>

class app_handle_ss_msg: public ::util::design_pattern::singleton<app_handle_ss_msg> {
public:
    int init();
};

#endif //_LOGIC_SVR_APP_HANDLE_SS_MSG_H
