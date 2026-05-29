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

configure_file(
  ${CMAKE_SOURCE_DIR}/tests/mock-template-expander.sh
  ${CMAKE_BINARY_DIR}/mock-template-expander.sh
  COPYONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/tests/mock-server.sh
  ${CMAKE_BINARY_DIR}/mock-server.sh
  COPYONLY
)

file(CHMOD
  ${CMAKE_BINARY_DIR}/mock-template-expander.sh
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

file(CHMOD
  ${CMAKE_BINARY_DIR}/mock-server.sh
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

target_compile_definitions(unit_test_run PRIVATE
  MOCK_SERVER_PATH="${CMAKE_BINARY_DIR}/mock-server.sh"
)

target_compile_definitions(unit_test_setup PRIVATE
  MOCK_EXPANDER_PATH="${CMAKE_BINARY_DIR}/mock-template-expander.sh"
)

function(copy_runtime_dll target)
  if (WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:instrument-data>
        $<TARGET_FILE_DIR:${target}>
    )
  endif()
endfunction()

copy_runtime_dll(unit_test_run)
copy_runtime_dll(unit_test_setup)
