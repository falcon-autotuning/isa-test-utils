#include <cjson/cJSON.h>
#include <cmocka.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exec.h"
#include "internal/path.h"
#include "isa-test-utils.h"
#include "internal/measurement-helpers.h"
#include "utarray.h"

// Define cross-platform environment variable utility macros
#ifdef _WIN32
#define setenv_cross(name, val) _putenv_s(name, val)
#define unsetenv_cross(name) _putenv_s(name, "")
#else
#define setenv_cross(name, val) setenv(name, val, 1)
#define unsetenv_cross(name) unsetenv(name)
#endif

/* ================================
   Setup
================================ */

static void setup_mock(void) {
  run_set_config(&(RunConfig){.instrument_server_path = MOCK_SERVER_PATH});
}

/* ================================
   run_executable / run_iss
================================ */

static void test_run_executable_success(void **state) {
  (void)state;
  setup_mock();

  UT_array *args;
  utarray_new(args, &ut_str_icd);
  const char *arg1 = "measure";
  utarray_push_back(args, &arg1);

  // void *args casts seamlessly matching your public API configuration
  ProcessResult r = run_iss(args);
  utarray_free(args);

  assert_int_equal(r.exit_code, 0);
  assert_non_null((void *)r.stdout_data);
  assert_non_null((void *)r.stderr_data);

  free(r.stdout_data);
  free(r.stderr_data);
}

/* ================================
   Failure path
================================ */

static void test_run_iss_failure_flag(void **state) {
  (void)state;
  setup_mock();

  UT_array *args;
  utarray_new(args, &ut_str_icd);
  const char *arg1 = "measure";
  const char *arg2 = "--fail";
  utarray_push_back(args, &arg1);
  utarray_push_back(args, &arg2);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  assert_int_not_equal(r.exit_code, 0);
  assert_non_null((void *)r.stdout_data);
  assert_non_null((void *)r.stderr_data);

  free(r.stdout_data);
  free(r.stderr_data);
}

static void test_run_iss_failure_env(void **state) {
  (void)state;
  setup_mock();

  setenv_cross("MOCK_FAIL_ALL", "1");

  UT_array *args;
  utarray_new(args, &ut_str_icd);
  const char *arg1 = "measure";
  utarray_push_back(args, &arg1);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  assert_int_not_equal(r.exit_code, 0);

  unsetenv_cross("MOCK_FAIL_ALL");

  free(r.stdout_data);
  free(r.stderr_data);
}

/* ================================
   Server lifecycle
================================ */

static void test_start_server(void **state) {
  (void)state;
  setup_mock();
  start_server(); // should succeed
}

static void test_stop_server(void **state) {
  (void)state;
  setup_mock();
  stop_server(); // should succeed
}

/* ================================
   Instrument lifecycle
================================ */

static void test_start_instrument_no_plugin(void **state) {
  (void)state;
  setup_mock();

  Path *cfg = path_new("config.yml");
  start_instrument(cfg, NULL);
  path_free(cfg);
}

static void test_start_instrument_with_plugin(void **state) {
  (void)state;
  setup_mock();
  Path *cfg = path_new("config.yml");
  Path *plugin = path_new("plugin.so");

  start_instrument(cfg, plugin);

  path_free(cfg);
  path_free(plugin);
}

static void test_stop_instrument(void **state) {
  (void)state;
  setup_mock();
  stop_instrument("MyInstrument");
}

/* ================================
   Instrument status
================================ */

static void test_instrument_status(void **state) {
  (void)state;
  setup_mock();

  char *out = instrument_status("MyInstrument");

  assert_non_null((void *)out);
  // Replaces g_str_has_prefix using standard strncmp
  assert_int_equal(strncmp(out, "instrument OK", 13), 0);

  free(out);
}

/* ================================
   Measurement
================================ */

static void test_perform_measurement_failure(void **state) {
  (void)state;
  setup_mock();

  UT_array *args;
  utarray_new(args, &ut_str_icd);
  const char *arg1 = "measure";
  const char *arg2 = "--fail";
  utarray_push_back(args, &arg1);
  utarray_push_back(args, &arg2);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  assert_int_not_equal(r.exit_code, 0);

  free(r.stdout_data);
  free(r.stderr_data);
}

/* ================================
   Script-based measurement
================================ */

