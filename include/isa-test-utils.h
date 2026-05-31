#pragma once
#include "isa-test-utils/isa-test-utils-export.h"
#include <instrument-data.h>

/**
 * @file isa-test-utils.h
 * @brief Core API for instrument testing utilities.
 *
 * This library provides helpers for:
 * - Constructing temporary test environments
 * - Running instruments and services
 * - Performing measurements and collecting structured results
 * - Managing buffers and environment data
 *
 * @note
 * - All paths are UTF-8 encoded.
 * - Memory ownership follows standard C conventions:
 *   all heap-allocated memory returned to the caller must be freed
 *   using the appropriate API function or `free()`.
 */

/* ================================
   Path Utilities
   ================================ */

/**
 * @brief Opaque platform-independent path buffer.
 */
typedef struct Path Path;

/**
 * @brief Clone an existing Path.
 *
 * @param other Path to clone
 * @return Newly allocated Path instance. Must be freed with path_free().
 */
ISA_TEST_UTILS_EXPORT Path *path_clone(const Path *other);

/**
 * @brief Free a Path instance.
 *
 * @param buf Path to free (NULL-safe)
 */
ISA_TEST_UTILS_EXPORT void path_free(Path *buf);

/**
 * @brief Append a path component.
 *
 * @param buf Path to modify
 * @param element Path segment to append
 */
ISA_TEST_UTILS_EXPORT void path_push(Path *buf, const char *element);

/**
 * @brief Remove the last path component.
 *
 * @param buf Path to modify
 */
ISA_TEST_UTILS_EXPORT void path_pop(Path *buf);

/* ================================
   Environment Setup
   ================================ */

/**
 * @brief Directory layout for a prepared test environment.
 */
typedef struct {
  Path *root_dir;   /**< Root temporary environment directory */
  Path *isa_dir;    /**< Extracted ISA files directory */
  Path *config_dir; /**< Generated configuration directory */
  Path *plugin_dir; /**< Plugin directory (NULL if unused) */
} EnvLocations;

/**
 * @brief Clean up a previously created environment.
 *
 * Removes generated directories and frees all associated Path objects.
 *
 * @param env Environment to clean (NULL-safe)
 */
ISA_TEST_UTILS_EXPORT void cleanup_environment(EnvLocations *env);

/**
 * @brief Prepare a test environment using embedded resources.
 *
 * @details
 * This function is **generated at build time** via the CMake
 * `isa_test_utils_embed_bundle` utility. It is declared in a generated header
 * (e.g. `embedded_bundle.h`) and not directly in this API.
 *
 * The generated function has the form:
 *
 * @code
 * EnvLocations prepare_environment(const char *first_key, ...)
 * @endcode
 *
 * It:
 * - Extracts embedded resources (ISA/config/plugin files)
 * - Applies template substitutions using key/value pairs
 * - Returns a fully prepared environment
 *
 * @param first_key First replacement key, followed by:
 *
 *        key1, value1, key2, value2, ..., NULL
 *
 *        - Must be NULL-terminated
 *        - Must appear in key/value pairs
 *
 * @return EnvLocations structure
 *
 * @note
 * - The function internally manages all intermediate allocations.
 * - Only the returned EnvLocations requires cleanup via cleanup_environment().
 *
 * @warning
 * Improper argument pairing or missing NULL terminator results in undefined
 * behavior.
 */

/* ================================
   Process / Runtime Control
   ================================ */

/**
 * @brief Result returned from executing a process (e.g. ISS).
 */
typedef struct {
  int exit_code;     /**< Process exit code */
  char *stdout_data; /**< Captured stdout (heap, free with free()) */
  char *stderr_data; /**< Captured stderr (heap, free with free()) */
} ProcessResult;

/**
 * @brief Run the instruction set simulator (ISS).
 *
 * @param args UT_array* of `char*` arguments
 * @return ProcessResult containing execution outputs
 */
ISA_TEST_UTILS_EXPORT ProcessResult run_iss(void *args);

/**
 * @brief Start the test server.
 */
ISA_TEST_UTILS_EXPORT void start_server(void);

/**
 * @brief Start an instrument instance.
 *
 * @param config_path Path to config file
 * @param plugin_path Optional plugin path (NULL if unused)
 */
ISA_TEST_UTILS_EXPORT void start_instrument(const Path *config_path,
                                            const Path *plugin_path);

/**
 * @brief Query instrument status.
 *
 * @param name Instrument identifier
 * @return Heap-allocated string (free with free())
 */
