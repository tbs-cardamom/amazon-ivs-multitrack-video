cmake_minimum_required(VERSION 3.28)

project(c-sample)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  LIBAV
  REQUIRED
  IMPORTED_TARGET
  libavdevice
  libavfilter
  libavformat
  libavcodec
  libswresample
  libswscale
  libavutil)

option(USE_GETCLIENTCONFIGURATION "Enable GetClientConfiguration" ON)

if(USE_GETCLIENTCONFIGURATION)
  find_package(CURL REQUIRED HTTPS)
  find_package(nlohmann_json 3.11.0 REQUIRED)
  set(GCC_SOURCES src/getclientconfiguration.cpp src/getclientconfiguration.h)
  set(GCC_LIBRARIES CURL::libcurl nlohmann_json::nlohmann_json)
  add_compile_definitions(ENABLE_GETCLIENTCONFIGURATION)
endif()

add_executable(c-sample src/main.c src/options.c src/options.h src/tracks.h
                        ${GCC_SOURCES})

set_property(TARGET c-sample PROPERTY C_STANDARD 11)
if(USE_GETCLIENTCONFIGURATION)
  set_property(TARGET c-sample PROPERTY CXX_STANDARD 17)
endif()

target_link_libraries(c-sample PkgConfig::LIBAV ${GCC_LIBRARIES})

if(MSVC)
  target_compile_options(c-sample PRIVATE /W4 /WX)
else()
  target_compile_options(c-sample PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
