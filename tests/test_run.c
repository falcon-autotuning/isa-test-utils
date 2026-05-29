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
  assert_non_null(r.stdout_data);
  assert_non_null(r.stderr_data);

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
  assert_non_null(r.stdout_data);
  assert_non_null(r.stderr_data);

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

  assert_non_null(out);
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

static void test_measurement_from_script(void **state) {
  (void)state;
  setup_mock();

  const char *script = "function main(ctx) ctx:log('hello') end";

  char *out = perform_measurement_from_script(script, "{}");

  assert_non_null(out);
  // Replaces g_strstr_len with standard strstr lookup
  assert_non_null(strstr(out, "result"));

  free(out);
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
      cmocka_unit_test(test_measurement_from_script),

      /* DI */
      cmocka_unit_test(test_dependency_injection),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
