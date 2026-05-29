cmake_minimum_required(VERSION 3.20)
find_package(cmocka CONFIG REQUIRED)

# =========================================
# Run tests
# =========================================
add_executable(unit_test_run
    test_run.c
)

target_link_libraries(unit_test_run
    PRIVATE
        isa-test-utils
        cmocka::cmocka
)

add_test(NAME unit_test_run COMMAND unit_test_run)

# =========================================
# Setup tests
# =========================================
add_executable(unit_test_setup
    test_setup.c
)

target_link_libraries(unit_test_setup
    PRIVATE
        isa-test-utils
        cmocka::cmocka
)

add_test(NAME unit_test_setup COMMAND unit_test_setup)

# =========================================
# Deploy Mock Scripts Locally (Next to Test Executables)
# =========================================
configure_file(
  ${CMAKE_SOURCE_DIR}/tests/mock-template-expander.sh
  ${CMAKE_CURRENT_BINARY_DIR}/mock-template-expander.sh
  COPYONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/tests/mock-server.sh
  ${CMAKE_CURRENT_BINARY_DIR}/mock-server.sh
  COPYONLY
)

file(CHMOD
  ${CMAKE_CURRENT_BINARY_DIR}/mock-template-expander.sh
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

file(CHMOD
  ${CMAKE_CURRENT_BINARY_DIR}/mock-server.sh
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# ============================================================
# Force strict absolute paths including full drive letter roots
# ============================================================
get_filename_component(ABS_MOCK_SERVER_PATH "${CMAKE_CURRENT_BINARY_DIR}/mock-server.sh" ABSOLUTE)
get_filename_component(ABS_MOCK_EXPANDER_PATH "${CMAKE_CURRENT_BINARY_DIR}/mock-template-expander.sh" ABSOLUTE)

# Force CMake to format the absolute paths with forward slashes (e.g., C:/path/to/mock-server.sh)
file(TO_CMAKE_PATH "${ABS_MOCK_SERVER_PATH}" POSIX_MOCK_SERVER_PATH)
file(TO_CMAKE_PATH "${ABS_MOCK_EXPANDER_PATH}" POSIX_MOCK_EXPANDER_PATH)

# Bake the clean absolute forward-slash strings straight into your C macros
target_compile_definitions(unit_test_run PRIVATE
  MOCK_SERVER_PATH="${POSIX_MOCK_SERVER_PATH}"
)

target_compile_definitions(unit_test_setup PRIVATE
  MOCK_EXPANDER_PATH="${POSIX_MOCK_EXPANDER_PATH}"
)

# Force the working directory of the test runner to be the target folder
set_tests_properties(unit_test_run unit_test_setup PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# ============================================================
# DLL Copier for Windows (Using correct vcpkg package target names)
# ============================================================
function(copy_runtime_dll target)
  if (WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:isa-test-utils>
        $<TARGET_FILE_DIR:${target}>
    )
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:cjson> 
        $<TARGET_FILE_DIR:${target}>
    )
  endif()
endfunction()

copy_runtime_dll(unit_test_run)
copy_runtime_dll(unit_test_setup)
