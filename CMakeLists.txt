cmake_minimum_required(VERSION 3.14)
project(RideSharingSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# Core library: models, graph/Dijkstra, fares, ETA, storage, service layer (no Crow dependency)
add_library(rs_core src/Graph.cpp src/Storage.cpp src/RideSystem.cpp)
target_include_directories(rs_core PUBLIC include)
target_link_libraries(rs_core PUBLIC Threads::Threads)
target_compile_options(rs_core PRIVATE -Wall -Wextra)

# Tests
enable_testing()
add_executable(rs_tests tests/test_core.cpp)
target_link_libraries(rs_tests PRIVATE rs_core)
add_test(NAME core COMMAND rs_tests)

# REST server: needs third_party/crow_all.h (single header) and standalone Asio (libasio-dev)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/crow_all.h")
  add_executable(ride_server src/main.cpp)
  target_include_directories(ride_server PRIVATE third_party)
  target_link_libraries(ride_server PRIVATE rs_core)
else()
  message(WARNING "third_party/crow_all.h not found - ride_server NOT built. See README (Setup).")
endif()
