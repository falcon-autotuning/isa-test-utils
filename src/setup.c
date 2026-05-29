#include "isa-test-utils/setup.h"
#include "isa-test-utils/run.h"
#include "utarray.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define access _access
#define F_OK 0
#define strdup _strdup
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* Recursively delete a directory and all of its contents */
static void remove_dir_recursive(const char *path) {
  if (!path || strlen(path) == 0)
    return;

#ifdef _WIN32
  // --- WINDOWS IMPLEMENTATION ---
  char search_path[MAX_PATH];
  snprintf(search_path, sizeof(search_path), "%s\\*", path);

  WIN32_FIND_DATAA find_data;
  HANDLE hFind = FindFirstFileA(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return; // Directory might not exist or can't be opened
  }

  do {
    // Skip the current (.) and parent (..) directory links
    if (strcmp(find_data.cFileName, ".") == 0 ||
        strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    char child_path[MAX_PATH];
    snprintf(child_path, sizeof(child_path), "%s\\%s", path,
             find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      remove_dir_recursive(child_path);
    } else {
      // Force remove files (even if read-only)
      SetFileAttributesA(child_path, FILE_ATTRIBUTE_NORMAL);
      DeleteFileA(child_path);
    }
  } while (FindNextFileA(hFind, &find_data));

  FindClose(hFind);
  RemoveDirectoryA(path);

#else
  // --- POSIX LINUX IMPLEMENTATION ---
  DIR *d = opendir(path);
  if (!d) {
    return; // Directory doesn't exist or can't be opened
  }

  struct dirent *p;
  while ((p = readdir(d))) {
    if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
      continue;
    }

    size_t len = strlen(path) + strlen(p->d_name) + 2;
    char *child_path = malloc(len);
    if (!child_path)
      continue;
    snprintf(child_path, len, "%s/%s", path, p->d_name);

    struct stat statbuf;
    if (stat(child_path, &statbuf) == 0) {
      if (S_ISDIR(statbuf.st_mode)) {
        remove_dir_recursive(child_path);
      } else {
        unlink(child_path);
      }
    }
    free(child_path);
  }
  closedir(d);
  rmdir(path);
#endif
}
static const char *get_system_tmp_dir(void) {
#ifdef _WIN32
  static char win_tmp[MAX_PATH];
  if (GetTempPathA(MAX_PATH, win_tmp) != 0) {
    return win_tmp;
  }
  return "C:\\Temp";
#else
  const char *tmp = getenv("TMPDIR");
  if (!tmp)
    tmp = getenv("TMP");
  if (!tmp)
    tmp = getenv("TEMP");
  if (!tmp)
    tmp = "/tmp";
  return tmp;
#endif
}

/**
 * Checks if a string ends with a specific suffix.
 * Returns true if it matches, false otherwise.
 */
static bool has_suffix(const char *str, const char *suffix) {
  if (!str || !suffix) {
    return false;
  }

  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (str_len < suffix_len) {
    return false;
  }
  return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

// Replaces g_file_test(path, G_FILE_TEST_EXISTS)
static bool file_exists(const char *path) {
  if (!path)
    return false;
  return access(path, F_OK) == 0;
}

// Replaces g_mkdir_with_parents
static bool create_dir_recursive(const char *path) {
  if (!path || strlen(path) == 0)
    return false;

  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);

  // Clean trailing slashes
  if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
    tmp[len - 1] = '\0';
  }

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char c = *p;
      *p = '\0'; // Temporarily truncate string
#ifdef _WIN32
      CreateDirectoryA(tmp, NULL);
#else
      mkdir(tmp, 0755);
#endif
      *p = c; // Restore character
    }
  }
#ifdef _WIN32
  return CreateDirectoryA(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(tmp, 0755) == 0 || errno == EEXIST;
#endif
}

