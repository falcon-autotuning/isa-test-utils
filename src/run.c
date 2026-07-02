#include "isa-test-utils.h"
#include "internal/measurement-helpers.h"
#include "internal/exec.h"
#include "path.h"
#include "utarray.h"
#include <cjson/cJSON.h>
#include <instrument-data.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
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

#ifdef _WIN32
  DWORD bytes =
      0; // Windows uses a 32-bit unsigned DWORD for tracking read bytes
  while (ReadFile((HANDLE)(intptr_t)fd, tmp, sizeof(tmp), &bytes, NULL) &&
         bytes > 0) {
#else
  ssize_t bytes = 0; // POSIX platforms use standard signed ssize_t
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
  // FIXED WINDOWS COMPATIBILITY: Wrap all paths and arguments in double quotes
  // to protect space boundaries and raw slashes passed to the OS
  char cmdline[4096] = {0};
  for (int i = 0; argv[i] != NULL; i++) {
    strcat_s(cmdline, sizeof(cmdline), "\"");
    strcat_s(cmdline, sizeof(cmdline), argv[i]);
    strcat_s(cmdline, sizeof(cmdline), "\" ");
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

  // PASSING cmdline as the secondary argument gives CreateProcess a mutable
  // string block
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

// Concrete definition hidden entirely from your public API consumers
// Types of data our map values can hold
typedef enum {
  MAP_INT = 0,
  MAP_FLOAT,
  MAP_STRING,
  MAP_BOOL,
  MAP_ARRAY_STRING,
  MAP_ARRAY_NUMBER,
  MAP_ARRAY_BOOLEAN
} MapValueType;

// Individual Key-Value entry layout
typedef struct {
  char *key;
  MapValueType type;
  union {
    float f_val;
    int i_val;
    char *string_val;
    bool bool_val;
    void *as_val;
    void *an_val;
    void *ab_val;
  } value;
} MapEntry;
struct Map {
  UT_array *entries;
};

// Internal destructor to deep-free allocated union variants and strings cleanly
static void free_map_entry_cb(void *elt) {
  MapEntry *e = *(MapEntry **)elt;
  if (!e)
    return;

  free(e->key);

  switch (e->type) {
  case MAP_STRING:
    free(e->value.string_val);
    break;
  case MAP_ARRAY_STRING:
    if (e->value.as_val) {
      // Cast to UT_array* to clear the string elements via its native ICD
      utarray_free((UT_array *)e->value.as_val);
    }
    break;
  case MAP_ARRAY_NUMBER:
    if (e->value.an_val) {
      utarray_free((UT_array *)e->value.an_val);
    }
    break;
  case MAP_ARRAY_BOOLEAN:
    if (e->value.ab_val) {
      utarray_free((UT_array *)e->value.ab_val);
    }
    break;
  default:
    break; // Primitives require no extra nested cleanup passes
  }
  free(e);
}

// Map configuration entry descriptor block metadata
static const UT_icd map_entry_icd = {sizeof(MapEntry *), NULL, NULL,
                                     free_map_entry_cb};

Map *map_new() {
  Map *m = calloc(1, sizeof(Map));
  if (!m)
    return NULL;

  utarray_new(m->entries, &map_entry_icd);
  return m;
}

void map_free(Map *map) {
  if (!map)
    return;
  // This automatically deep-frees all internally allocated entries via our
  // free_map_entry_cb
  utarray_free(map->entries);
  free(map);
}

void map_add_float(Map *map, const char *key, float val) {
  if (!map || !key)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_FLOAT;
  e->value.f_val = val;
  utarray_push_back(map->entries, &e);
}

void map_add_int(Map *map, const char *key, int val) {
  if (!map || !key)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_INT;
  e->value.i_val = val;
  utarray_push_back(map->entries, &e);
}

void map_add_string(Map *map, const char *key, const char *val) {
  if (!map || !key)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_STRING;
  e->value.string_val = strdup(val ? val : "");
  utarray_push_back(map->entries, &e);
}

void map_add_bool(Map *map, const char *key, bool val) {
  if (!map || !key)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_BOOL;
  e->value.bool_val = val;
  utarray_push_back(map->entries, &e);
}

/* =========================================================================
   Opaque Array Handlers
   ========================================================================= */

// Custom copy callbacks for tracking array primitives cleanly inside cloned
// maps
static const UT_icd native_float_icd = {sizeof(float), NULL, NULL, NULL};
static const UT_icd native_bool_icd = {sizeof(bool), NULL, NULL, NULL};
static void free_str_cb(void *elt) {
  char *s = *(char **)elt;
  free(s);
}
static const UT_icd owned_str_icd = {sizeof(char *), NULL, NULL, free_str_cb};

void map_add_array_string(Map *map, const char *key, void *val) {
  if (!map || !key || !val)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_ARRAY_STRING;

  // Clone incoming dynamic string utarray to maintain clean, isolated memory
  // ownership
  UT_array *src = (UT_array *)val;
  UT_array *dst;
  utarray_new(dst, &owned_str_icd);

  char **p = NULL;
  while ((p = (char **)utarray_next(src, p))) {
    char *str_copy = strdup(*p);
    utarray_push_back(dst, &str_copy);
  }
  e->value.as_val = (void *)dst;
  utarray_push_back(map->entries, &e);
}

void map_add_array_number(Map *map, const char *key, void *val) {
  if (!map || !key || !val)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_ARRAY_NUMBER;

  UT_array *src = (UT_array *)val;
  UT_array *dst;
  utarray_new(dst, &native_float_icd);

  float *p = NULL;
  while ((p = (float *)utarray_next(src, p))) {
    utarray_push_back(dst, p);
  }
  e->value.an_val = (void *)dst;
  utarray_push_back(map->entries, &e);
}

void map_add_array_bool(Map *map, const char *key, void *val) {
  if (!map || !key || !val)
    return;
  MapEntry *e = calloc(1, sizeof(MapEntry));
  if (!e)
    return;

  e->key = strdup(key);
  e->type = MAP_ARRAY_BOOLEAN;

  UT_array *src = (UT_array *)val;
  UT_array *dst;
  utarray_new(dst, &native_bool_icd);

  bool *p = NULL;
  while ((p = (bool *)utarray_next(src, p))) {
    utarray_push_back(dst, p);
  }
  e->value.ab_val = (void *)dst;
  utarray_push_back(map->entries, &e);
}

char *map_to_json(const Map *map) {
  if (!map || !map->entries)
    return strdup("{}");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;

  unsigned int num_elements = utarray_len(map->entries);
  for (unsigned int i = 0; i < num_elements; i++) {
    MapEntry **entry_ptr = (MapEntry **)utarray_eltptr(map->entries, i);
    if (!entry_ptr || !(*entry_ptr))
      continue;
    MapEntry *e = *entry_ptr;

    if (!e->key)
      continue;

    switch (e->type) {
    case MAP_FLOAT:
      cJSON_AddNumberToObject(root, e->key, (double)e->value.f_val);
      break;
    case MAP_INT:
      cJSON_AddNumberToObject(root, e->key, e->value.i_val);
      break;
    case MAP_STRING:
      cJSON_AddStringToObject(root, e->key,
                              e->value.string_val ? e->value.string_val : "");
      break;
    case MAP_BOOL:
      cJSON_AddBoolToObject(root, e->key, e->value.bool_val);
      break;
    case MAP_ARRAY_STRING: {
      cJSON *arr = cJSON_CreateArray();
      UT_array *str_arr = (UT_array *)e->value.as_val;
      if (str_arr) {
        char **p = NULL;
        while ((p = (char **)utarray_next(str_arr, p))) {
          cJSON_AddItemToArray(arr, cJSON_CreateString(*p));
        }
      }
      cJSON_AddItemToObject(root, e->key, arr);
      break;
    }
    case MAP_ARRAY_NUMBER: {
      cJSON *arr = cJSON_CreateArray();
      UT_array *num_arr = (UT_array *)e->value.an_val;
      if (num_arr) {
        float *p = NULL;
        while ((p = (float *)utarray_next(num_arr, p))) {
          cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)*p));
        }
      }
      cJSON_AddItemToObject(root, e->key, arr);
      break;
    }
    case MAP_ARRAY_BOOLEAN: {
      cJSON *arr = cJSON_CreateArray();
      UT_array *bool_arr = (UT_array *)e->value.ab_val;
      if (bool_arr) {
        bool *p = NULL;
        while ((p = (bool *)utarray_next(bool_arr, p))) {
          cJSON_AddItemToArray(arr, cJSON_CreateBool(*p));
        }
      }
      cJSON_AddItemToObject(root, e->key, arr);
      break;
    }
    }
  }

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json_str;
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

  // 1. Duplicate string to apply invariant transformations
  char *normalized_exe = strdup(exe ? exe : "");
  if (!normalized_exe) {
    free_strv(argv);
    result.exit_code = -1;
    result.stdout_data = strdup("");
    result.stderr_data = strdup("Memory allocation failed");
    return result;
  }

  // 2. FIXED: Normalize paths dynamically according to target OS rules
  for (size_t i = 0; normalized_exe[i] != '\0'; i++) {
    if (normalized_exe[i] == OPPOSITE_SEPARATOR) {
      normalized_exe[i] = DIR_SEPARATOR;
    }
  }

  // 3. FIXED: Identify if we need an explicit shell runtime on Windows
  bool needs_sh_wrapper = false;
