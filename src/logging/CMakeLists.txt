# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

# Logging library CMake configuration
cmake_minimum_required(VERSION 3.14)

# fmt is provided via FetchContent in parent CMakeLists.txt, so we don't need find_package

# Collect logging sources
set(MCP_LOGGING_SOURCES
  logger.cc
  logger_registry.cc
  log_sink.cc
  log_formatter.cc
)

# Create static library
add_library(gopher-mcp-logging-static STATIC ${MCP_LOGGING_SOURCES})
set_target_properties(gopher-mcp-logging-static PROPERTIES
  OUTPUT_NAME gopher-mcp-logging
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(gopher-mcp-logging-static PUBLIC
  $<BUILD_INTERFACE:${GOPHER_MCP_ROOT_DIR}/include>
  $<INSTALL_INTERFACE:include/gopher-mcp>
)

# Public logging headers use fmt's formatting API. Propagate its target's
# include paths and compile requirements instead of copying include directories.
target_link_libraries(gopher-mcp-logging-static
  PUBLIC Threads::Threads fmt::fmt
)

target_compile_features(gopher-mcp-logging-static PUBLIC cxx_std_17)

target_compile_definitions(gopher-mcp-logging-static PUBLIC
  $<$<CONFIG:Debug>:MCP_DEBUG_LOGGING>
)

# Create shared library
if(BUILD_SHARED_LIBS)
  add_library(gopher-mcp-logging SHARED ${MCP_LOGGING_SOURCES})

  if(WIN32)
    set_target_properties(gopher-mcp-logging PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON
      PREFIX ""
    )
  endif()

  target_include_directories(gopher-mcp-logging PUBLIC
    $<BUILD_INTERFACE:${GOPHER_MCP_ROOT_DIR}/include>
    $<INSTALL_INTERFACE:include/gopher-mcp>
  )
  
  target_link_libraries(gopher-mcp-logging PUBLIC
    fmt::fmt
    Threads::Threads
  )
  
  target_compile_features(gopher-mcp-logging PUBLIC cxx_std_17)
  
  target_compile_definitions(gopher-mcp-logging PUBLIC
    $<$<CONFIG:Debug>:MCP_DEBUG_LOGGING>
  )
else()
  add_library(gopher-mcp-logging ALIAS gopher-mcp-logging-static)
endif()

# Install rules
if(GOPHER_MCP_INSTALL AND NOT GOPHER_MCP_IS_SUBMODULE)
  install(TARGETS gopher-mcp-logging-static
    EXPORT gopher-mcp-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  
  if(BUILD_SHARED_LIBS)
    install(TARGETS gopher-mcp-logging
      EXPORT gopher-mcp-targets
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib
    )
  endif()
  
  install(DIRECTORY ${GOPHER_MCP_ROOT_DIR}/include/mcp/logging
    DESTINATION include/mcp
    FILES_MATCHING PATTERN "*.h"
  )
endif()
