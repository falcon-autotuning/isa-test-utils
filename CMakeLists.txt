cmake_minimum_required(VERSION 3.20)
project(
  isa-test-utils
  VERSION 1.0.0
  DESCRIPTION "The set of configurations and plugins for running instruments"
  HOMEPAGE_URL "https://github.com/falcon-autotuning/isa-test-utils"
  LANGUAGES C
)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Options
# ============================================================================
option(BUILD_TESTS "Build tests" OFF)
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(ENABLE_PCH
       "Enable target_precompile_headers to speed up builds (opt-in)" ON)

# Windows-specific definitions
if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Try to auto-detect a compiler launcher (prefer sccache if present)
if(USE_CCACHE)
  find_program(SCCACHE_PROGRAM sccache)
  if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
  else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    else()
      message(
        STATUS "No ccache/sccache found (fine, builds will just be normal).")
    endif()
  endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)
add_library(isa-test-utils SHARED
    src/run.c
    src/setup.c
)
target_include_directories(isa-test-utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${INSTALL_INCLUDE_DIR}>
)
target_link_libraries(isa-test-utils PRIVATE PkgConfig::GLIB2)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
include(CMakePackageConfigHelpers)
include(GNUInstallDirs) # Use standard install directories

# Install headers into a subdirectory to avoid name collisions
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  FILES cmake/embed_bundle.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/isa-test-utils
)

# Create an export set for all targets
install(TARGETS isa-test-utils
  EXPORT isa-test-utils-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Generate and install the `isa-test-utils-targets.cmake` file
install(EXPORT isa-test-utils-targets
  FILE isa-test-utils-targets.cmake
  NAMESPACE isa-test-utils::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/isa-test-utils
)

# Generate the version file
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
# Workaround for Ninja timestamp issue on Windows
if(CMAKE_GENERATOR STREQUAL "Ninja" AND WIN32)
  file(TOUCH "${CMAKE_BINARY_DIR}/build.ninja")
endif()