#ifdef _WIN32
  if (strstr(normalized_exe, ".sh") != NULL) {
    needs_sh_wrapper = true;
  }
#endif

  // 4. Calculate adjusted boundaries
  unsigned int extra_slots = needs_sh_wrapper ? 2 : 1;
  char **full = calloc(argc + extra_slots + 1, sizeof(char *));
  if (!full) {
    free(normalized_exe);
    free_strv(argv);
    result.exit_code = -1;
    result.stdout_data = strdup("");
    result.stderr_data = strdup("Memory allocation failed");
    return result;
  }

  unsigned int dest_idx = 0;
#ifdef _WIN32
  if (needs_sh_wrapper) {
    full[dest_idx++] =
        strdup("sh"); // Prepend shell interpreter so Windows can read it
  }
#endif

  full[dest_idx++] = normalized_exe; // Already allocated via strdup
  for (unsigned int i = 0; i < argc; i++) {
    full[dest_idx++] = strdup(argv[i]);
  }
  full[dest_idx] = NULL;

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
  free_strv(
      full); // This safely releases normalized_exe since it's stored inside

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

void start_instrument(const Path *config, const Path *plugin) {
  UT_array *args;
  char *con = path_free_to_path(path_clone(config));
  char *plug = path_free_to_path(path_clone(plugin));
  utarray_new(args, &ut_str_icd);

  const char *cmd = "inst";
  const char *subcmd = "start";
  utarray_push_back(args, &cmd);
  utarray_push_back(args, &subcmd);
  utarray_push_back(args, &con);

  if (plugin) {
    const char *flag = "--plugin";
    utarray_push_back(args, &flag);
    utarray_push_back(args, &plug);
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
  free(con);
  free(plug);
}

char *instrument_status(const char *name) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *arg1 = "inst";
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

  const char *cmd = "inst";
  const char *subcmd = "stop";
  utarray_push_back(args, &cmd);
  utarray_push_back(args, &subcmd);
  utarray_push_back(args, &name);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  free(r.stdout_data);
  free(r.stderr_data);
}

