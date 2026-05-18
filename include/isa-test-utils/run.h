#pragma once

#include "isa-test-utils/isa-test-utils-export.h"
#include <glib.h>

/*
 * NOTE:
 * - All path_* fields are UTF-8 encoded filesystem paths.
 * - Memory ownership follows GLib conventions:
 *   strings must be allocated with g_strdup / freed with g_free.
 */

typedef struct {
  int exit_code;
  char *stdout_data; // heap-allocated string
  char *stderr_data; // heap-allocated string
} ProcessResult;

// For dependancy injection
typedef struct {
  const char *instrument_server_path;
} RunConfig;

ISA_TEST_UTILS_EXPORT void run_set_config(const RunConfig *config);
// end of dependancy injection

ISA_TEST_UTILS_EXPORT ProcessResult run_executable(
    const char *executable_path, // path to executable
    GPtrArray *args              // array of char* (arguments)
);

ISA_TEST_UTILS_EXPORT ProcessResult run_iss(GPtrArray *args // array of char*
);

ISA_TEST_UTILS_EXPORT void start_server(void);

ISA_TEST_UTILS_EXPORT void
start_instrument(const char *config_path, // path
                 const char *plugin_path  // nullable (NULL if unused)
);

ISA_TEST_UTILS_EXPORT char *instrument_status(const char *name // string
);

ISA_TEST_UTILS_EXPORT void stop_instrument(const char *name);

ISA_TEST_UTILS_EXPORT void stop_server(void);

ISA_TEST_UTILS_EXPORT char *
perform_measurement_from_script(const char *script_contents,
                                const char *variables_json);
