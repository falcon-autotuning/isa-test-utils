#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "isa-test-utils/setup.h"
#include "utarray.h"

// Cross-platform environment and file access configurations
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define access _access
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

static bool file_exists(const char *path) {
  if (!path)
    return false;
  return access(path, F_OK) == 0;
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

static PairString *pair(const char *k, const char *v) {
  PairString *p = calloc(1, sizeof(PairString));
  if (p) {
    p->first = strdup(k);
    p->second = strdup(v);
  }
  return p;
}

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

static void free_pair_string_cb(void *elt) {
  PairString *p = *(PairString **)elt;
  if (!p)
    return;
  free(p->first);
  free(p->second);
  free(p);
}

static const UT_icd embedded_file_icd = {sizeof(EmbeddedFile *), NULL, NULL,
                                         free_embedded_file_cb};
static const UT_icd pair_string_icd = {sizeof(PairString *), NULL, NULL,
                                       free_pair_string_cb};

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

  PathBuffer *root = path_buf_new(get_system_tmp_dir());
  char *dir = prepare_isa_directory(files, root);
  utarray_free(files);

  PathBuffer *out_pb = path_buf_new(dir);
  path_buf_push(out_pb, "a.txt");
  char *out = path_buf_free_to_path(out_pb);

  assert_true(file_exists(out));

  free(out);
  free(dir);
  free(root->path_str);
  free(root);
}

static void test_prepare_isa_tmpl(void **state) {
  (void)state;
  setup_expander();

  UT_array *files;
  utarray_new(files, &embedded_file_icd);

  EmbeddedFile *f = make_file("x.tmpl", "expand me");
  utarray_push_back(files, &f);

  PathBuffer *root = path_buf_new(get_system_tmp_dir());
  char *dir = prepare_isa_directory(files, root);
  utarray_free(files);

  PathBuffer *exp_pb = path_buf_new(dir);
  path_buf_push(exp_pb, "x");
  char *expanded = path_buf_free_to_path(exp_pb);

  PathBuffer *tmpl_pb = path_buf_new(dir);
  path_buf_push(tmpl_pb, "x.tmpl");
  char *tmpl = path_buf_free_to_path(tmpl_pb);

  assert_true(file_exists(expanded));
  assert_false(file_exists(tmpl));

  free(expanded);
  free(tmpl);
  free(dir);
  free(root->path_str);
  free(root);
}

/* =========================================================================
   Config Tests
   ========================================================================= */

static void test_config_replace(void **state) {
  (void)state;
  UT_array *files;
  UT_array *replacements;
  utarray_new(files, &embedded_file_icd);
  utarray_new(replacements, &pair_string_icd);

  EmbeddedFile *f = make_file("cfg.yml.in", "hello __NAME__");
  utarray_push_back(files, &f);

  PairString *p = pair("__NAME__", "world");
  utarray_push_back(replacements, &p);

  PathBuffer *root = path_buf_new(get_system_tmp_dir());
  char *dir = prepare_config_directory(files, root, replacements);
  utarray_free(files);
  utarray_free(replacements);

  PathBuffer *out_pb = path_buf_new(dir);
  path_buf_push(out_pb, "cfg.yml");
  char *out = path_buf_free_to_path(out_pb);

  char *contents = read_entire_file(out, NULL);
  assert_non_null(contents);
  assert_string_equal(contents, "hello world");

  free(contents);
  free(out);
  free(dir);
  free(root->path_str);
  free(root);
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

  PathBuffer *root = path_buf_new(get_system_tmp_dir());
  char *dir = prepare_plugin_directory(files, root);
  utarray_free(files);

  PathBuffer *out_pb = path_buf_new(dir);
  path_buf_push(out_pb, "plugin.so");
  char *out = path_buf_free_to_path(out_pb);

  assert_true(file_exists(out));

  free(out);
  free(dir);
  free(root->path_str);
  free(root);
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
