#include "isa-test-utils/run.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

/* ================================
   Config (Dependency Injection)
================================ */

static RunConfig g_config = {.instrument_server_path =
                                 "instrument-script-server"};

void run_set_config(const RunConfig *config) {
  if (config) {
    g_config = *config;
  }
}

/* ================================
   Helpers
================================ */

static gchar **gptrarray_to_strv(GPtrArray *arr) {
  if (!arr)
    return NULL;

  gchar **argv = g_new0(gchar *, arr->len + 1);

  for (guint i = 0; i < arr->len; i++) {
    argv[i] = g_strdup((char *)g_ptr_array_index(arr, i));
  }

  return argv;
}

static void free_strv(gchar **strv) {
  if (!strv)
    return;
  for (guint i = 0; strv[i]; i++) {
    g_free(strv[i]);
  }
  g_free(strv);
}

/* ================================
   Core Execution
================================ */

ProcessResult run_executable(const char *exe, GPtrArray *args) {
  ProcessResult result = {0};

  gchar **argv = gptrarray_to_strv(args);

  guint argc = args ? args->len : 0;
  gchar **full = g_new0(gchar *, argc + 2);

  full[0] = g_strdup(exe);
  for (guint i = 0; i < argc; i++) {
    full[i + 1] = g_strdup(argv[i]);
  }

  gchar *out = NULL;
  gchar *err = NULL;
  gint status = 0;
  GError *error = NULL;

  gboolean ok = g_spawn_sync(NULL, full, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                             &out, &err, &status, &error);

  if (!ok) {
    result.exit_code = -1;
    result.stdout_data = g_strdup("");
    result.stderr_data = g_strdup(error ? error->message : "spawn failed");
    if (error)
      g_error_free(error);
  } else {
    result.exit_code = status;
    result.stdout_data = out ? out : g_strdup("");
    result.stderr_data = err ? err : g_strdup("");
  }

  free_strv(argv);
  free_strv(full);

  return result;
}

ProcessResult run_iss(GPtrArray *args) {
  const char *exe = g_config.instrument_server_path
                        ? g_config.instrument_server_path
                        : "instrument-script-server";

  ProcessResult r = run_executable(exe, args);

  if (r.exit_code == -1) {
    g_free(r.stderr_data);
    r.stderr_data = g_strdup_printf(
        "%s not found in PATH. Ensure it is installed correctly.", exe);
  }

  return r;
}

/* ================================
   Server API
================================ */

void start_server(void) {
  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "daemon");
  g_ptr_array_add(args, "start");

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  if (r.exit_code != 0) {
    g_error("Failed to start server: %s", r.stderr_data);
  }

  g_free(r.stdout_data);
  g_free(r.stderr_data);
}

void stop_server(void) {
  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "daemon");
  g_ptr_array_add(args, "stop");

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
}

void start_instrument(const char *config, const char *plugin) {
  GPtrArray *args = g_ptr_array_new();

  g_ptr_array_add(args, "start");
  g_ptr_array_add(args, (char *)config);

  if (plugin) {
    g_ptr_array_add(args, "--plugin");
    g_ptr_array_add(args, (char *)plugin);
  }

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  if (r.exit_code != 0) {
    g_error("Failed to start instrument: %s", r.stderr_data);
  }

  g_free(r.stdout_data);
  g_free(r.stderr_data);
}

char *instrument_status(const char *name) {
  GPtrArray *args = g_ptr_array_new();

  g_ptr_array_add(args, "instrument");
  g_ptr_array_add(args, "status");
  g_ptr_array_add(args, (char *)name);

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  g_free(r.stderr_data);
  return r.stdout_data;
}

void stop_instrument(const char *name) {
  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, "stop");
  g_ptr_array_add(args, (char *)name);

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  g_free(r.stdout_data);
  g_free(r.stderr_data);
}

/* ================================
   Measurement
================================ */

static char *perform_measurement(const char *script, const char *json) {
  GPtrArray *args = g_ptr_array_new();

  g_ptr_array_add(args, "measure");
  g_ptr_array_add(args, (char *)script);
  g_ptr_array_add(args, "--globals");
  g_ptr_array_add(args, (char *)json);
  g_ptr_array_add(args, "--json");

  ProcessResult r = run_iss(args);
  g_ptr_array_free(args, TRUE);

  if (r.exit_code != 0) {
    g_error("Measurement failed: %s", r.stderr_data);
  }

  g_free(r.stderr_data);
  return r.stdout_data;
}

char *perform_measurement_from_script(const char *contents, const char *json) {
  GError *error = NULL;
  char *tmp = NULL;

  int fd = g_file_open_tmp("iss-script-XXXXXX.lua", &tmp, &error);
  if (fd < 0) {
    g_error("Failed to create temp script: %s", error->message);
  }
  close(fd);

  if (!g_file_set_contents(tmp, contents, -1, &error)) {
    g_error("Failed to write script: %s", error->message);
  }

  char *out = perform_measurement(tmp, json);

  g_remove(tmp);
  g_free(tmp);

  return out;
}