static void test_perform_measurement_success(void **state) {
  (void)state;
  setup_mock();

  const char *script = "function main(ctx) ctx:log('hello') end";

  // Initialize and populate a mock parameters Map instead of hardcoding JSON
  Map *vars = map_new();
  assert_non_null((void *)vars);
  map_add_float(vars, "target_voltage", 5.0f);
  map_add_string(vars, "mode", "calibration");

  // Call perform_measurement using our clean Map abstraction pointer
  const Result *res = perform_measurement(script, vars);

  // Clean up variables dictionary right after triggering execution
  map_free(vars);

  assert_non_null((void *)res);
  assert_string_equal(res->status, "success");
  assert_string_equal(res->script_name, "iv_curve.lua");
  assert_int_equal(res->step_count, 2);
  assert_non_null((void *)res->steps);

  const StepResult *step0 = &res->steps[0];
  assert_int_equal(step0->index, 0);
  assert_string_equal(step0->instrument, "MockInstrument1:1");
  assert_string_equal(step0->verb, "SET");
  assert_non_null((void *)step0->params_json);
  assert_int_equal(step0->return_type, VAL_TYPE_BOOL);
  assert_true(step0->return_value.b_val);

  const StepResult *step1 = &res->steps[1];
  assert_int_equal(step1->index, 4);
  assert_string_equal(step1->instrument, "Scope1");
  assert_string_equal(step1->verb, "CAPTURE");
  assert_int_equal(step1->return_type, VAL_TYPE_BUFFER);

  assert_string_equal(step1->return_value.buf_val.buffer_id, "buf_abc123");
  assert_int_equal(step1->return_value.buf_val.element_count, 10000);
  assert_string_equal(step1->return_value.buf_val.data_type, "float32");

  free_result(res);
}

/* ================================
   Buffer Integration Tests
================================ */

static void test_read_buffer_success(void **state) {
  (void)state;
  setup_mock();

  const char *test_id = "buffer_1779829276760326";
  const buffer *buf = read_buffer(test_id);

  assert_non_null((void *)buf);
  assert_string_equal(buf->buffer_id, test_id);
  assert_int_equal(buf->data_type, INST_DATA_FLOAT64);
  assert_int_equal(buf->element_count, 3);

  double *measurements = (double *)buf->data;
  assert_non_null((void *)measurements);
  assert_float_equal(measurements[0], 1.0, 0.00001);
  assert_float_equal(measurements[1], 2.5, 0.00001);
  assert_float_equal(measurements[2], 3.14159, 0.00001);

  free_buffer(buf);
}

static void test_release_buffer_success(void **state) {
  (void)state;
  setup_mock();

  release_buffer("buffer_1779829276760326");
}

/* ================================
   Dependency injection sanity
================================ */

static void test_dependency_injection(void **state) {
  (void)state;
  RunConfig cfg = {.instrument_server_path = MOCK_SERVER_PATH};
  run_set_config(&cfg);

  UT_array *args;
  utarray_new(args, &ut_str_icd);
  const char *arg1 = "measure";
  utarray_push_back(args, &arg1);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  assert_int_equal(r.exit_code, 0);

  free(r.stdout_data);
  free(r.stderr_data);
}

static void test_map_primitive_types(void **state) {
  (void)state;

  Map *m = map_new();
  assert_non_null((void *)m);

  // Populate map with primitive data types
  map_add_float(m, "pi", 3.14159f);
  map_add_int(m, "count", 42);
  map_add_string(m, "message", "hello world");
  map_add_bool(m, "flag", true);

  // Serialize map to JSON to assert internal values accurately
  char *raw_json = map_to_json(m);
  assert_non_null((void *)raw_json);

  cJSON *root = cJSON_Parse(raw_json);
  free(raw_json);
  assert_non_null((void *)root);

  // Assert primitive fields are present and accurately structured
  cJSON *pi_item = cJSON_GetObjectItemCaseSensitive(root, "pi");
  assert_true(cJSON_IsNumber(pi_item));
  assert_float_equal((float)pi_item->valuedouble, 3.14159f, 0.00001f);

  cJSON *count_item = cJSON_GetObjectItemCaseSensitive(root, "count");
  assert_true(cJSON_IsNumber(count_item));
  assert_int_equal(count_item->valueint, 42);

  cJSON *msg_item = cJSON_GetObjectItemCaseSensitive(root, "message");
  assert_true(cJSON_IsString(msg_item));
  assert_string_equal(msg_item->valuestring, "hello world");

  cJSON *flag_item = cJSON_GetObjectItemCaseSensitive(root, "flag");
  assert_true(cJSON_IsBool(flag_item));
  assert_true(cJSON_IsTrue(flag_item));

  cJSON_Delete(root);
  map_free(m);
}

/* =========================================================================
   3. Complex Nested Array Unit Tests
   ========================================================================= */

