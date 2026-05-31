#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "isa-test-utils.h"
#include "utarray.h"
#include "internal/prepare_directories.h"
#include "internal/exec.h"
#include "internal/path.h"
#include "isa-test-utils/embed_runtime.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define access _access
#define F_OK 0
#define strdup _strdup
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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
typedef struct {
  char *first;
  char *second;
} PairString;
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
static bool is_template_file(const char *path) {
  return has_suffix(path, ".tmpl");
}

// Concrete implementation hidden from public header consumers
struct Replacements {
  UT_array *pairs;
};

// Internal destructor callback to automatically wipe pairs
static void free_pair_cb(void *elt) {
  PairString *p = *(PairString **)elt;
  if (!p)
    return;
  free(p->first);
  free(p->second);
  free(p);
}

static const UT_icd pair_icd = {sizeof(PairString *), NULL, NULL, free_pair_cb};

Replacements *replacements_new(void) {
  Replacements *r = calloc(1, sizeof(Replacements));
  if (!r)
    return NULL;
  utarray_new(r->pairs, &pair_icd);
  return r;
}

void replacements_free(Replacements *repls) {
  if (!repls)
    return;
  // Deep-frees every PairString and their strings automatically via pair_icd
  utarray_free(repls->pairs);
  free(repls);
}

void replacements_add(Replacements *repls, const char *key, const char *value) {
  if (!repls || !key || !value)
    return;

  PairString *p = calloc(1, sizeof(PairString));
  if (!p)
    return;

  p->first = strdup(key);
  char *temp = yaml_quote(value);
  p->second = strdup(temp);
  free(temp);
  utarray_push_back(repls->pairs, &p);
}

static SetupConfig g_setup_config = {.template_expander_path =
                                         "template-expander"};

void setup_set_config(const SetupConfig *cfg) {
  if (cfg)
    g_setup_config = *cfg;
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
// Internal bridge helper used inside prepare_full_environment_bundle
// to let it iterate over the opaque structure fields natively
void *replacements_get_internal_array(const Replacements *repls) {
  return repls ? (void *)repls->pairs : NULL;
}
Path *prepare_isa_directory(void *files, Path *root) {
  UT_array *file_array = (UT_array *)files;
  if (!root)
    return NULL;

  Path *dst = path_clone(root);
  path_push(dst, "isa");

  /* clean existing */
  if (path_file_exists(dst)) {
    path_remove_path(dst);
  }

  path_create_dir_recursive(dst);

  /* extract all embedded files */
  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    // Extract the element pointer from the sequential UT_array configuration
    // matrix
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    Path *out_pb = path_clone(dst);
    path_push(out_pb, f->relative_path);
    char *out_path = path_free_to_path(out_pb);

    Path *dir_pb = path_new(out_path);
    path_pop(dir_pb);
    path_create_dir_recursive(dir_pb);
    path_free(dir_pb);

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
Path *prepare_config_directory(void *files, Path *root,
                               const Replacements *replacements) {
  UT_array *file_array = (UT_array *)files;

  UT_array *repl_array =
      (UT_array *)replacements_get_internal_array(replacements);
  if (!root)
    return NULL;

  Path *dst = path_clone(root);
  path_push(dst, "config");

  if (path_file_exists(dst)) {
    path_remove_path(dst);
  }
  path_create_dir_recursive(dst);

  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    Path *out_pb = path_clone(dst);
    path_push(out_pb, f->relative_path);
    Path *dir_pb = path_clone(out_pb);
    path_pop(dir_pb);

    char *out_path = path_free_to_path(out_pb);
    char *dir = path_free_to_path(dir_pb);
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
Path *prepare_plugin_directory(void *files, Path *root) {
  UT_array *file_array = (UT_array *)files;
  if (!root)
    return NULL;

  Path *dst = path_clone(root);
  path_push(dst, "plugins");

  if (path_file_exists(dst)) {
    path_remove_path(dst);
  }
  path_create_dir_recursive(dst);

  unsigned int num_files = file_array ? utarray_len(file_array) : 0;
  for (unsigned int i = 0; i < num_files; i++) {
    EmbeddedFile **f_ptr = (EmbeddedFile **)utarray_eltptr(file_array, i);
    if (!f_ptr || !(*f_ptr))
      continue;
    EmbeddedFile *f = *f_ptr;

    Path *out_pb = path_clone(dst);
    path_push(out_pb, f->relative_path);
    char *out_path = path_free_to_path(out_pb);

    Path *dir_pb = path_new(out_path);
    path_pop(dir_pb);
    char *dir = path_free_to_path(dir_pb);
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

EnvLocations _prepare_environment(EmbeddedYamls eya, Replacements reps) {
  EnvLocations result = {0};

  /* create root dir */
  Path *root = path_new(get_system_tmp_dir());
  path_push(root, "test_env");

  // Keep a copy of the path string for tracking inside the result layout
  Path *path = path_clone(root);
  create_dir_recursive(root->path_str);

  if (!eya.isa_files) {
    fprintf(stderr, "FATAL ERROR: Missing ISA files\n");
    free(result.root_dir);
    exit(1);
  }

  result.isa_dir = prepare_isa_directory(eya.isa_files, root);

  if (!eya.config_files) {
    fprintf(stderr, "FATAL ERROR: Missing config files\n");
    free(result.root_dir);
    free(result.isa_dir);
    exit(1);
  }

  result.config_dir = prepare_config_directory(eya.config_files, root, &reps);

  /* =============================
     Plugin (optional)
  ============================== */
  unsigned int num_plugins =
      eya.plugin_files ? utarray_len((UT_array *)eya.plugin_files) : 0;
  if (eya.plugin_files && num_plugins > 0) {
    result.plugin_dir = prepare_plugin_directory(eya.plugin_files, root);
  } else {
    result.plugin_dir = NULL;
  }
  path_free(root);

  replacements_free(&reps);
  embedded_yamls_free(&eya);

  return result;
}

void cleanup_environment(EnvLocations *env) {
  if (!env)
    return;

  if (env->root_dir) {
    path_remove_dir_recursive(env->root_dir);
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

void embedded_yamls_free(EmbeddedYamls *eya) {
  if (!eya)
    return;

  if (eya->isa_files) {
    utarray_free(eya->isa_files);
    eya->isa_files = NULL;
  }

  if (eya->config_files) {
    utarray_free(eya->config_files);
    eya->config_files = NULL;
  }

  if (eya->plugin_files) {
    utarray_free(eya->plugin_files);
    eya->plugin_files = NULL;
  }
}