/* ================================
   Measurement
================================ */

static char *extract_json_blob(const char *text) {
  if (!text)
    return NULL;
  const char *p = text;

  /* Find first JSON structural boundary start point: '{' or '[' */
  while (*p && *p != '{' && *p != '[') {
    p++;
  }
  if (!*p)
    return NULL;

  char open_char = *p;
  char close_char = (open_char == '{') ? '}' : ']';
  const char *start = p;

  int depth = 0;
  bool in_string = false;
  bool escape = false;

  for (; *p; p++) {
    char c = *p;

    if (escape) {
      escape = false;
      continue;
    }
    if (c == '\\') {
      escape = true;
      continue;
    }
    if (c == '"') {
      in_string = !in_string;
      continue;
    }

    if (!in_string) {
      if (c == open_char) {
        depth++;
      } else if (c == close_char) {
        depth--;

        if (depth == 0) {
          // Replaces g_strndup with standard string extraction
          size_t extracted_len = (size_t)(p - start + 1);
          char *json_text = malloc(extracted_len + 1);
          if (!json_text)
            return NULL;

          memcpy(json_text, start, extracted_len);
          json_text[extracted_len] = '\0';

          // Leverage cJSON to immediately validate the extracted chunk before
          // returning
          cJSON *test_parse = cJSON_Parse(json_text);
          if (!test_parse) {
            free(json_text);
            return NULL;
          }
          cJSON_Delete(test_parse);

          return json_text;
        }
      }
    }
  }

  return NULL; // No fully matched JSON closure discovered
}
// Reusable intermediate function to parse primitive string tokens down to types
static ValueType map_string_to_val_type(const char *str) {
  if (!str)
    return VAL_TYPE_VOID;
  if (strcmp(str, "double") == 0)
    return VAL_TYPE_DOUBLE;
  if (strcmp(str, "int64") == 0)
    return VAL_TYPE_INT64;
  if (strcmp(str, "string") == 0)
    return VAL_TYPE_STRING;
  if (strcmp(str, "bool") == 0)
    return VAL_TYPE_BOOL;
  if (strcmp(str, "buffer") == 0)
    return VAL_TYPE_BUFFER;
  return VAL_TYPE_VOID;
}
char *write_script_to_temp(const char *contents) {
  char *tmp_path = NULL;

#ifdef _WIN32
  char temp_dir[MAX_PATH];
  char temp_file[MAX_PATH];

  if (GetTempPathA(MAX_PATH, temp_dir) == 0 ||
      GetTempFileNameA(temp_dir, "iss", 0, temp_file) == 0) {
    fprintf(stderr,
            "FATAL ERROR: Failed to create Windows temporary tracking file.\n");
    exit(1);
  }
  tmp_path = strdup(temp_file);
#else
  // Reused from our earlier fix: generate name and append extension afterward
  char template[] = "/tmp/iss-script-XXXXXX";
  int fd = mkstemp(template);
  if (fd < 0) {
    perror("FATAL ERROR: mkstemp failed");
    exit(1);
  }
  close(fd);

  size_t path_len = strlen(template) + 5; // 4 bytes for ".lua" + 1 for null
  tmp_path = malloc(path_len);
  if (!tmp_path) {
    remove(template);
    exit(1);
  }
  snprintf(tmp_path, path_len, "%s.lua", template);
  remove(template); // remove extensionless file left by mkstemp
#endif

  // Stream script data via standard I/O streams
  FILE *f = fopen(tmp_path, "wb");
  if (!f) {
    fprintf(stderr,
            "FATAL ERROR: Failed to open temp script path for writing.\n");
    free(tmp_path);
    exit(1);
  }

  size_t contents_len = contents ? strlen(contents) : 0;
  if (contents_len > 0) {
    if (fwrite(contents, 1, contents_len, f) != contents_len) {
      fprintf(stderr, "FATAL ERROR: Failed to write full stream payloads to "
                      "temp script.\n");
      fclose(f);
      remove(tmp_path);
      free(tmp_path);
      exit(1);
    }
  }
  fclose(f);

  return tmp_path; // Caller owns this string and must free() it later
}

