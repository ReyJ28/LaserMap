cmake_minimum_required(VERSION 3.16)
project(LaserMap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(lasermap_core
  src/lasermap/trace.cpp
  src/lasermap/modes.cpp
  src/lasermap/ilda.cpp
  src/lasermap/wire.cpp
  src/lasermap/net.cpp
  src/lasermap/beyond_sink.cpp
  src/lasermap/schedule.cpp
  src/lasermap/react.cpp
  src/lasermap/safety.cpp)
target_include_directories(lasermap_core PUBLIC src)
if(MSVC)
  target_compile_options(lasermap_core PRIVATE /W4)
else()
  target_compile_options(lasermap_core PRIVATE -Wall -Wextra)
endif()

find_package(Threads REQUIRED)
target_link_libraries(lasermap_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(lasermap_core PUBLIC ws2_32)
endif()

add_executable(lasermap_trace_video tools/lasermap_trace_video.cpp)
target_link_libraries(lasermap_trace_video PRIVATE lasermap_core)

add_executable(lasermap_bridge tools/lasermap_bridge.cpp)
target_link_libraries(lasermap_bridge PRIVATE lasermap_core)

add_executable(lasermap_cli tools/lasermap_cli.cpp)
target_link_libraries(lasermap_cli PRIVATE lasermap_core)

add_executable(lasermap_demo tools/lasermap_demo.cpp)
target_link_libraries(lasermap_demo PRIVATE lasermap_core)

enable_testing()
add_executable(lasermap_tests tests/test_core.cpp)
target_link_libraries(lasermap_tests PRIVATE lasermap_core)
add_test(NAME core COMMAND lasermap_tests)

option(LASERMAP_BUILD_FFGL "Build the Resolume FFGL plugin (needs FFGL_SDK_DIR)" OFF)
if(LASERMAP_BUILD_FFGL)
  add_subdirectory(plugin/ffgl)
endif()