// Replaces g_rmdir (wipes directory or files cross-platform)
static void remove_path(const char *path) {
  if (!path)
    return;
#ifdef _WIN32
  // Tries removing as a directory first, falls back to standard file unlinking
  if (!RemoveDirectoryA(path)) {
    DeleteFileA(path);
  }
#else
  if (rmdir(path) != 0) {
    unlink(path);
  }
#endif
}
// Helper function to replace all occurrences of a substring inside a
// heap-allocated buffer.
// It returns a newly allocated string with replacements applied, and frees the
// original input buffer.
static char *replace_substring_in_buffer(char *src, const char *find,
                                         const char *replace) {
  if (!src || !find || strlen(find) == 0)
    return src;
  if (!replace)
    replace = "";

  size_t find_len = strlen(find);
  size_t replace_len = strlen(replace);

  // Count occurrences
  size_t count = 0;
  char *pos = src;
  while ((pos = strstr(pos, find))) {
    count++;
    pos += find_len;
  }

  if (count == 0)
    return src; // No matches found, return unchanged

  // Calculate new length and allocate memory
  size_t src_len = strlen(src);
  size_t new_len = src_len + (replace_len - find_len) * count;
  char *dst = malloc(new_len + 1);
  if (!dst)
    return src;

  char *dst_ptr = dst;
  char *src_ptr = src;
  while ((pos = strstr(src_ptr, find))) {
    // Copy chunk before match
    size_t chunk_len = pos - src_ptr;
    memcpy(dst_ptr, src_ptr, chunk_len);
    dst_ptr += chunk_len;

    // Insert replacement string
    memcpy(dst_ptr, replace, replace_len);
    dst_ptr += replace_len;

    // Advance past source match
    src_ptr = pos + find_len;
  }

  // Copy remaining trailing characters
  strcpy(dst_ptr, src_ptr);

  free(src); // Free the original buffer
  return dst;
}

/* =========================================================================
   Path Buffer Implementation
   ========================================================================= */

ISA_TEST_UTILS_EXPORT PathBuffer *path_buf_new(const char *initial_path) {
  PathBuffer *buf = calloc(1, sizeof(PathBuffer));
  if (!buf)
    return NULL;

  // Allocate an initial capacity with a small safety buffer
  buf->capacity = initial_path ? strlen(initial_path) + 32 : 64;
  buf->path_str = malloc(buf->capacity);
  if (!buf->path_str) {
    free(buf);
    return NULL;
  }

  if (initial_path) {
    strcpy(buf->path_str, initial_path);
    buf->length = strlen(initial_path);
  } else {
    buf->path_str[0] = '\0';
    buf->length = 0;
  }
  return buf;
}

ISA_TEST_UTILS_EXPORT void path_buf_push(PathBuffer *buf, const char *element) {
  if (!buf || !element || strlen(element) == 0)
    return;

  size_t element_len = strlen(element);
  // Reserve extra space for directory separator, element text, and a null
  // terminator
  size_t needed = buf->length + element_len + 2;

  if (needed > buf->capacity) {
    buf->capacity = needed * 2;
    char *new_str = realloc(buf->path_str, buf->capacity);
    if (!new_str)
      return;
    buf->path_str = new_str;
  }

  // Append a trailing directory separator if the path string isn't empty
  // and doesn't already end with a slash boundary
  if (buf->length > 0 && buf->path_str[buf->length - 1] != '/' &&
      buf->path_str[buf->length - 1] != '\\') {
    buf->path_str[buf->length++] = DIR_SEPARATOR;
  }

  // Handle cross-platform path transformations (normalize inner slashes
  // seamlessly)
  for (size_t i = 0; i < element_len; i++) {
    char c = element[i];
    if (c == '/' || c == '\\') {
      buf->path_str[buf->length++] = DIR_SEPARATOR;
    } else {
      buf->path_str[buf->length++] = c;
    }
  }
  buf->path_str[buf->length] = '\0';
}

ISA_TEST_UTILS_EXPORT void path_buf_pop(PathBuffer *buf) {
  if (!buf || buf->length == 0)
    return;

  // Trim trailing separators if any exist to start clean
  while (buf->length > 0 && (buf->path_str[buf->length - 1] == '/' ||
                             buf->path_str[buf->length - 1] == '\\')) {
    buf->length--;
  }

  // Scan backwards until we hit the parent separator boundary
  size_t i = buf->length;
  while (i > 0) {
    char c = buf->path_str[i - 1];
    if (c == '/' || c == '\\') {
      buf->length = i - 1;

      // Handle the case where the parent is the filesystem root (e.g. "/" or
      // "C:\")
      if (buf->length == 0) {
        buf->length = 1; // Preserve the root "/" separator
      }
#ifdef _WIN32
      if (buf->length == 2 && buf->path_str[1] == ':') {
        buf->length = 3; // Preserve "C:\" separator rules on Windows
      }
#endif
      buf->path_str[buf->length] = '\0';
      return;
    }
    i--;
  }

  // If no directory separator was encountered, the buffer was a flat name;
  // clear it.
  buf->length = 0;
  buf->path_str[0] = '\0';
}