ISA_TEST_UTILS_EXPORT const Result *
perform_measurement(const char *script_contents, const Map *variables) {

  char *tmp_script_path = write_script_to_temp(script_contents);
  if (!tmp_script_path)
    return NULL;
  char *input = map_to_json(variables);
  if (input == NULL) {
    fprintf(stderr, "ERROR: Variable conversion to json\n");
    free(tmp_script_path);
    free(input);
    exit(1);
  }

  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "measure";
  const char *flag1 = "--globals-json";
  const char *flag2 = "--json";

  utarray_push_back(args, &cmd);
  utarray_push_back(args, &tmp_script_path);
  utarray_push_back(args, &flag1);
  utarray_push_back(args, &input);
  utarray_push_back(args, &flag2);

  ProcessResult r = run_iss(args);
  utarray_free(args);
  free(input);

  // Clean up disk footprint unlinking temp script allocation path immediately
  remove(tmp_script_path);
  free(tmp_script_path);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Measurement processing execution failure: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }
  free(r.stderr_data);

  // 3. Isolate the target json blob boundary from raw stdout noise
  char *json_blob = extract_json_blob(r.stdout_data);
  free(r.stdout_data);

  if (!json_blob) {
    fprintf(stderr, "ERROR: No valid measurement JSON block found in server "
                    "stdout stream\n");
    return NULL;
  }

  cJSON *json = cJSON_Parse(json_blob);
  free(json_blob);

  if (!json) {
    fprintf(stderr, "ERROR: JSON tracking schema parse validation error\n");
    exit(1);
  }

  // Under the new gRPC CLI, the entire JSON structure is a wrapper:
  // { "ok": true, "message": [...], "output": [...] }
  cJSON *ok_obj = cJSON_GetObjectItemCaseSensitive(json, "ok");
  if (!ok_obj || !cJSON_IsBool(ok_obj) || !ok_obj->valueint) {
    fprintf(stderr, "ERROR: Measurement output marked ok: false\n");
    cJSON_Delete(json);
    exit(1);
  }

  cJSON *output_arr = cJSON_GetObjectItemCaseSensitive(json, "output");
  if (!output_arr || !cJSON_IsArray(output_arr)) {
    fprintf(stderr, "ERROR: Invalid output format from script server (no output array)\n");
    cJSON_Delete(json);
    exit(1);
  }

  // Find the last item in output array that contains the results key (MeasureJobResultResponse)
  cJSON *job_result_obj = NULL;
  int output_size = cJSON_GetArraySize(output_arr);
  for (int i = output_size - 1; i >= 0; --i) {
    cJSON *temp = cJSON_GetArrayItem(output_arr, i);
    if (temp && cJSON_GetObjectItemCaseSensitive(temp, "results")) {
      job_result_obj = temp;
      break;
    }
  }

  if (!job_result_obj) {
    fprintf(stderr, "ERROR: No MeasureJobResultResponse found in output stream\n");
    cJSON_Delete(json);
    exit(1);
  }

  // 4. Allocate and construct your native target matrix metadata wrapper
  Result *res = calloc(1, sizeof(Result));
  if (!res) {
    cJSON_Delete(json);
    return NULL;
  }

  cJSON *status_obj = cJSON_GetObjectItemCaseSensitive(job_result_obj, "status");
  cJSON *results_arr = cJSON_GetObjectItemCaseSensitive(job_result_obj, "results");

  if (status_obj && cJSON_IsString(status_obj) && 
      (strcmp(status_obj->valuestring, "JOB_STATUS_COMPLETED") == 0 ||
       strcmp(status_obj->valuestring, "3") == 0)) {
    res->status = strdup("success");
  } else {
    res->status = strdup("failure");
  }
  cJSON *script_obj = cJSON_GetObjectItemCaseSensitive(job_result_obj, "script");
  if (!script_obj) {
    script_obj = cJSON_GetObjectItemCaseSensitive(json, "script");
  }
  res->script_name = strdup((script_obj && cJSON_IsString(script_obj)) ? script_obj->valuestring : "unknown");

  if (!cJSON_IsArray(results_arr)) {
    res->step_count = 0;
    res->steps = NULL;
    cJSON_Delete(json);
    return (const Result *)res;
  }

  res->step_count = cJSON_GetArraySize(results_arr);
  res->steps = calloc(res->step_count, sizeof(StepResult));

  // 5. Serialize array indices over individual element blocks cleanly
  int idx = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, results_arr) {
    if (idx >= res->step_count)
      break;

    cJSON *c_inst = cJSON_GetObjectItemCaseSensitive(item, "instrumentName");
    cJSON *c_verb = cJSON_GetObjectItemCaseSensitive(item, "verb");
    cJSON *c_time = cJSON_GetObjectItemCaseSensitive(item, "executedAt");
    cJSON *c_param_arr = cJSON_GetObjectItemCaseSensitive(item, "param");

    StepResult *step = &res->steps[idx];
    cJSON *c_idx = cJSON_GetObjectItemCaseSensitive(item, "index");
    step->index = (c_idx && cJSON_IsNumber(c_idx)) ? c_idx->valueint : idx;
    step->instrument = strdup((c_inst && cJSON_IsString(c_inst)) ? c_inst->valuestring : "");
    step->verb = strdup((c_verb && cJSON_IsString(c_verb)) ? c_verb->valuestring : "");
    step->params_json = strdup("{}"); // Legacy parameter payload (no longer populated in results)

    // Parse executedAt timestamp
    if (c_time && cJSON_IsString(c_time)) {
      int y = 0, mon = 0, d = 0, hr = 0, min = 0;
      float fsec = 0.0f;
      if (sscanf(c_time->valuestring, "%d-%d-%dT%d:%d:%f", &y, &mon, &d, &hr, &min, &fsec) == 6) {
        struct tm tm_val = {0};
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = mon - 1;
        tm_val.tm_mday = d;
        tm_val.tm_hour = hr;
        tm_val.tm_min = min;
        tm_val.tm_sec = (int)fsec;
        time_t epoch_sec = mktime(&tm_val);
        step->executed_at_ms = (uint64_t)epoch_sec * 1000 + (uint64_t)((fsec - (int)fsec) * 1000);
      } else {
        step->executed_at_ms = 0;
      }
    } else {
      step->executed_at_ms = 0;
    }

    step->return_count = 0;
    step->returns = NULL;

    if (c_param_arr && cJSON_IsArray(c_param_arr)) {
      int param_size = cJSON_GetArraySize(c_param_arr);
      if (param_size > 0) {
        step->returns = calloc(param_size, sizeof(StepReturn));
        if (step->returns) {
          int actual_returns = 0;
          for (int p_idx = 0; p_idx < param_size; ++p_idx) {
            cJSON *p_item = cJSON_GetArrayItem(c_param_arr, p_idx);
            if (!p_item) continue;
            cJSON *p_name = cJSON_GetObjectItemCaseSensitive(p_item, "name");
            cJSON *p_type = cJSON_GetObjectItemCaseSensitive(p_item, "type");
            cJSON *p_val = cJSON_GetObjectItemCaseSensitive(p_item, "value");

            if (p_name && cJSON_IsString(p_name) && p_type && cJSON_IsString(p_type) && p_val) {
              StepReturn *ret = &step->returns[actual_returns];
              ret->name = strdup(p_name->valuestring);
              const char *type_str = p_type->valuestring;

              ret->type = VAL_TYPE_VOID;
              if (strcmp(type_str, "LUA_TYPES_DOUBLE") == 0 || strcmp(type_str, "LUA_TYPES_DOUBLE_ARRAY") == 0) {
                ret->type = VAL_TYPE_DOUBLE;
                cJSON *d_obj = cJSON_GetObjectItemCaseSensitive(p_val, "d");
                ret->value.d_val = (d_obj && cJSON_IsNumber(d_obj)) ? d_obj->valuedouble : 0.0;
              } else if (strcmp(type_str, "LUA_TYPES_INT64") == 0) {
                ret->type = VAL_TYPE_INT64;
                cJSON *i_obj = cJSON_GetObjectItemCaseSensitive(p_val, "i");
                ret->value.i_val = (i_obj && cJSON_IsNumber(i_obj)) ? (int64_t)i_obj->valuedouble : 0;
              } else if (strcmp(type_str, "LUA_TYPES_BOOL") == 0) {
                ret->type = VAL_TYPE_BOOL;
                cJSON *b_obj = cJSON_GetObjectItemCaseSensitive(p_val, "b");
                ret->value.b_val = (b_obj && cJSON_IsBool(b_obj)) ? b_obj->valueint : false;
              } else if (strcmp(type_str, "LUA_TYPES_STRING") == 0) {
                ret->type = VAL_TYPE_STRING;
                cJSON *s_obj = cJSON_GetObjectItemCaseSensitive(p_val, "s");
                ret->value.s_val = strdup((s_obj && cJSON_IsString(s_obj)) ? s_obj->valuestring : "");
              } else if (strcmp(type_str, "LUA_TYPES_DATA_BUFFER") == 0) {
                ret->type = VAL_TYPE_BUFFER;
                cJSON *s_obj = cJSON_GetObjectItemCaseSensitive(p_val, "s");
                cJSON *dbmeta = cJSON_GetObjectItemCaseSensitive(p_item, "dbmeta");

                BufferReturn *b_ret = &ret->value.buf_val;
                b_ret->buffer_id = strdup((s_obj && cJSON_IsString(s_obj)) ? s_obj->valuestring : "");

                size_t el_count = 0;
                const char *dt_str = "void";
                if (dbmeta) {
                  cJSON *c_buf_count = cJSON_GetObjectItemCaseSensitive(dbmeta, "elementCount");
                  cJSON *c_buf_type = cJSON_GetObjectItemCaseSensitive(dbmeta, "dataType");
                  if (c_buf_count && cJSON_IsNumber(c_buf_count)) {
                    el_count = c_buf_count->valueint;
                  }
                  if (c_buf_type && cJSON_IsNumber(c_buf_type)) {
                    int dt_val = c_buf_type->valueint;
                    if (dt_val == 0) dt_str = "uint8";
                    else if (dt_val == 1) dt_str = "float32";
                    else if (dt_val == 2) dt_str = "float64";
                    else if (dt_val == 3) dt_str = "int32";
                    else if (dt_val == 4) dt_str = "int64";
                    else if (dt_val == 5) dt_str = "uint32";
                    else if (dt_val == 6) dt_str = "uint64";
                  }
                }
                b_ret->element_count = el_count;
                b_ret->data_type = strdup(dt_str);
              }
              actual_returns++;
            }
          }
          step->return_count = actual_returns;
        }
      }
    }
    idx++;
  }

  cJSON_Delete(json);
  return (const Result *)res;
}

