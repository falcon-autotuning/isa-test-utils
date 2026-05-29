#pragma once

#include "isa-test-utils/isa-test-utils-export.h"

/*
 * NOTE:
 * - All path_* fields are UTF-8 encoded filesystem paths.
 * - Memory ownership follows standard C library rules:
 *   dynamically allocated strings must be created via strdup / malloc
 *   and ownership is transferred to the caller, who must release
 *   the memory using standard free().
 * - Functions accepting `void *args` expect a pointer to a `UT_array`
 *   initialized with string pointers (`&ut_str_icd`).
 */

typedef struct {
  int exit_code;
  char *stdout_data; // heap-allocated string (free with free())
  char *stderr_data; // heap-allocated string (free with free())
} ProcessResult;

// For dependency injection
typedef struct {
  const char *instrument_server_path;
} RunConfig;

ISA_TEST_UTILS_EXPORT void run_set_config(const RunConfig *config);

ISA_TEST_UTILS_EXPORT ProcessResult run_executable(
    const char *executable_path, // path to executable
    void *args // Must be an initialized UT_array* containing char* arguments
);

ISA_TEST_UTILS_EXPORT ProcessResult run_iss(
    void *args // Must be an initialized UT_array* containing char* arguments
);

ISA_TEST_UTILS_EXPORT void start_server(void);

ISA_TEST_UTILS_EXPORT void
start_instrument(const char *config_path,
                 const char *plugin_path // nullable (NULL if unused)
);

ISA_TEST_UTILS_EXPORT char *instrument_status(const char *name);

ISA_TEST_UTILS_EXPORT void stop_instrument(const char *name);

ISA_TEST_UTILS_EXPORT void stop_server(void);

ISA_TEST_UTILS_EXPORT char *
perform_measurement_from_script(const char *script_contents,
                                const char *variables_json);
