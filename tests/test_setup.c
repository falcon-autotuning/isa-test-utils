#include "isa-test-utils/setup.h"
#include <glib.h>
#include <glib/gstdio.h>

/* =========================
   Helpers
========================= */

static PairString *pair(const char *k, const char *v) {
  PairString *p = g_new0(PairString, 1);
  p->first = g_strdup(k);
  p->second = g_strdup(v);
  return p;
}

static EmbeddedFile *make_file(const char *path, const char *data) {
  EmbeddedFile *f = g_new0(EmbeddedFile, 1);
  f->relative_path = path;
  f->data = (const unsigned char *)g_strdup(data);
  f->size = strlen(data);
  return f;
}

static void setup_expander(void) {
  setup_set_config(
      &(SetupConfig){.template_expander_path = MOCK_EXPANDER_PATH});
}

static void free_embedded_file(gpointer ptr) {
  EmbeddedFile *f = ptr;

  if (!f)
    return;

  g_free((void *)f->data); // free strdup data
  /* NOTE: relative_path is NOT owned (string literal) */

  g_free(f);
}

/* =========================
   ISA tests
========================= */

static void test_prepare_isa_basic(void) {
  setup_expander();

  GPtrArray *files = g_ptr_array_new_with_free_func(free_embedded_file);

  g_ptr_array_add(files, make_file("a.txt", "hello"));
  g_auto(GPathBuf) root;
  g_path_buf_init_from_path(&root, g_get_tmp_dir());

  char *dir = prepare_isa_directory(files, &root);

  char *out = g_build_filename(dir, "a.txt", NULL);

  g_assert_true(g_file_test(out, G_FILE_TEST_EXISTS));

  g_free(out);
  g_free(dir);
}

static void test_prepare_isa_tmpl(void) {
  setup_expander();

  GPtrArray *files = g_ptr_array_new_with_free_func(free_embedded_file);

  g_ptr_array_add(files, make_file("x.tmpl", "expand me"));
  g_auto(GPathBuf) root;
  g_path_buf_init_from_path(&root, g_get_tmp_dir());

  char *dir = prepare_isa_directory(files, &root);

  char *expanded = g_build_filename(dir, "x", NULL);
  char *tmpl = g_build_filename(dir, "x.tmpl", NULL);

  g_assert_true(g_file_test(expanded, G_FILE_TEST_EXISTS));
  g_assert_false(g_file_test(tmpl, G_FILE_TEST_EXISTS));

  g_free(expanded);
  g_free(tmpl);
  g_free(dir);
}

/* =========================
   Config tests
========================= */

static void test_config_replace(void) {
  GPtrArray *files = g_ptr_array_new_with_free_func(free_embedded_file);
  GPtrArray *replacements = g_ptr_array_new_with_free_func(free_embedded_file);

  g_ptr_array_add(files, make_file("cfg.yml.in", "hello __NAME__"));

  g_ptr_array_add(replacements, pair("__NAME__", "world"));
  g_auto(GPathBuf) root;
  g_path_buf_init_from_path(&root, g_get_tmp_dir());

  char *dir = prepare_config_directory(files, &root, replacements);

  char *out = g_build_filename(dir, "cfg.yml", NULL);

  gchar *contents;
  g_file_get_contents(out, &contents, NULL, NULL);

  g_assert_cmpstr(contents, ==, "hello world");

  g_free(contents);
  g_free(out);
  g_free(dir);
}

/* =========================
   Plugin tests
========================= */

static void test_plugin_extract(void) {
  GPtrArray *files = g_ptr_array_new_with_free_func(free_embedded_file);

  g_ptr_array_add(files, make_file("plugin.so", "binary"));
  g_auto(GPathBuf) root;
  g_path_buf_init_from_path(&root, g_get_tmp_dir());

  char *dir = prepare_plugin_directory(files, &root);

  char *out = g_build_filename(dir, "plugin.so", NULL);

  g_assert_true(g_file_test(out, G_FILE_TEST_EXISTS));

  g_free(out);
  g_free(dir);
}

/* =========================
   Env + utils
========================= */

static void test_env_string(void) {
  g_setenv("X", "abc", TRUE);

  char *v = get_required_env_string("X");
  g_assert_cmpstr(v, ==, "abc");

  g_free(v);
}

static void test_env_int(void) {
  g_setenv("Y", "123", TRUE);

  int v = get_required_env_int("Y");
  g_assert_cmpint(v, ==, 123);
}

static void test_yaml_quote(void) {
  char *q = yaml_quote("a'b");

  g_assert_cmpstr(q, ==, "'a''b'");

  g_free(q);
}

/* =========================
   Script writing tests
========================= */

static void test_write_script_basic(void) {
  const char *script = "print('hello')";

  char *path = write_script_to_temp(script);

  g_assert_nonnull(path);
  g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

  gchar *contents = NULL;
  g_file_get_contents(path, &contents, NULL, NULL);

  g_assert_cmpstr(contents, ==, script);

  g_free(contents);
  g_remove(path);
  g_free(path);
}

/* -------------------------
   Empty contents
------------------------- */

static void test_write_script_empty_contents(void) {
  char *path = write_script_to_temp("");

  g_assert_nonnull(path);
  g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

  gchar *contents = NULL;
  gsize len = 0;

  g_file_get_contents(path, &contents, &len, NULL);

  g_assert_cmpint(len, ==, 0);

  g_free(contents);
  g_remove(path);
  g_free(path);
}

/* =========================
   main
========================= */

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/setup/isa/basic", test_prepare_isa_basic);
  g_test_add_func("/setup/isa/tmpl", test_prepare_isa_tmpl);

  g_test_add_func("/setup/config/replace", test_config_replace);

  g_test_add_func("/setup/plugin/extract", test_plugin_extract);

  g_test_add_func("/setup/env/string", test_env_string);
  g_test_add_func("/setup/env/int", test_env_int);
  g_test_add_func("/setup/yaml", test_yaml_quote);
  g_test_add_func("/setup/script/basic", test_write_script_basic);
  g_test_add_func("/setup/script/empty_contents",
                  test_write_script_empty_contents);

  return g_test_run();
}