ISA_TEST_UTILS_EXPORT void free_result(const Result *res) {
  if (!res)
    return;

  // Cast away the read-only constraint inside destructor safely
  Result *m = (Result *)res;
  free(m->status);
  free(m->script_name);

  if (m->steps) {
    for (int i = 0; i < m->step_count; i++) {
      StepResult *step = &m->steps[i];
      free(step->instrument);
      free(step->verb);
      free(step->params_json);

      if (step->returns) {
        for (int r_idx = 0; r_idx < step->return_count; r_idx++) {
          StepReturn *ret = &step->returns[r_idx];
          free(ret->name);
          if (ret->type == VAL_TYPE_STRING) {
            free(ret->value.s_val);
          } else if (ret->type == VAL_TYPE_BUFFER) {
            free(ret->value.buf_val.buffer_id);
            free(ret->value.buf_val.data_type);
          }
        }
        free(step->returns);
      }
    }
    free(m->steps);
  }
  free(m);
}
bool map_string_to_array_type(const char *str, ArrayType *out_type,
                              size_t *out_size) {
  if (strcmp(str, "float32") == 0) {
    *out_type = INST_DATA_FLOAT32;
    *out_size = sizeof(float);
    return true;
  }
  if (strcmp(str, "float64") == 0) {
    *out_type = INST_DATA_FLOAT64;
    *out_size = sizeof(double);
    return true;
  }
  if (strcmp(str, "int32") == 0) {
    *out_type = INST_DATA_INT32;
    *out_size = sizeof(int32_t);
    return true;
  }
  if (strcmp(str, "int64") == 0) {
    *out_type = INST_DATA_INT64;
    *out_size = sizeof(int64_t);
    return true;
  }
  if (strcmp(str, "uint32") == 0) {
    *out_type = INST_DATA_UINT32;
    *out_size = sizeof(uint32_t);
    return true;
  }
  if (strcmp(str, "uint64") == 0) {
    *out_type = INST_DATA_UINT64;
    *out_size = sizeof(uint64_t);
    return true;
  }
  if (strcmp(str, "uint8") == 0) {
    *out_type = INST_DATA_UINT8;
    *out_size = sizeof(uint8_t);
    return true;
  }
  return false;
}

