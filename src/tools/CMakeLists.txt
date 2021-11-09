add_subdirectory(etcd-atproxy-ls)
add_subdirectory(etcd-watcher)

if(NOT MSVC
   AND NOT MINGW
   AND NOT CYGWIN)
  add_subdirectory(simulator)
endif()