static void test_map_nested_arrays(void **state) {
  (void)state;

  Map *m = map_new();
  assert_non_null((void *)m);

  // --- 1. Populate String Array ---
  UT_array *str_arr;
  utarray_new(str_arr, &ut_str_icd);
  const char *s1 = "abc";
  const char *s2 = "xyz";
  utarray_push_back(str_arr, &s1);
  utarray_push_back(str_arr, &s2);
  map_add_array_string(m, "strings", (void *)str_arr);
  utarray_free(str_arr); // Safe to free local array right after map deep-copies

  // --- 2. Populate Number Array ---
  static const UT_icd native_float_icd = {sizeof(float), NULL, NULL, NULL};
  UT_array *num_arr;
  utarray_new(num_arr, &native_float_icd);
  float f1 = 10.5f;
  float f2 = -2.2f;
  utarray_push_back(num_arr, &f1);
  utarray_push_back(num_arr, &f2);
  map_add_array_number(m, "numbers", (void *)num_arr);
  utarray_free(num_arr);

  // --- 3. Populate Boolean Array ---
  static const UT_icd native_bool_icd = {sizeof(bool), NULL, NULL, NULL};
  UT_array *bool_arr;
  utarray_new(bool_arr, &native_bool_icd);
  bool b1 = true;
  bool b2 = false;
  utarray_push_back(bool_arr, &b1);
  utarray_push_back(bool_arr, &b2);
  map_add_array_bool(m, "booleans", (void *)bool_arr);
  utarray_free(bool_arr);

  // --- 4. Serialize and Verify Nested JSON Structures ---
  char *raw_json = map_to_json(m);
  cJSON *root = cJSON_Parse(raw_json);
  free(raw_json);
  assert_non_null((void *)root);

  // Validate String Array contents
  cJSON *j_str_arr = cJSON_GetObjectItemCaseSensitive(root, "strings");
  assert_true(cJSON_IsArray(j_str_arr));
  assert_int_equal(cJSON_GetArraySize(j_str_arr), 2);
  assert_string_equal(cJSON_GetArrayItem(j_str_arr, 0)->valuestring, "abc");
  assert_string_equal(cJSON_GetArrayItem(j_str_arr, 1)->valuestring, "xyz");

  // Validate Number Array contents
  cJSON *j_num_arr = cJSON_GetObjectItemCaseSensitive(root, "numbers");
  assert_true(cJSON_IsArray(j_num_arr));
  assert_int_equal(cJSON_GetArraySize(j_num_arr), 2);
  assert_float_equal((float)cJSON_GetArrayItem(j_num_arr, 0)->valuedouble,
                     10.5f, 0.00001f);
  assert_float_equal((float)cJSON_GetArrayItem(j_num_arr, 1)->valuedouble,
                     -2.2f, 0.00001f);

  // Validate Boolean Array contents
  cJSON *j_bool_arr = cJSON_GetObjectItemCaseSensitive(root, "booleans");
  assert_true(cJSON_IsArray(j_bool_arr));
  assert_int_equal(cJSON_GetArraySize(j_bool_arr), 2);
  assert_true(cJSON_IsTrue(cJSON_GetArrayItem(j_bool_arr, 0)));
  assert_true(cJSON_IsFalse(cJSON_GetArrayItem(j_bool_arr, 1)));

  cJSON_Delete(root);
  map_free(m);
}

/* ================================
   Main Engine Integration
================================ */

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  const struct CMUnitTest tests[] = {
      /* Core execution */
      cmocka_unit_test(test_run_executable_success),
      cmocka_unit_test(test_run_iss_failure_flag),
      cmocka_unit_test(test_run_iss_failure_env),

      /* Server */
      cmocka_unit_test(test_start_server),
      cmocka_unit_test(test_stop_server),

      /* Instrument */
      cmocka_unit_test(test_start_instrument_no_plugin),
      cmocka_unit_test(test_start_instrument_with_plugin),
      cmocka_unit_test(test_stop_instrument),
      cmocka_unit_test(test_instrument_status),

      /* Measurement */
      cmocka_unit_test(test_perform_measurement_failure),
      cmocka_unit_test(test_perform_measurement_success),

      /* Buffer Mechanics */
      cmocka_unit_test(test_read_buffer_success),
      cmocka_unit_test(test_release_buffer_success),

      /* DI */
      cmocka_unit_test(test_dependency_injection),

      /* map */
      cmocka_unit_test(test_perform_measurement_success),
      cmocka_unit_test(test_map_primitive_types),
      cmocka_unit_test(test_map_nested_arrays),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