ISA_TEST_UTILS_EXPORT char *instrument_status(const char *name);

/**
 * @brief Stop a running instrument.
 *
 * @param name Instrument identifier
 */
ISA_TEST_UTILS_EXPORT void stop_instrument(const char *name);

/**
 * @brief Stop the test server.
 */
ISA_TEST_UTILS_EXPORT void stop_server(void);

/* ================================
   Measurement Results
   ================================ */

/**
 * @brief Supported return value types.
 */
typedef enum {
  VAL_TYPE_VOID = 0,
  VAL_TYPE_DOUBLE,
  VAL_TYPE_INT64,
  VAL_TYPE_STRING,
  VAL_TYPE_BOOL,
  VAL_TYPE_BUFFER
} ValueType;

/**
 * @brief Metadata describing a buffer return value.
 */
typedef struct {
  char *buffer_id;      /**< Buffer identifier (heap) */
  size_t element_count; /**< Number of elements */
  char *data_type;      /**< Data format description */
} BufferReturn;

/**
 * @brief Polymorphic return value container.
 */
typedef union {
  double d_val;
  int64_t i_val;
  char *s_val;
  bool b_val;
  BufferReturn buf_val;
} ReturnValue;

/**
 * @brief Result of a single step in a script.
 */
typedef struct {
  int index;
  char *instrument;
  char *verb;
  char *params_json;
  uint64_t executed_at_ms;
  ValueType return_type;
  ReturnValue return_value;
} StepResult;

/**
 * @brief Top-level result returned from a measurement.
 */
typedef struct {
  char *status; /**< "success" or "failure" */
  char *script_name;
  int step_count;
  StepResult *steps;
} Result;

/* ================================
   Input Map
   ================================ */

/**
 * @brief Opaque key-value container for measurement inputs.
 */
typedef struct Map Map;

/**
 * @brief Allocate a new Map.
 */
ISA_TEST_UTILS_EXPORT Map *map_new(void);

/**
 * @brief Free a Map.
 */
ISA_TEST_UTILS_EXPORT void map_free(Map *map);

/**
 * @name Map Value Insertion
 *
 * Functions for inserting key/value pairs into a Map.
 *
 * All functions:
 * - Copy the provided key string internally
 * - Store values in an internal representation owned by the Map
 *
 * Ownership rules:
 * - The caller retains ownership of all input values
 * - The Map makes internal copies of all data and manages its own memory
 *
 * Supported value types:
 *
 * | Function                  | Value type                        |
 * |--------------------------|-----------------------------------|
 * | map_add_float            | float                             |
 * | map_add_int              | int                               |
 * | map_add_string           | const char* (copied internally)   |
 * | map_add_bool             | bool                              |
 * | map_add_array_string     | UT_array of char*                 |
 * | map_add_array_number     | UT_array of float                 |
 * | map_add_array_bool       | UT_array of bool                  |
 *
 * @note
 * Passing NULL for `map`, `key`, or `val` results in a no-op.
 *
 * @warning
 * Keys must be non-NULL strings. Passing invalid pointers results in undefined
 * behavior.
 *
 * @{
 */

/**
 * @brief Add a floating-point value to the map.
 */
ISA_TEST_UTILS_EXPORT void map_add_float(Map *map, const char *key, float val);

/**
 * @brief Add an integer value to the map.
 */
ISA_TEST_UTILS_EXPORT void map_add_int(Map *map, const char *key, int val);

/**
 * @brief Add a string value to the map.
 *
 * @param val String is duplicated internally; caller retains ownership.
 */
ISA_TEST_UTILS_EXPORT void map_add_string(Map *map, const char *key,
                                          const char *val);

/**
 * @brief Add a boolean value to the map.
 */
ISA_TEST_UTILS_EXPORT void map_add_bool(Map *map, const char *key, bool val);
/**
 * @brief Add an array of strings to the map.
 *
 * @param map Target map
 * @param key Key name (copied internally)
 * @param val Pointer to a UT_array containing `char*` elements
 *
 * @details
 * The input must be a pointer to a UT_array configured with string elements
 * (`char*`). Each string is duplicated (`strdup`) and stored in an internal
 * array owned by the map.
 *
 * The input array itself is not modified and remains owned by the caller.
 *
 * Example:
 * @code
 * UT_array *arr;
 * utarray_new(arr, &ut_str_icd);
 *
 * char *s1 = "a";
 * char *s2 = "b";
 * utarray_push_back(arr, &s1);
 * utarray_push_back(arr, &s2);
 *
 * map_add_array_string(map, "my_key", arr);
 * @endcode
 *
 * @note
 * The map creates a deep copy of all elements. The caller retains ownership
 * of the input array and its contents.
 *
 * @warning
 * Passing a UT_array with an incompatible element type results in undefined
 * behavior.
 */
