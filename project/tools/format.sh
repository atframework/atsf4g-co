#!/bin/bash


# Require python3 -m pip install --user cmake-format 
find .  -regex '^./build_jobs/*' -prune -o                       \
        -o -regex '^./build_jobs_.*' -prune -o                   \
        -o -regex '^./.vscode/.*' -prune -o                      \
        -o -regex '^./.vs/.*' -prune -o                          \
        -o -regex '^./.clion/.*' -prune -o                       \
        -o -regex '^./3rd_party/.*' -prune -o                    \
        -o -regex '^./third_party/install/.*' -prune -o          \
        -o -regex '^./third_party/packages/.*' -prune -o         \
        -o -regex '^./atframework/cmake-toolset/.*' -prune -o    \
        -o -regex '^./atframework/atframe_utils/.*' -prune -o    \
        -o -regex '^./atframework/libatbus/.*' -prune -o         \
        -o -regex '^./atframework/libatapp/.*' -prune -o         \
        -type f                                                  \
        -name "*.cmake" -print                                   \
        -o -name "*.cmake.in" -print                             \
        -o -name 'CMakeLists.txt' -print                         \
        | xargs cmake-format -i

find . -type f                                        \
  -regex '^./build_jobs/*' -prune -o                  \
  -o -regex '^./build_jobs_.*' -prune -o              \
  -o -regex "^./third_party/packages/.*" -prune       \
  -o -regex "^./third_party/install/.*" -prune        \
  -o -regex "^./atframework/cmake-toolset/.*" -prune  \
  -o -regex "^./atframework/atframe_utils/.*" -prune  \
  -o -regex "^./atframework/libatbus/.*" -prune       \
  -o -name "*.h" -print                               \
  -o -name "*.hpp" -print                             \
  -o -name "*.cxx" -print                             \
  -o -name '*.cpp' -print                             \
  -o -name '*.cc' -print                              \
  -o -name '*.c' -print                               \
  | xargs -r -n 32 clang-format -i --style=file --fallback-style=none
