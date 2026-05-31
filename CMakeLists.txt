cmake_minimum_required(VERSION 3.20)

project(
  isa-test-utils
  VERSION 1.2.1
  DESCRIPTION "Utilities for hermetic ISA/config/plugin test environments"
  HOMEPAGE_URL "https://github.com/falcon-autotuning/isa-test-utils"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
option(BUILD_TESTS "Build tests" OFF)
option(USE_CCACHE "Enable ccache or sccache if available" ON)

# ============================================================
# Windows Definitions
# ============================================================

if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(USE_CCACHE)
  find_program(SCCACHE_PROGRAM sccache)
  if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
  else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()
  endif()
endif()

find_path(UTHASH_INCLUDE_DIRS "utarray.h")
find_package(cJSON CONFIG REQUIRED)
add_library(isa-test-utils SHARED
  src/run.c
  src/path.c
  src/env.c
  src/prepare_directories.c
)

target_link_libraries(isa-test-utils
  PRIVATE
    cjson
)
include(GenerateExportHeader)
set(GENERATED_INCLUDE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/include/isa-test-utils)
file(MAKE_DIRECTORY ${GENERATED_INCLUDE_DIR})
generate_export_header(isa-test-utils
  EXPORT_FILE_NAME
    ${GENERATED_INCLUDE_DIR}/isa-test-utils-export.h
)
target_include_directories(isa-test-utils
  PRIVATE
    ${UTHASH_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ============================================================
# Install
# ============================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES
  ${GENERATED_INCLUDE_DIR}/isa-test-utils-export.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/isa-test-utils
)
install(FILES cmake/embed_bundle.cmake
  DESTINATION ${CMAKE_INSTALL_DATADIR}/isa-test-utils
)
install(TARGETS isa-test-utils
  EXPORT isa-test-utils-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT isa-test-utils-targets
  FILE isa-test-utils-targets.cmake
  NAMESPACE isa-test-utils::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/isa-test-utils
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/isa-test-utils-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)
configure_package_config_file(
  cmake/isa-test-utils-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/isa-test-utils-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/isa-test-utils
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/isa-test-utils-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/isa-test-utils-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/isa-test-utils
)

if(CMAKE_GENERATOR STREQUAL "Ninja" AND WIN32)
  file(TOUCH "${CMAKE_BINARY_DIR}/build.ninja")
endif()