ISA_TEST_UTILS_EXPORT void path_buf_set_extension(PathBuffer *buf,
                                                  const char *ext) {
  if (!buf || buf->length == 0)
    return;

  // 1. Strip away any existing extension by rewinding to the last '.'
  for (size_t i = buf->length; i > 0; i--) {
    char c = buf->path_str[i - 1];
    if (c == '/' || c == '\\')
      break; // Hit folder boundary, no extension exists
    if (c == '.') {
      buf->length = i - 1; // Slice string here
      buf->path_str[buf->length] = '\0';
      break;
    }
  }

  if (!ext)
    return;

  // 2. Append new extension
  size_t ext_len = strlen(ext);
  size_t offset =
      (ext[0] == '.') ? 0 : 1; // Check if we need to manually inject a '.'
  size_t needed = buf->length + ext_len + offset + 1;

  if (needed > buf->capacity) {
    buf->capacity = needed + 16;
    char *new_str = realloc(buf->path_str, buf->capacity);
    if (!new_str)
      return;
    buf->path_str = new_str;
  }

  if (offset == 1) {
    buf->path_str[buf->length++] = '.';
  }
  strcpy(buf->path_str + buf->length, ext);
  buf->length += ext_len;
}

ISA_TEST_UTILS_EXPORT char *path_buf_free_to_path(PathBuffer *buf) {
  if (!buf)
    return NULL;
  char *final_path = buf->path_str;
  free(buf);
  return final_path; // Caller takes ownership and must free() this string later
}

/* ================================
   Dependency Injection
================================ */

static SetupConfig g_setup_config = {.template_expander_path =
                                         "template-expander"};

void setup_set_config(const SetupConfig *cfg) {
  if (cfg)
    g_setup_config = *cfg;
}

static bool is_template_file(const char *path) {
  return has_suffix(path, ".tmpl");
}

static void run_template_expander(const char *in, const char *out) {
  UT_array *args;
  utarray_new(args, &ut_str_icd);

  utarray_push_back(args, &in);
  utarray_push_back(args, &out);

  ProcessResult r = run_executable(g_setup_config.template_expander_path, args);
  utarray_free(args);

  if (r.exit_code != 0) {
    fprintf(stderr,
            "FATAL ERROR: Template expander "
            "failed\ninput=%s\noutput=%s\nstderr=%s\n",
            in, out, r.stderr_data ? r.stderr_data : "Unknown");
    free(r.stdout_data);
    free(r.stderr_data);
    exit(1);
  }

  free(r.stdout_data);
  free(r.stderr_data);
}

char *prepare_isa_directory(void *files, PathBuffer *root) {
  UT_array *file_array = (UT_array *)files;
  if (!root)
    return NULL;

  PathBuffer *path = path_buf_new(root->path_str);
  path_buf_push(path, "isa");

  char *dst = path_buf_free_to_path(path);

  /* clean existing */
  if (file_exists(dst)) {
    remove_path(dst);
  }

  create_dir_recursive(dst);

  /* extract all embedded files */
  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    // Extract the element pointer from the sequential UT_array configuration
    // matrix
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    PathBuffer *out_pb = path_buf_new(dst);
    path_buf_push(out_pb, f->relative_path);
    char *out_path = path_buf_free_to_path(out_pb);

    PathBuffer *dir_pb = path_buf_new(out_path);
    path_buf_pop(dir_pb);
    char *dir = path_buf_free_to_path(dir_pb);

    create_dir_recursive(dir);
    free(dir);

    FILE *f_out = fopen(out_path, "wb");
    if (f_out) {
      if (f->size > 0) {
        fwrite(f->data, 1, f->size, f_out);
      }
      fclose(f_out);
    }

    /* handle .tmpl components */
    if (has_suffix(out_path, ".tmpl")) {
      char *expanded = strdup(out_path);
      if (expanded) {
        size_t exp_len = strlen(expanded);
        if (exp_len > 5) {
          expanded[exp_len - 5] =
              '\0'; // Slice off the ".tmpl" extension safely

          run_template_expander(out_path, expanded);
          remove_path(out_path); // Wipe raw template source file
        }
        free(expanded);
      }
    }

    free(out_path);
  }

  return dst;
}

