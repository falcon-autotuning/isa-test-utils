#pragma once

#include "isa-test-utils/isa-test-utils-export.h"
#include "isa-test-utils.h"

ISA_TEST_UTILS_EXPORT ProcessResult run_executable(
    const char *executable_path, // path to executable
    void *args // Must be an initialized UT_array* containing char* arguments
);

// For dependency injection
typedef struct {
  const char *template_expander_path;
} SetupConfig;

ISA_TEST_UTILS_EXPORT void setup_set_config(const SetupConfig *config);

// For dependency injection
typedef struct {
  const char *instrument_server_path;
} RunConfig;

ISA_TEST_UTILS_EXPORT void run_set_config(const RunConfig *config);
