cmake_minimum_required(VERSION 3.20)
project(
  sim900-instrument
  VERSION 1.0.0
  DESCRIPTION "The set of configurations and plugins for running instruments"
  HOMEPAGE_URL "https://github.com/falcon-autotuning/sim900-instrument"
  LANGUAGES C
)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Options
# ============================================================================
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_PLUGINS "Build instrument plugins" ON)
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(ENABLE_PCH
       "Enable target_precompile_headers to speed up builds (opt-in)" ON)

# ============================================================================
# Test Runtime Environment
#
# These are optional CMake cache variables used only to make hardware-backed
# tests more discoverable and easier to run through CTest.
#
# Tests may also read these directly from the shell environment at runtime.
#
# Required by SIM900 hardware tests:
#   SIM900_ADDRESS - VISA address for the SIM900, e.g. ASRL/dev/ttyUSB0::INSTR
#   SIM_CHANNEL    - SIM900 channel number, e.g. 1
#
# You can provide them either by exporting shell variables:
#   export SIM900_ADDRESS="ASRL/dev/ttyUSB0::INSTR"
#   export SIM_CHANNEL=1
#
# or by configuring CMake with:
#   -DTEST_SIM900_ADDRESS="ASRL/dev/ttyUSB0::INSTR"
#   -DTEST_SIM_CHANNEL=1
# ============================================================================
set(TEST_SIM900_ADDRESS ""
    CACHE STRING "Optional SIM900 VISA address for hardware-backed tests")
set(TEST_SIM_CHANNEL ""
    CACHE STRING "Optional SIM900 channel for hardware-backed tests")

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

find_package(instrument-script-server CONFIG REQUIRED)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/env.sh.in"
    "${CMAKE_BINARY_DIR}/env.sh"
    @ONLY
  )

  # Also generate a JSON file for other tools
  file(WRITE "${CMAKE_BINARY_DIR}/build-info.json"
    "{
      \"VCPKG_INSTALLED_DIR\": \"${VCPKG_INSTALLED_DIR}\",
      \"VCPKG_TRIPLET\": \"${VCPKG_TARGET_TRIPLET}\",
      \"CMAKE_BUILD_TYPE\": \"${CMAKE_BUILD_TYPE}\",
      \"PRESET_NAME\": \"${CMAKE_PRESET_NAME}\"
    }")

  message(STATUS "Generated env.sh and build-info.json in ${CMAKE_BINARY_DIR}")
endif()
# ============================================================================
# Installation Prefix Configuration
# ============================================================================
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(CMAKE_INSTALL_PREFIX "C:/falcon" CACHE PATH "Installation prefix" FORCE)
  else()
    set(CMAKE_INSTALL_PREFIX "/opt/falcon" CACHE PATH "Installation prefix" FORCE)
  endif()
endif()
# ============================================================================
# CPack General Settings
# ============================================================================
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_SOURCE_IGNORE_FILES
  build/
  ".*~$"
  vcpkg/
  vcpkg_installed/
)
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# ============================================================================
# Platform-Specific CPack Configuration
# ============================================================================
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_ARCHIVE_THREADS 0)
else()
  # Linux: TGZ format (extractable via curl | tar xz)
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_ARCHIVE_THREADS 0)
endif()

# ============================================================================
# Installation Directory Structure
# ============================================================================
set(INSTALL_BIN_DIR "bin")
set(INSTALL_LIB_DIR "lib")
set(INSTALL_INCLUDE_DIR "include")
set(INSTALL_SHARE_DIR "share")
set(INSTALL_DOC_DIR "doc")

# ============================================================================
# Install Targets
# ============================================================================
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${INSTALL_DOC_DIR}"
)

# ============================================================================
# Install vcpkg dependencies
# ============================================================================
if(WIN32)
  file(GLOB_RECURSE WIN_DLLS "${VCPKG_INSTALLED_DIR}/x64-win-llvm/bin/*.dll")
  install(
    PROGRAMS ${WIN_DLLS}
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  file(GLOB_RECURSE WIN_LIBS "${VCPKG_INSTALLED_DIR}/x64-win-llvm/lib/*.lib")
  install(
    FILES ${WIN_LIBS}
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  file(GLOB_RECURSE WIN_INCLUDES "${VCPKG_INSTALLED_DIR}/x64-win-llvm/include/*")
  install(
    FILES ${WIN_INCLUDES}
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  file(GLOB_RECURSE WIN_SHARE "${VCPKG_INSTALLED_DIR}/x64-win-llvm/share/*")
  install(
    FILES ${WIN_SHARE}
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
else()
  file(GLOB_RECURSE LINUX_BINS "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/bin/*")
  install(
    PROGRAMS ${LINUX_BINS}
    DESTINATION "${INSTALL_BIN_DIR}"
  )
  file(GLOB_RECURSE LINUX_LIBS_A "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.a")
  file(GLOB_RECURSE LINUX_LIBS_SO "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/lib/*.so*")
  install(
    FILES ${LINUX_LIBS_A} ${LINUX_LIBS_SO}
    DESTINATION "${INSTALL_LIB_DIR}"
  )
  file(GLOB_RECURSE LINUX_INCLUDES "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/include/*")
  install(
    FILES ${LINUX_INCLUDES}
    DESTINATION "${INSTALL_INCLUDE_DIR}"
  )
  file(GLOB_RECURSE LINUX_SHARE "${VCPKG_INSTALLED_DIR}/x64-linux-dynamic/share/*")
  install(
    FILES ${LINUX_SHARE}
    DESTINATION "${INSTALL_SHARE_DIR}"
  )
endif()

include(CPack)
