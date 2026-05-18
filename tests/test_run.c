#include "isa-test-utils/run.h"
#include <glib.h>

/* ================================
   Setup
================================ */

static void setup_mock(void) {
  run_set_config(&(RunConfig){.instrument_server_path = MOCK_SERVER_PATH});
}

/* ================================
   run_executable
================================ */

static void test_run_executable_success(void) {
  setup_mock();

  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "measure");

  ProcessResult r = run_iss(args);

  g_assert_cmpint(r.exit_code, ==, 0);
  g_assert_nonnull(r.stdout_data);
  g_assert_nonnull(r.stderr_data);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
  g_ptr_array_free(args, TRUE);
}

/* ================================
   failure path
================================ */

static void test_run_iss_failure_flag(void) {
  setup_mock();

  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "measure");
  g_ptr_array_add(args, "--fail");

  ProcessResult r = run_iss(args);

  g_assert_cmpint(r.exit_code, !=, 0);
  g_assert_nonnull(r.stdout_data);
  g_assert_nonnull(r.stderr_data);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
  g_ptr_array_free(args, TRUE);
}

static void test_run_iss_failure_env(void) {
  setup_mock();

  g_setenv("MOCK_FAIL_ALL", "1", TRUE);

  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "measure");

  ProcessResult r = run_iss(args);

  g_assert_cmpint(r.exit_code, !=, 0);

  g_unsetenv("MOCK_FAIL_ALL");

  g_free(r.stdout_data);
  g_free(r.stderr_data);
  g_ptr_array_free(args, TRUE);
}

/* ================================
   Server lifecycle
================================ */

static void test_start_server(void) {
  setup_mock();
  start_server(); // should succeed
}

static void test_stop_server(void) {
  setup_mock();
  stop_server(); // should succeed
}

/* ================================
   Instrument lifecycle
================================ */

static void test_start_instrument_no_plugin(void) {
  setup_mock();
  start_instrument("config.yml", NULL);
}

static void test_start_instrument_with_plugin(void) {
  setup_mock();
  start_instrument("config.yml", "plugin.so");
}

static void test_stop_instrument(void) {
  setup_mock();
  stop_instrument("MyInstrument");
}

/* ================================
   Instrument status
================================ */

static void test_instrument_status(void) {
  setup_mock();

  char *out = instrument_status("MyInstrument");

  g_assert_nonnull(out);
  g_assert_true(g_str_has_prefix(out, "instrument OK"));

  g_free(out);
}

/* ================================
   Measurement
================================ */

static void test_perform_measurement_failure(void) {
  setup_mock();

  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "measure");
  g_ptr_array_add(args, "--fail");

  ProcessResult r = run_iss(args);

  g_assert_cmpint(r.exit_code, !=, 0);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
  g_ptr_array_free(args, TRUE);
}

/* ================================
   Script-based measurement
================================ */

static void test_measurement_from_script(void) {
  setup_mock();

  const char *script = "function main(ctx) ctx:log('hello') end";

  char *out = perform_measurement_from_script(script, "{}");

  g_assert_nonnull(out);
  g_assert_true(g_strstr_len(out, -1, "result") != NULL);

  g_free(out);
}

/* ================================
   Dependency injection sanity
================================ */

static void test_dependency_injection(void) {
  RunConfig cfg = {.instrument_server_path = MOCK_SERVER_PATH};
  run_set_config(&cfg);

  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "measure");

  ProcessResult r = run_iss(args);

  g_assert_cmpint(r.exit_code, ==, 0);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
  g_ptr_array_free(args, TRUE);
}

/* ================================
   Main
================================ */

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  /* Core execution */
  g_test_add_func("/run/exec_success", test_run_executable_success);
  g_test_add_func("/run/fail_flag", test_run_iss_failure_flag);
  g_test_add_func("/run/fail_env", test_run_iss_failure_env);

  /* Server */
  g_test_add_func("/run/server/start", test_start_server);
  g_test_add_func("/run/server/stop", test_stop_server);

  /* Instrument */
  g_test_add_func("/run/instrument/start_no_plugin",
                  test_start_instrument_no_plugin);
  g_test_add_func("/run/instrument/start_with_plugin",
                  test_start_instrument_with_plugin);
  g_test_add_func("/run/instrument/stop", test_stop_instrument);
  g_test_add_func("/run/instrument/status", test_instrument_status);

  /* Measurement */
  g_test_add_func("/run/measurement/failure", test_perform_measurement_failure);
  g_test_add_func("/run/measurement/script", test_measurement_from_script);

  /* DI */
  g_test_add_func("/run/di", test_dependency_injection);

  return g_test_run();
}
