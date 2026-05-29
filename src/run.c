#include "isa-test-utils/run.h"
#include "utarray.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define strdup _strdup
#define close _close
#define strdup _strdup
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Helper to safely read an entire pipe/stream into a heap-allocated string
static char *read_all_from_fd(int fd) {
  size_t capacity = 512;
  size_t len = 0;
  char *buf = malloc(capacity);
  if (!buf)
    return strdup("");

  char tmp[256];
  ssize_t bytes;
#ifdef _WIN32
  while (
      ReadFile((HANDLE)(intptr_t)fd, tmp, sizeof(tmp), (DWORD *)&bytes, NULL) &&
      bytes > 0) {
#else
  while ((bytes = read(fd, tmp, sizeof(tmp))) > 0) {
#endif
    if (len + bytes >= capacity) {
      capacity = (len + bytes) * 2;
      char *new_buf = realloc(buf, capacity);
      if (!new_buf) {
        free(buf);
        return strdup("");
      }
      buf = new_buf;
    }
    memcpy(buf + len, tmp, bytes);
    len += bytes;
  }
  buf[len] = '\0';
  return buf;
}

// Cross-platform sync execution helper replacing g_spawn_sync
static bool os_spawn_sync(char **argv, char **out_str, char **err_str,
                          int *status) {
#ifdef _WIN32
  // Flatten argv array back into a single Windows command line string
  char cmdline[2048] = {0};
  for (int i = 0; argv[i] != NULL; i++) {
    strcat_s(cmdline, sizeof(cmdline), argv[i]);
    strcat_s(cmdline, sizeof(cmdline), " ");
  }

  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  HANDLE out_r, out_w, err_r, err_w;
  CreatePipe(&out_r, &out_w, &sa, 0);
  CreatePipe(&err_r, &err_w, &sa, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFO si = {0};
  si.cb = sizeof(si);
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  si.dwFlags |= STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi = {0};

  if (!CreateProcess(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                     NULL, &si, &pi)) {
    CloseHandle(out_r);
    CloseHandle(out_w);
    CloseHandle(err_r);
    CloseHandle(err_w);
    return false;
  }
  CloseHandle(out_w);
  CloseHandle(err_w);
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  *status = (int)exit_code;
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  *out_str = read_all_from_fd((int)(intptr_t)out_r);
  *err_str = read_all_from_fd((int)(intptr_t)err_r);
  CloseHandle(out_r);
  CloseHandle(err_r);
  return true;
#else
  int out_pipe[2], err_pipe[2];
  if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0)
    return false;

  pid_t pid = fork();
  if (pid < 0)
    return false;

  if (pid == 0) {
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);

    execvp(argv[0], argv);
    _exit(127);
  }

  close(out_pipe[1]);
  close(err_pipe[1]);
  *out_str = read_all_from_fd(out_pipe[0]);
  *err_str = read_all_from_fd(err_pipe[0]);
  close(out_pipe[0]);
  close(err_pipe[0]);

  int wait_status;
  waitpid(pid, &wait_status, 0);
  *status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
  return (*status != 127);
#endif
}

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

static char **utarray_to_strv(UT_array *arr) {
  if (!arr)
    return NULL;

  unsigned int num_elements = utarray_len(arr);

  char **argv = calloc(num_elements + 1, sizeof(char *));
  if (!argv)
    return NULL;

  char **p = NULL;
  unsigned int i = 0;
  while ((p = (char **)utarray_next(arr, p))) {
    argv[i] = strdup(*p);
    i++;
  }

  return argv;
}

static void free_strv(char **strv) {
  if (!strv)
    return;

  for (size_t i = 0; strv[i] != NULL; i++) {
    free(strv[i]);
  }
  free(strv);
}

/* ================================
   Core Execution
================================ */

ProcessResult run_executable(const char *exe, void *args) {
  ProcessResult result = {0};
  UT_array *arr = (UT_array *)args;
  char **argv = utarray_to_strv(arr);
  unsigned int argc = arr ? utarray_len(arr) : 0;
  char **full = calloc(argc + 2, sizeof(char *));
  if (!full) {
    free_strv(argv);
    result.exit_code = -1;
    result.stdout_data = strdup("");
    result.stderr_data = strdup("Memory allocation failed");
    return result;
  }

  full[0] = strdup(exe);
  for (unsigned int i = 0; i < argc; i++) {
    full[i + 1] = strdup(argv[i]);
  }
  full[argc + 1] = NULL; // Explicit null termination

  char *out = NULL;
  char *err = NULL;
  int status = 0;

  bool ok = os_spawn_sync(full, &out, &err, &status);

  if (!ok) {
    result.exit_code = -1;
    result.stdout_data = strdup("");
    result.stderr_data = strdup("Spawn failed or binary not found in PATH.");
    free(out);
    free(err);
  } else {
    result.exit_code = status;
    result.stdout_data = out ? out : strdup("");
    result.stderr_data = err ? err : strdup("");
  }

  free_strv(argv);
  free_strv(full);

  return result;
}