ISA_TEST_UTILS_EXPORT const buffer *read_buffer(const char *buffer_id) {
  if (!buffer_id)
    return NULL;

  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "buffer";
  const char *subcmd = "read";
  const char *json_flag = "--json";

  utarray_push_back(args, &cmd);
  utarray_push_back(args, &subcmd);
  utarray_push_back(args, &buffer_id);
  utarray_push_back(args, &json_flag);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Buffer read failed: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown error");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }
  free(r.stderr_data);

  if (!r.stdout_data || strlen(r.stdout_data) == 0) {
    fprintf(stderr, "ERROR: Buffer read returned empty stdout\n");
    free(r.stdout_data);
    exit(1);
  }

  cJSON *json = cJSON_Parse(r.stdout_data);
  free(r.stdout_data);

  if (!json) {
    fprintf(stderr, "ERROR: Failed to parse buffer output JSON.\n");
    exit(1);
  }

  // Under the new gRPC CLI, the entire JSON structure is a wrapper:
  // { "ok": true, "message": [...], "output": [...] }
  cJSON *ok_obj = cJSON_GetObjectItemCaseSensitive(json, "ok");
  if (!ok_obj || !cJSON_IsBool(ok_obj) || !ok_obj->valueint) {
    fprintf(stderr, "ERROR: Buffer response marked as failed or missing 'ok'\n");
    cJSON_Delete(json);
    exit(1);
  }

  cJSON *output_arr = cJSON_GetObjectItemCaseSensitive(json, "output");
  if (!output_arr || !cJSON_IsArray(output_arr)) {
    fprintf(stderr, "ERROR: Invalid buffer output format (no output array)\n");
    cJSON_Delete(json);
    exit(1);
  }

  cJSON *buffer_data_obj = NULL;
  int output_size = cJSON_GetArraySize(output_arr);
  for (int i = 0; i < output_size; ++i) {
    cJSON *temp = cJSON_GetArrayItem(output_arr, i);
    if (temp && cJSON_GetObjectItemCaseSensitive(temp, "buffer_id") &&
        cJSON_GetObjectItemCaseSensitive(temp, "data")) {
      buffer_data_obj = temp;
      break;
    }
  }

  if (!buffer_data_obj) {
    fprintf(stderr, "ERROR: Buffer data payload not found in CLI output wrapper\n");
    cJSON_Delete(json);
    exit(1);
  }

  cJSON *id_obj = cJSON_GetObjectItemCaseSensitive(buffer_data_obj, "buffer_id");
  cJSON *count_obj = cJSON_GetObjectItemCaseSensitive(buffer_data_obj, "element_count");
  cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(buffer_data_obj, "data_type");
  cJSON *data_array = cJSON_GetObjectItemCaseSensitive(buffer_data_obj, "data");

  if (!cJSON_IsString(id_obj) || !cJSON_IsNumber(count_obj) ||
      !cJSON_IsString(type_obj) || !cJSON_IsArray(data_array)) {
    fprintf(stderr,
            "ERROR: JSON fields missing or corrupted in buffer response\n");
    cJSON_Delete(json);
    exit(1);
  }

  ArrayType mapped_type;
  size_t element_size = 0;
  if (!map_string_to_array_type(type_obj->valuestring, &mapped_type,
                                &element_size)) {
    fprintf(stderr, "ERROR: Unsupported buffer array data type string: %s\n",
            type_obj->valuestring);
    cJSON_Delete(json);
    exit(1);
  }

  buffer *buf_res = calloc(1, sizeof(buffer));
  if (!buf_res) {
    cJSON_Delete(json);
    return NULL;
  }

  buf_res->buffer_id = strdup(id_obj->valuestring);
  buf_res->element_count = count_obj->valueint;
  buf_res->data_type = mapped_type;

  // Allocate continuous heap memory for the parsed data block array
  buf_res->data = malloc(buf_res->element_count * element_size);
  if (!buf_res->data) {
    cJSON_Delete(json);
    free(buf_res->buffer_id);
    free(buf_res);
    return NULL;
  }

  // Iterate over the JSON array elements and parse directly into destination pointers
  int i = 0;
  cJSON *element = NULL;
  cJSON_ArrayForEach(element, data_array) {
    if (i >= buf_res->element_count)
      break;
    if (!cJSON_IsNumber(element))
      continue;

    switch (buf_res->data_type) {
    case INST_DATA_FLOAT32:
      ((float *)buf_res->data)[i] = (float)element->valuedouble;
      break;
    case INST_DATA_FLOAT64:
      ((double *)buf_res->data)[i] = element->valuedouble;
      break;
    case INST_DATA_INT32:
      ((int32_t *)buf_res->data)[i] = (int32_t)element->valueint;
      break;
    case INST_DATA_INT64:
      ((int64_t *)buf_res->data)[i] = (int64_t)element->valuedouble;
      break; // valuedouble protects 53-bits safely
    case INST_DATA_UINT32:
      ((uint32_t *)buf_res->data)[i] = (uint32_t)element->valuedouble;
      break;
    case INST_DATA_UINT64:
      ((uint64_t *)buf_res->data)[i] = (uint64_t)element->valuedouble;
      break;
    case INST_DATA_UINT8:
      ((uint8_t *)buf_res->data)[i] = (uint8_t)element->valueint;
      break;
    }
    i++;
  }

  cJSON_Delete(json);
  return (const buffer *)buf_res;
}

