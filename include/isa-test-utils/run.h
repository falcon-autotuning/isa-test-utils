#pragma once

#include "isa-test-utils/isa-test-utils-export.h"
#include <instrument-data.h>

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

// --- Measurement Types & APIs ---
typedef enum {
  VAL_TYPE_VOID = 0,
  VAL_TYPE_DOUBLE,
  VAL_TYPE_INT64,
  VAL_TYPE_STRING,
  VAL_TYPE_BOOL,
  VAL_TYPE_BUFFER
} ValueType;

// Structured tracking block for buffer return variant metadata payloads
typedef struct {
  char *buffer_id;      // Heap-allocated string
  size_t element_count; // Number of items in target shared memory space
  char *data_type;      // Allocation format description (e.g. "float32")
} BufferReturn;

// Poly-morphic storage payload union tracking custom variant data mappings
typedef union {
  double d_val;
  int64_t i_val;
  char *s_val;
  bool b_val;
  BufferReturn buf_val;
} ReturnValue;

// Native layout representing a single step execution within the script array
typedef struct {
  int index;
  char *instrument;  // Heap-allocated string
  char *verb;        // Heap-allocated string
  char *params_json; // Raw serialized snapshot parameters mapping
  uint64_t executed_at_ms;
  ValueType return_type;
  ReturnValue return_value;
} ScriptStepResult;

// Top-level structure representation returned directly to your test units
typedef struct {
  char *status;      // Heap-allocated string ("success" / "failure")
  char *script_name; // Heap-allocated string
  int step_count;
  ScriptStepResult *steps; // Continuous array allocated dynamically via malloc
} MeasurementResult;

ISA_TEST_UTILS_EXPORT const MeasurementResult *
perform_measurement(const char *script_contents, const char *variables_json);
ISA_TEST_UTILS_EXPORT void
free_measurement_result(const MeasurementResult *res);

// --- Buffer Types & APIs ---
typedef struct {
  char *buffer_id; // heap-allocated string (free with free())
  int element_count;
  ArrayType data_type;
  void *data;
} buffer;
ISA_TEST_UTILS_EXPORT const buffer *read_buffer(const char *buffer_id);

ISA_TEST_UTILS_EXPORT void free_buffer(const buffer *buf);

ISA_TEST_UTILS_EXPORT void release_buffer(const char *buffer_id);
