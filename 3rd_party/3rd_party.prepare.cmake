﻿# =========== 3rd_party ===========
set (PROJECT_3RD_PARTY_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})

unset(3RD_PARTY_PUBLIC_INCLUDE_DIRS)
unset(3RD_PARTY_PUBLIC_LINK_NAMES)
unset(3RD_PARTY_INTERFACE_LINK_NAMES)
unset(3RD_PARTY_COPY_EXECUTABLE_PATTERN)

find_package(Threads)