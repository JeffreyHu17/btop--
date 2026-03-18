cmake_minimum_required(VERSION 3.25)

project("btop-agent-adapter" LANGUAGES CXX)

set(BTOP_UPSTREAM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../btop" CACHE PATH "Path to the upstream btop source tree")
set(BTOP_UPSTREAM_BINARY_DIR "${CMAKE_BINARY_DIR}/btop-upstream")

if(NOT EXISTS "${BTOP_UPSTREAM_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Expected btop source tree at ${BTOP_UPSTREAM_SOURCE_DIR}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# The host-side agent now owns distributed monitoring. If the local btop tree still
# contains legacy distributed targets, keep them disabled to avoid target collisions.
set(BTOP_DISTRIBUTED OFF CACHE BOOL "Disable in-tree distributed monitoring targets" FORCE)

add_subdirectory("${BTOP_UPSTREAM_SOURCE_DIR}" "${BTOP_UPSTREAM_BINARY_DIR}")

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

set(BTOP_AGENT_SOURCES
  src/distributed/client/daemon_logger.cpp
  src/distributed/client/daemon_manager.cpp
  src/distributed/client/distributed_config.cpp
  src/distributed/client/main.cpp
  src/distributed/client/metrics_collector.cpp
  src/distributed/client/network_client.cpp
  src/distributed/common/auth_interface.cpp
  src/distributed/common/config_parser.cpp
  src/distributed/common/metrics_data.cpp
)

add_executable(btop-agent ${BTOP_AGENT_SOURCES})
set_target_properties(btop-agent PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

target_compile_definitions(btop-agent PRIVATE DISTRIBUTED_MONITORING)
target_include_directories(btop-agent PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/src"
  "${BTOP_UPSTREAM_SOURCE_DIR}/src"
  "${BTOP_UPSTREAM_SOURCE_DIR}/include"
)

target_link_libraries(btop-agent PRIVATE
  CURL::libcurl
  libbtop
  nlohmann_json::nlohmann_json
)

install(TARGETS btop-agent RUNTIME)