ISA_TEST_UTILS_EXPORT void map_add_array_string(Map *map, const char *key,
                                                void *val);
/**
 * @brief Add an array of floating-point values to the map.
 *
 * @param map Target map
 * @param key Key name (copied internally)
 * @param val Pointer to a UT_array containing `float` elements
 *
 * @details
 * The input must be a pointer to a UT_array configured with `float` elements.
 * Values are copied into an internal array owned by the map.
 *
 * The input array remains owned by the caller.
 *
 * Example:
 * @code
 * UT_array *arr;
 * utarray_new(arr, &native_float_icd);
 *
 * float v = 1.23f;
 * utarray_push_back(arr, &v);
 *
 * map_add_array_number(map, "values", arr);
 * @endcode
 *
 * @note
 * This function performs a shallow copy of element values (primitive types).
 *
 * @warning
 * Passing a UT_array with a mismatched element type results in undefined
 * behavior.
 */
ISA_TEST_UTILS_EXPORT void map_add_array_number(Map *map, const char *key,
                                                void *val);
/**
 * @brief Add an array of boolean values to the map.
 *
 * @param map Target map
 * @param key Key name (copied internally)
 * @param val Pointer to a UT_array containing `bool` elements
 *
 * @details
 * The input must be a pointer to a UT_array configured with `bool` elements.
 * Values are copied into an internal array owned by the map.
 *
 * The input array remains owned by the caller.
 *
 * Example:
 * @code
 * UT_array *arr;
 * utarray_new(arr, &native_bool_icd);
 *
 * bool flag = true;
 * utarray_push_back(arr, &flag);
 *
 * map_add_array_bool(map, "flags", arr);
 * @endcode
 *
 * @warning
 * Passing a UT_array with an incorrect element type results in undefined
 * behavior.
 */
ISA_TEST_UTILS_EXPORT void map_add_array_bool(Map *map, const char *key,
                                              void *value);
/** @} */

/**
 * @brief Execute a measurement script.
 *
 * @param script_contents Script source
 * @param variables Input variables
 * @return Result structure (must call free_result())
 */
ISA_TEST_UTILS_EXPORT const Result *
perform_measurement(const char *script_contents, const Map *variables);

/**
 * @brief Free a Result structure.
 */
ISA_TEST_UTILS_EXPORT void free_result(const Result *res);

/* ================================
   Buffer API
   ================================ */

/**
 * @brief Data buffer representation.
 */
typedef struct {
  char *buffer_id;
  int element_count;
  ArrayType data_type;
  void *data;
} buffer;

/**
 * @brief Read a buffer by ID.
 */
ISA_TEST_UTILS_EXPORT const buffer *read_buffer(const char *buffer_id);

/**
 * @brief Free a buffer returned by read_buffer().
 */
ISA_TEST_UTILS_EXPORT void free_buffer(const buffer *buf);

/**
 * @brief Release a buffer resource from the runtime.
 */
ISA_TEST_UTILS_EXPORT void release_buffer(const char *buffer_id);

/* ================================
   Environment Utilities
   ================================ */

/**
 * @brief Retrieve required environment variable as string.
 *
 * @param name Environment variable name
 * @return Heap-allocated string (free with free())
 */
ISA_TEST_UTILS_EXPORT char *get_required_env_string(const char *name);

/**
 * @brief Retrieve required environment variable as integer.
 *
 * @param name Environment variable name
 * @return Parsed integer value
 */
ISA_TEST_UTILS_EXPORT int get_required_env_int(const char *name);

/**
 * @brief Format key/value pairs into YAML string.
 *
 * @param format printf-style format string
 * @param ... Values
 * @return Heap-allocated YAML string
 */
ISA_TEST_UTILS_EXPORT char *flatten_yaml(const char *format, ...);

/**
 * @brief Generate indexed name string.
 *
 * @param name Base name
 * @param index Index value
 * @return Heap-allocated formatted string
 */
ISA_TEST_UTILS_EXPORT char *name_with_index(const char *name, int index);