char *prepare_config_directory(void *files, PathBuffer *root,
                               void *replacements) {
  UT_array *file_array = (UT_array *)files;
  UT_array *repl_array = (UT_array *)replacements;
  if (!root)
    return NULL;

  PathBuffer *path = path_buf_new(root->path_str);
  path_buf_push(path, "config");
  char *dst = path_buf_free_to_path(path);

  if (file_exists(dst)) {
    remove_path(dst);
  }
  create_dir_recursive(dst);

  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    PathBuffer *out_pb = path_buf_new(dst);
    path_buf_push(out_pb, f->relative_path);
    char *out_path = path_buf_free_to_path(out_pb);

    PathBuffer *dir_pb = path_buf_new(out_path);
    path_buf_pop(dir_pb);
    char *dir = path_buf_free_to_path(dir_pb);
    create_dir_recursive(dir);
    free(dir);

    FILE *f_out = fopen(out_path, "wb");
    if (f_out) {
      if (f->size > 0) {
        fwrite(f->data, 1, f->size, f_out);
      }
      fclose(f_out);
    }

    /* handle .in templates */
    if (has_suffix(out_path, ".in")) {

      FILE *f_in = fopen(out_path, "rb");
      if (!f_in) {
        free(out_path);
        continue;
      }

      fseek(f_in, 0, SEEK_END);
      long file_size = ftell(f_in);
      fseek(f_in, 0, SEEK_SET);

      char *contents = malloc(file_size + 1);
      if (contents) {
        size_t read_bytes = fread(contents, 1, file_size, f_in);
        contents[read_bytes] = '\0';
      }
      fclose(f_in);

      if (contents) {
        unsigned int num_repls = repl_array ? utarray_len(repl_array) : 0;
        for (unsigned int j = 0; j < num_repls; j++) {
          PairString **p_ptr = (PairString **)utarray_eltptr(repl_array, j);
          if (!p_ptr || !(*p_ptr))
            continue;
          PairString *p = *p_ptr;

          // Mutates and scales text memory block internally
          contents = replace_substring_in_buffer(contents, p->first, p->second);
        }

        char *final_path = strdup(out_path);
        if (final_path) {
          size_t len = strlen(final_path);
          if (len > 3) {
            final_path[len - 3] = '\0'; // Remove ".in"
          }

          // Write final evaluated string buffer out to disk
          FILE *f_final = fopen(final_path, "wb");
          if (f_final) {
            size_t contents_len = strlen(contents);
            if (contents_len > 0) {
              fwrite(contents, 1, contents_len, f_final);
            }
            fclose(f_final);
          }
          free(final_path);
        }
        free(contents);
      }

      remove_path(out_path);
    }

    free(out_path);
  }

  return dst;
}
char *prepare_plugin_directory(void *files, PathBuffer *root) {
  UT_array *file_array = (UT_array *)files;
  if (!root)
    return NULL;

  PathBuffer *path = path_buf_new(root->path_str);
  path_buf_push(path, "plugins");
  char *dst = path_buf_free_to_path(path);

  if (file_exists(dst)) {
    remove_path(dst);
  }
  create_dir_recursive(dst);

  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    PathBuffer *out_pb = path_buf_new(dst);
    path_buf_push(out_pb, f->relative_path);
    char *out_path = path_buf_free_to_path(out_pb);

    PathBuffer *dir_pb = path_buf_new(out_path);
    path_buf_pop(dir_pb);
    char *dir = path_buf_free_to_path(dir_pb);
    create_dir_recursive(dir);
    free(dir);

    FILE *f_out = fopen(out_path, "wb");
    if (!f_out) {
      fprintf(stderr, "FATAL ERROR: Failed to write plugin file: %s\n",
              out_path);
      free(out_path);
      exit(1);
    }
    if (f->size > 0) {
      fwrite(f->data, 1, f->size, f_out);
    }
    fclose(f_out);

#ifndef _WIN32
    chmod(out_path, 0755);
#endif

    free(out_path);
  }

  return dst;
}