ProcessResult run_iss(void *args) {
  const char *exe = g_config.instrument_server_path
                        ? g_config.instrument_server_path
                        : "instrument-script-server";

  ProcessResult r = run_executable(exe, args);

  if (r.exit_code == -1) {
    free(r.stderr_data);

    size_t err_msg_len = strlen(exe) + 64;
    r.stderr_data = malloc(err_msg_len);
    if (r.stderr_data) {
      snprintf(r.stderr_data, err_msg_len,
               "%s not found in PATH. Ensure it is installed correctly.", exe);
    }
  }

  return r;
}

/* ================================
   Server API
================================ */

#include "isa-test-utils/run.h"
#include "utarray.h"
#include <stdio.h>
#include <stdlib.h>

void start_server(void) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *arg1 = "daemon";
  const char *arg2 = "start";
  utarray_push_back(args, &arg1);
  utarray_push_back(args, &arg2);

  ProcessResult r = run_iss(args);

  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Failed to start server: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown error");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }

  free(r.stdout_data);
  free(r.stderr_data);
}

void stop_server(void) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *arg1 = "daemon";
  const char *arg2 = "stop";
  utarray_push_back(args, &arg1);
  utarray_push_back(args, &arg2);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  free(r.stdout_data);
  free(r.stderr_data);
}

void start_instrument(const char *config, const char *plugin) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "start";
  utarray_push_back(args, &cmd);
  utarray_push_back(args, &config);

  if (plugin) {
    const char *flag = "--plugin";
    utarray_push_back(args, &flag);
    utarray_push_back(args, &plugin);
  }

  ProcessResult r = run_iss(args);
  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Failed to start instrument: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown error");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }

  free(r.stdout_data);
  free(r.stderr_data);
}

char *instrument_status(const char *name) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *arg1 = "instrument";
  const char *arg2 = "status";
  utarray_push_back(args, &arg1);
  utarray_push_back(args, &arg2);
  utarray_push_back(args, &name);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  free(r.stderr_data);
  return r.stdout_data;
}

void stop_instrument(const char *name) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "stop";
  utarray_push_back(args, &cmd);
  utarray_push_back(args, &name);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  free(r.stdout_data);
  free(r.stderr_data);
}

/* ================================
   Measurement
================================ */

static char *perform_measurement(const char *script, const char *json) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "measure";
  const char *flag1 = "--globals";
  const char *flag2 = "--json";

  utarray_push_back(args, &cmd);
  utarray_push_back(args, &script);
  utarray_push_back(args, &flag1);
  utarray_push_back(args, &json);
  utarray_push_back(args, &flag2);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Measurement failed: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown error");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }

  free(r.stderr_data);
  return r.stdout_data;
}

char *perform_measurement_from_script(const char *contents, const char *json) {
  char *tmp_path = NULL;

#ifdef _WIN32
  char temp_dir[MAX_PATH];
  char temp_file[MAX_PATH];

  if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
    fprintf(stderr, "ERROR: Failed to get Windows temp path.\n");
    exit(1);
  }
  if (GetTempFileNameA(temp_dir, "iss", 0, temp_file) == 0) {
    fprintf(stderr, "ERROR: Failed to create Windows temp file name.\n");
    exit(1);
  }
  // Convert Windows path into our standard heap string layout
  tmp_path = _strdup(temp_file);
#else
  char template[] = "/tmp/iss-script-XXXXXX";
  int fd = mkstemp(template);
  if (fd < 0) {
    perror("ERROR: mkstemp failed"); // Prints the exact OS reason (e.g.
                                     // Permission Denied)
    exit(1);
  }
  close(fd);

  size_t path_len =
      strlen(template) + 5; // 4 bytes for ".lua" + 1 for null terminator
  tmp_path = malloc(path_len);
  if (!tmp_path) {
    fprintf(stderr, "ERROR: Memory allocation failed for tmp_path\n");
    remove(template);
    exit(1);
  }

  snprintf(tmp_path, path_len, "%s.lua", template);
  remove(template);
#endif

  FILE *f = fopen(tmp_path, "wb");
  if (!f) {
    fprintf(stderr, "ERROR: Failed to open temp file for writing: %s\n",
            tmp_path);
    free(tmp_path);
    exit(1);
  }

  size_t contents_len = strlen(contents);
  if (contents_len > 0 &&
      fwrite(contents, 1, contents_len, f) != contents_len) {
    fprintf(stderr, "ERROR: Failed to write script contents to file.\n");
    fclose(f);
    remove(tmp_path);
    free(tmp_path);
    exit(1);
  }
  fclose(f);

  char *out = perform_measurement(tmp_path, json);

  remove(tmp_path);
  free(tmp_path);

  return out;
}
