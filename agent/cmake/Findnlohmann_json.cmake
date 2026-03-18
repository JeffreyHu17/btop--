# Find nlohmann/json library
# This module defines:
#  nlohmann_json_FOUND - True if nlohmann/json is found
#  nlohmann_json::nlohmann_json - Imported interface target

find_path(nlohmann_json_INCLUDE_DIR
  NAMES nlohmann/json.hpp
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    ${CMAKE_PREFIX_PATH}/include
)

if(nlohmann_json_INCLUDE_DIR)
  set(nlohmann_json_FOUND TRUE)

  if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_INCLUDE_DIR}"
    )
  endif()
else()
  set(nlohmann_json_FOUND FALSE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(nlohmann_json REQUIRED_VARS nlohmann_json_INCLUDE_DIR)

mark_as_advanced(nlohmann_json_INCLUDE_DIR)
