cmake_minimum_required(VERSION 3.25)

project("btop-agent-adapter" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  find_package(nlohmann_json REQUIRED)
endif()

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

if(WIN32)
  set(BTOP4WIN_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../btop4win" CACHE PATH "Path to the upstream btop4win source tree")
  if(NOT EXISTS "${BTOP4WIN_SOURCE_DIR}/btop4win.vcxproj")
    message(FATAL_ERROR "Expected btop4win source tree at ${BTOP4WIN_SOURCE_DIR}")
  endif()

  add_library(btop-tui-obj OBJECT
    "${BTOP4WIN_SOURCE_DIR}/src/btop.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_collect.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_config.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_draw.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_input.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_menu.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_theme.cpp"
    "${BTOP4WIN_SOURCE_DIR}/src/btop_tools.cpp"
  )
  set_target_properties(btop-tui-obj PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  target_compile_definitions(btop-tui-obj PRIVATE
    main=btop4win_main
    UNICODE
    _UNICODE
  )
  target_include_directories(btop-tui-obj PRIVATE
    "${BTOP4WIN_SOURCE_DIR}/src"
    "${BTOP4WIN_SOURCE_DIR}/include"
  )
  target_compile_options(btop-tui-obj PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd4715 /WX->
  )
else()
  set(BTOP_UPSTREAM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../btop" CACHE PATH "Path to the upstream btop source tree")
  set(BTOP_UPSTREAM_BINARY_DIR "${CMAKE_BINARY_DIR}/btop-upstream")

  if(NOT EXISTS "${BTOP_UPSTREAM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Expected btop source tree at ${BTOP_UPSTREAM_SOURCE_DIR}")
  endif()

  # The host-side agent now owns distributed monitoring. If the local btop tree still
  # contains legacy distributed targets, keep them disabled to avoid target collisions.
  set(BTOP_DISTRIBUTED OFF CACHE BOOL "Disable in-tree distributed monitoring targets" FORCE)

  add_subdirectory("${BTOP_UPSTREAM_SOURCE_DIR}" "${BTOP_UPSTREAM_BINARY_DIR}")
  if(APPLE)
    target_compile_options(libbtop PRIVATE
      $<$<CXX_COMPILER_ID:Clang>:-Wno-invalid-constexpr>
    )
  endif()
endif()

add_executable(btop-agent ${BTOP_AGENT_SOURCES})
set_target_properties(btop-agent PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

target_compile_definitions(btop-agent PRIVATE DISTRIBUTED_MONITORING)
target_include_directories(btop-agent PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")

if(WIN32)
  target_compile_definitions(btop-agent PRIVATE
    BTOP_AGENT_TUI_BACKEND_BTOP4WIN
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  target_include_directories(btop-agent PRIVATE
    "${BTOP4WIN_SOURCE_DIR}/src"
    "${BTOP4WIN_SOURCE_DIR}/include"
  )
  target_sources(btop-agent PRIVATE $<TARGET_OBJECTS:btop-tui-obj>)
  target_link_libraries(btop-agent PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    psapi
    iphlpapi
    ws2_32
    wbemuuid
    PowrProf
    pdh
    ole32
    oleaut32
    advapi32
    comsuppw
  )
else()
  target_compile_definitions(btop-agent PRIVATE BTOP_AGENT_TUI_BACKEND_BTOP)
  target_include_directories(btop-agent PRIVATE
    "${BTOP_UPSTREAM_SOURCE_DIR}/src"
    "${BTOP_UPSTREAM_SOURCE_DIR}/include"
  )
  target_link_libraries(btop-agent PRIVATE
    CURL::libcurl
    libbtop
    nlohmann_json::nlohmann_json
  )
endif()

install(TARGETS btop-agent RUNTIME)
