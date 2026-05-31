#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "internal/exec.h"
#include "isa-test-utils.h"
#include "internal/measurement-helpers.h"
#include "internal/prepare_directories.h"
#include "internal/path.h"
#include "utarray.h"

// Cross-platform environment and file access configurations
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define access _access
#define strdup _strdup
#define F_OK 0
#define setenv_cross(name, val) _putenv_s(name, val)
#define unsetenv_cross(name) _putenv_s(name, "")

#else
#include <unistd.h>
#define setenv_cross(name, val) setenv(name, val, 1)
#define unsetenv_cross(name) unsetenv(name)
#endif

/* =========================================================================
   Cross-Platform Core Testing Helpers
   ========================================================================= */

static const char *get_system_tmp_dir(void) {
#ifdef _WIN32
  static char win_tmp[MAX_PATH];
  if (GetTempPathA(MAX_PATH, win_tmp) != 0)
    return win_tmp;
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

static char *read_entire_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *contents = malloc(size + 1);
  if (contents) {
    size_t read_bytes = fread(contents, 1, size, f);
    contents[read_bytes] = '\0';
    if (out_len)
      *out_len = read_bytes;
  }
  fclose(f);
  return contents;
}

/* =========================================================================
   Struct Builders & Destructors
   ========================================================================= */

static EmbeddedFile *make_file(const char *path, const char *data) {
  EmbeddedFile *f = calloc(1, sizeof(EmbeddedFile));
  if (f) {
    f->relative_path = path;
    f->data = (const unsigned char *)strdup(data);
    f->size = strlen(data);
  }
  return f;
}

static void setup_expander(void) {
  setup_set_config(
      &(SetupConfig){.template_expander_path = MOCK_EXPANDER_PATH});
}

// Custom ICD hooks enabling UT_array to cleanly manage embedded structure
// pointers
static void free_embedded_file_cb(void *elt) {
  EmbeddedFile *f = *(EmbeddedFile **)elt;
  if (!f)
    return;
  free((void *)f->data);
  free(f);
}

static const UT_icd embedded_file_icd = {sizeof(EmbeddedFile *), NULL, NULL,
                                         free_embedded_file_cb};

/* =========================================================================
   ISA Tests
   ========================================================================= */

static void test_prepare_isa_basic(void **state) {
  (void)state;
  setup_expander();

  UT_array *files;
  utarray_new(files, &embedded_file_icd);

  EmbeddedFile *f = make_file("a.txt", "hello");
  utarray_push_back(files, &f);

  Path *root = path_new(get_system_tmp_dir());
  Path *dir = prepare_isa_directory(files, root);
  utarray_free(files);

  path_push(dir, "a.txt");
  char *out = path_free_to_path(dir);

  assert_true(file_exists(out));

  free(out);
  path_free(root);
}

static void test_prepare_isa_tmpl(void **state) {
  (void)state;
  setup_expander();

  UT_array *files;
  utarray_new(files, &embedded_file_icd);

  EmbeddedFile *f = make_file("x.yml.tmpl", "expand me");
  utarray_push_back(files, &f);

  Path *root = path_new(get_system_tmp_dir());
  Path *dir = prepare_isa_directory(files, root);
  utarray_free(files);

  Path *exp_pb = path_clone(dir);
  path_push(exp_pb, "x.yml");
  char *expanded = path_free_to_path(exp_pb);

  Path *tmpl_pb = path_clone(dir);
  path_push(tmpl_pb, "x.yml.tmpl");
  char *tmpl = path_free_to_path(tmpl_pb);

  assert_true(file_exists(expanded));
  assert_false(file_exists(tmpl));

  free(expanded);
  free(tmpl);
  path_free(dir);
  path_free(root);
}

/* =========================================================================
   Config Tests
   ========================================================================= */

static void test_config_replace(void **state) {
  (void)state;
  UT_array *files;
  utarray_new(files, &embedded_file_icd);
  EmbeddedFile *f = make_file("cfg.yml.in", "hello __NAME__");
  utarray_push_back(files, &f);

  Replacements *replacements = replacements_new();
  assert_non_null((void *)replacements);

  replacements_add(replacements, "__NAME__", "world");

  Path *root = path_new(get_system_tmp_dir());
  Path *dir = prepare_config_directory(files, root, replacements);

  utarray_free(files);
  replacements_free(
      replacements); // Deep-frees your pairs and keys automatically
  Path *out_pb = path_clone(dir);
  path_push(out_pb, "cfg.yml");
  char *out = path_free_to_path(out_pb);

  char *contents = read_entire_file(out, NULL);
  assert_non_null((void *)contents);
  assert_string_equal(contents, "hello 'world'");

  free(contents);
  free(out);
  path_free(dir);
  path_free(root);
}

/* =========================================================================
   Plugin Tests
   ========================================================================= */

static void test_plugin_extract(void **state) {
  (void)state;
  UT_array *files;
  utarray_new(files, &embedded_file_icd);

  EmbeddedFile *f = make_file("plugin.so", "binary");
  utarray_push_back(files, &f);

  Path *root = path_new(get_system_tmp_dir());
  Path *dir = prepare_plugin_directory(files, root);
  utarray_free(files);

  Path *out_pb = path_clone(dir);
  path_push(out_pb, "plugin.so");
  char *out = path_free_to_path(out_pb);

  assert_true(file_exists(out));

  free(out);
  path_free(dir);
  path_free(root);
}

/* =========================================================================
   Env + Utils
   ========================================================================= */

static void test_env_string(void **state) {
  (void)state;
  setenv_cross("X", "abc");

  char *v = get_required_env_string("X");
  assert_string_equal(v, "abc");

  unsetenv_cross("X");
  free(v);
}

static void test_env_int(void **state) {
  (void)state;
  setenv_cross("Y", "123");

  int v = get_required_env_int("Y");
  assert_int_equal(v, 123);

  unsetenv_cross("Y");
}

static void test_yaml_quote(void **state) {
  (void)state;
  char *q = yaml_quote("a'b");
  assert_string_equal(q, "'a''b'");
  free(q);
}

/* =========================================================================
   Script Writing Tests
   ========================================================================= */

static void test_write_script_basic(void **state) {
  (void)state;
  const char *script = "print('hello')";

  char *path = write_script_to_temp(script);

  assert_non_null((void *)path);
  assert_true(file_exists(path));

  char *contents = read_entire_file(path, NULL);
  assert_non_null(contents);
  assert_string_equal(contents, script);

  free(contents);
  remove(path);
  free(path);
}

static void test_write_script_empty_contents(void **state) {
  (void)state;
  char *path = write_script_to_temp("");

  assert_non_null((void *)path);
  assert_true(file_exists(path));

  size_t len = 0;
  char *contents = read_entire_file(path, &len);
  assert_non_null(contents);
  assert_int_equal(len, 0);

  free(contents);
  remove(path);
  free(path);
}

/* =========================================================================
   Execution Suite Registration Matrix
   ========================================================================= */

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_prepare_isa_basic),
      cmocka_unit_test(test_prepare_isa_tmpl),
      cmocka_unit_test(test_config_replace),
      cmocka_unit_test(test_plugin_extract),
      cmocka_unit_test(test_env_string),
      cmocka_unit_test(test_env_int),
      cmocka_unit_test(test_yaml_quote),
      cmocka_unit_test(test_write_script_basic),
      cmocka_unit_test(test_write_script_empty_contents),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