ISA_TEST_UTILS_EXPORT void free_buffer(const buffer *buf) {
  if (!buf)
    return;
  free((void *)buf->buffer_id);
  free((void *)buf->data);
  free((void *)buf);
}

ISA_TEST_UTILS_EXPORT void release_buffer(const char *buffer_id) {
  if (!buffer_id)
    return;

  UT_array *args;
  utarray_new(args, &ut_str_icd);

  const char *cmd = "buffer";
  const char *subcmd = "release";

  utarray_push_back(args, &cmd);
  utarray_push_back(args, &subcmd);
  utarray_push_back(args, &buffer_id);

  ProcessResult r = run_iss(args);
  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr, "ERROR: Remote release-buffer call failed: %s\n",
            r.stderr_data ? r.stderr_data : "Unknown IPC error");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }

  free(r.stdout_data);
  free(r.stderr_data);
}
char *flatten_yaml(const char *format, ...) {
  va_list args;
  va_start(args, format);

  // Predict exactly how many bytes are needed for the string format layout
  va_list args_copy;
  va_copy(args_copy, args);
  int len = vsnprintf(NULL, 0, format, args_copy);
  va_end(args_copy);

  if (len < 0) {
    va_end(args);
    return NULL;
  }

  char *str = malloc(len + 1);
  if (str) {
    vsnprintf(str, len + 1, format, args);
  }

  va_end(args);
  return str;
}
char *name_with_index(const char *base, int index) {
  return flatten_yaml("%s:%d", base, index);
}
