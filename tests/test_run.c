#include <cmocka.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "isa-test-utils/run.h"
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
  start_instrument("config.yml", NULL);
}

static void test_start_instrument_with_plugin(void **state) {
  (void)state;
  setup_mock();
  start_instrument("config.yml", "plugin.so");
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

  const MeasurementResult *res = perform_measurement(script, "{}");

  assert_non_null((void *)res);
  assert_string_equal(res->status, "success");
  assert_string_equal(res->script_name, "iv_curve.lua");
  assert_int_equal(res->step_count, 2);
  assert_non_null((void *)res->steps);

  const ScriptStepResult *step0 = &res->steps[0];
  assert_int_equal(step0->index, 0);
  assert_string_equal(step0->instrument, "MockInstrument1:1");
  assert_string_equal(step0->verb, "SET");
  assert_non_null((void *)step0->params_json);
  assert_int_equal(step0->return_type, VAL_TYPE_BOOL);
  assert_true(step0->return_value.b_val);

  const ScriptStepResult *step1 = &res->steps[1];
  assert_int_equal(step1->index, 4);
  assert_string_equal(step1->instrument, "Scope1");
  assert_string_equal(step1->verb, "CAPTURE");
  assert_int_equal(step1->return_type, VAL_TYPE_BUFFER);

  assert_string_equal(step1->return_value.buf_val.buffer_id, "buf_abc123");
  assert_int_equal(step1->return_value.buf_val.element_count, 10000);
  assert_string_equal(step1->return_value.buf_val.data_type, "float32");

  free_measurement_result(res);
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
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