PrepareEnvironmentResult
prepare_full_environment_bundle(const EmbeddedBundle *bundle,
                                void *replacements) {
  PrepareEnvironmentResult result = {0};

  /* create root dir */
  PathBuffer *root = path_buf_new(get_system_tmp_dir());
  path_buf_push(root, "test_env");

  // Keep a copy of the path string for tracking inside the result layout
  result.root_dir = strdup(root->path_str);
  create_dir_recursive(result.root_dir);

  if (!bundle->isa_files) {
    fprintf(stderr, "FATAL ERROR: Missing ISA files\n");
    free(result.root_dir);
    exit(1);
  }

  result.isa_dir = prepare_isa_directory(bundle->isa_files, root);

  if (!bundle->config_files) {
    fprintf(stderr, "FATAL ERROR: Missing config files\n");
    free(result.root_dir);
    free(result.isa_dir);
    exit(1);
  }

  void *local_repl = replacements;
  bool need_free = false;

  if (!local_repl) {
    UT_array *empty_repl;
    utarray_new(
        empty_repl,
        &ut_str_icd); // Assuming string-based or pair tracker format matches
    local_repl = (void *)empty_repl;
    need_free = true;
  }

  result.config_dir =
      prepare_config_directory(bundle->config_files, root, local_repl);

  /* =============================
     Plugin (optional)
  ============================== */
  unsigned int num_plugins =
      bundle->plugin_files ? utarray_len((UT_array *)bundle->plugin_files) : 0;
  if (bundle->plugin_files && num_plugins > 0) {
    result.plugin_dir = prepare_plugin_directory(bundle->plugin_files, root);
  } else {
    result.plugin_dir = NULL;
  }

  free(root->path_str);
  free(root);

  if (need_free) {
    utarray_free((UT_array *)local_repl);
  }

  return result;
}

void cleanup_environment(PrepareEnvironmentResult *env) {
  if (!env)
    return;

  if (env->root_dir) {
    remove_dir_recursive(env->root_dir);
  }

  free(env->isa_dir);
  free(env->config_dir);
  free(env->plugin_dir);
  free(env->root_dir);

  // Optional zeroing out to prevent accidental double-free bugs later
  env->isa_dir = NULL;
  env->config_dir = NULL;
  env->plugin_dir = NULL;
  env->root_dir = NULL;
}

/* ================================
   Env + Utils
================================ */

char *get_required_env_string(const char *name) {
  // Replaces g_getenv with standard C library getenv
  const char *val = getenv(name);

  if (!val || *val == '\0') {
    fprintf(stderr, "FATAL ERROR: Missing env var: %s\n", name);
    exit(1); // Mimics g_error abortion behavior
  }

  return strdup(val);
}

int get_required_env_int(const char *name) {
  char *val = get_required_env_string(name);

  char *end;
  long x = strtol(val, &end, 10);

  if (*end != '\0') {
    fprintf(stderr, "FATAL ERROR: Invalid integer env var: %s\n", name);
    free(val);
    exit(1);
  }

  free(val);
  return (int)x;
}

char *yaml_quote(const char *value) {
  if (!value)
    return strdup("''");

  size_t single_quotes = 0;
  size_t len = 0;
  for (const char *p = value; *p; p++) {
    if (*p == '\'')
      single_quotes++;
    len++;
  }

  size_t allocation_size = len + single_quotes + 2 + 1;
  char *out = malloc(allocation_size);
  if (!out)
    return NULL;

  char *dst = out;
  *dst++ = '\''; // Opening quote

  for (const char *p = value; *p; p++) {
    if (*p == '\'') {
      *dst++ = '\''; // First quote (escape char)
      *dst++ = '\''; // Second quote (literal payload)
    } else {
      *dst++ = *p;
    }
  }

  *dst++ = '\''; // Closing quote
  *dst = '\0';   // Null terminator

  return out;
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
