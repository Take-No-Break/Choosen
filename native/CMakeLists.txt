# The core as a shared library for the Flutter app (Version 2): choosen_core.dll on Windows,
# libchoosen_core.so on Android. Same sources as the Windows exe and the Android app; only the
# platform layer differs.
cmake_minimum_required(VERSION 3.22)
project(choosen_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

set(CORE_SOURCES
  ${SRC}/core.cpp
  ${SRC}/core_library.cpp
  ${SRC}/core_journal.cpp
  ${SRC}/core_schedule.cpp
  ${SRC}/core_api.cpp
  ${SRC}/core_export.cpp
  ${SRC}/common.cpp
  ${SRC}/api_routes.cpp
  ${SRC}/qr.cpp
  ${SRC}/capi.cpp)

if(WIN32)
  list(APPEND CORE_SOURCES ${SRC}/common_win.cpp ${SRC}/platform_win.cpp)
else()
  set(POSIX ${CMAKE_CURRENT_LIST_DIR}/../android/app/src/main/cpp)
  list(APPEND CORE_SOURCES ${POSIX}/common_posix.cpp ${POSIX}/platform_posix.cpp)
endif()

add_library(choosen_core SHARED ${CORE_SOURCES})
target_include_directories(choosen_core PRIVATE ${SRC})
if(MSVC)
  target_compile_options(choosen_core PRIVATE /utf-8 /EHsc /W3)
  target_compile_definitions(choosen_core PRIVATE UNICODE _UNICODE)
else()
  target_compile_options(choosen_core PRIVATE -Wall -Wno-unused-parameter)
  if(ANDROID)
    target_link_libraries(choosen_core log)
  endif()
endif()
