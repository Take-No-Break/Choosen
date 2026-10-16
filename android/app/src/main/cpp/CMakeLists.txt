# The shared C++ core of Choosen (../../../../../src) built for Android, plus the POSIX platform
# layer and the JNI bridge. The Windows-only files (main, http, remote, thumbs, *_win) are left out.
cmake_minimum_required(VERSION 3.22)
project(choosen_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)

add_library(choosen_core SHARED
  ${SRC}/core.cpp
  ${SRC}/core_library.cpp
  ${SRC}/core_journal.cpp
  ${SRC}/core_schedule.cpp
  ${SRC}/core_api.cpp
  ${SRC}/core_export.cpp
  ${SRC}/common.cpp
  ${SRC}/api_routes.cpp
  ${SRC}/qr.cpp
  common_posix.cpp
  platform_posix.cpp
  jni_bridge.cpp)
target_include_directories(choosen_core PRIVATE ${SRC})
target_compile_options(choosen_core PRIVATE -Wall -Wno-unused-parameter)
target_link_libraries(choosen_core log)
