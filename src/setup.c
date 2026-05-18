#include "isa-test-utils/setup.h"
#include "isa-test-utils/run.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

/* ================================
   Dependency Injection
================================ */

static SetupConfig g_setup_config = {.template_expander_path =
                                         "template-expander"};

void setup_set_config(const SetupConfig *cfg) {
  if (cfg)
    g_setup_config = *cfg;
}

static gboolean is_template_file(const char *path) {
  return g_str_has_suffix(path, ".tmpl");
}

static void run_template_expander(const char *in, const char *out) {
  GPtrArray *args = g_ptr_array_new();
  g_ptr_array_add(args, (char *)in);
  g_ptr_array_add(args, (char *)out);

  ProcessResult r = run_executable(g_setup_config.template_expander_path, args);
  g_ptr_array_free(args, TRUE);

  if (r.exit_code != 0) {
    g_error("Template expander failed\ninput=%s\noutput=%s\nstderr=%s", in, out,
            r.stderr_data);
  }

  g_free(r.stdout_data);
  g_free(r.stderr_data);
}

char *prepare_isa_directory(GPtrArray *files, GPathBuf *root) {
  GPathBuf *path = g_path_buf_copy(root);
  g_path_buf_push(path, "isa");

  char *dst = g_path_buf_free_to_path(path);

  /* clean existing */
  if (g_file_test(dst, G_FILE_TEST_EXISTS)) {
    g_rmdir(dst);
  }

  g_mkdir_with_parents(dst, 0755);

  /* extract all embedded files */
  for (guint i = 0; i < files->len; i++) {
    EmbeddedFile *f = g_ptr_array_index(files, i);

    char *out_path = g_build_filename(dst, f->relative_path, NULL);

    char *dir = g_path_get_dirname(out_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    g_file_set_contents(out_path, (const char *)f->data, f->size, NULL);

    /* handle .tmpl */
    if (g_str_has_suffix(out_path, ".tmpl")) {
      char *expanded = g_strdup(out_path);
      expanded[strlen(expanded) - 5] = '\0';

      run_template_expander(out_path, expanded);

      g_remove(out_path); // ✅ remove tmpl file

      g_free(expanded);
    }

    g_free(out_path);
  }

  return dst;
}

char *prepare_config_directory(GPtrArray *files, GPathBuf *root,
                               GPtrArray *replacements) {
  GPathBuf *path = g_path_buf_copy(root);
  g_path_buf_push(path, "config");

  char *dst = g_path_buf_free_to_path(path);

  /* clean existing */
  if (g_file_test(dst, G_FILE_TEST_EXISTS)) {
    g_rmdir(dst);
  }

  g_mkdir_with_parents(dst, 0755);

  for (guint i = 0; i < files->len; i++) {
    EmbeddedFile *f = g_ptr_array_index(files, i);

    char *out_path = g_build_filename(dst, f->relative_path, NULL);

    char *dir = g_path_get_dirname(out_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    /* write raw file first */
    g_file_set_contents(out_path, (const char *)f->data, f->size, NULL);

    /* handle .in templates */
    if (g_str_has_suffix(out_path, ".in")) {

      /* load into GString */
      gchar *contents = NULL;
      g_file_get_contents(out_path, &contents, NULL, NULL);

      GString *buf = g_string_new(contents);
      g_free(contents);

      /* apply replacements */
      for (guint j = 0; j < replacements->len; j++) {
        PairString *p = g_ptr_array_index(replacements, j);

        gchar *pos;
        while ((pos = g_strstr_len(buf->str, buf->len, p->first))) {
          gsize idx = pos - buf->str;
          g_string_erase(buf, idx, strlen(p->first));
          g_string_insert(buf, idx, p->second);
        }
      }

      /* write final output without .in */
      char *final_path = g_strdup(out_path);
      size_t len = strlen(final_path);

      if (len > 3) {
        final_path[len - 3] = '\0'; // remove ".in"
      }

      g_file_set_contents(final_path, buf->str, -1, NULL);

      g_string_free(buf, TRUE);

      /* remove template file */
      g_remove(out_path);

      g_free(final_path);
    }

    g_free(out_path);
  }

  return dst;
}
char *prepare_plugin_directory(GPtrArray *files, GPathBuf *root) {
  GPathBuf *path = g_path_buf_copy(root);
  g_path_buf_push(path, "plugins");

  char *dst = g_path_buf_free_to_path(path);

  /* clean existing */
  if (g_file_test(dst, G_FILE_TEST_EXISTS)) {
    // Simple cleanup (no recursion for demo)
    g_rmdir(dst);
  }

  g_mkdir_with_parents(dst, 0755);

  for (guint i = 0; i < files->len; i++) {
    EmbeddedFile *f = g_ptr_array_index(files, i);

    char *out_path = g_build_filename(dst, f->relative_path, NULL);

    char *dir = g_path_get_dirname(out_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    /* Write binary exactly as-is */
    if (!g_file_set_contents(out_path, (const char *)f->data, f->size, NULL)) {
      g_error("Failed to write plugin file: %s", out_path);
    }

#ifdef _WIN32
    /* nothing needed */
#else
    /* Ensure executable permissions */
    g_chmod(out_path, 0755);
#endif

    g_free(out_path);
  }

  return dst;
}
PrepareEnvironmentResult
prepare_full_environment_bundle(const EmbeddedBundle *bundle,
                                GPtrArray *replacements) {
  PrepareEnvironmentResult result = {0};

  /* create root dir */
  g_auto(GPathBuf) root;
  g_path_buf_init_from_path(&root, g_get_tmp_dir());
  g_path_buf_push(&root, "test_env");

  result.root_dir = g_path_buf_to_path(&root);

  g_mkdir_with_parents(result.root_dir, 0755);

  /* =============================
     ISA
  ============================== */
  if (!bundle->isa_files) {
    g_error("Missing ISA files");
  }

  result.isa_dir = prepare_isa_directory(bundle->isa_files, &root);

  /* =============================
     Config
  ============================== */
  if (!bundle->config_files) {
    g_error("Missing config files");
  }

  GPtrArray *local_repl = replacements;
  gboolean need_free = FALSE;

  if (!local_repl) {
    local_repl = g_ptr_array_new();
    need_free = TRUE;
  }

  result.config_dir =
      prepare_config_directory(bundle->config_files, &root, local_repl);

  /* =============================
     Plugin (optional)
  ============================== */
  if (bundle->plugin_files && bundle->plugin_files->len > 0) {
    result.plugin_dir = prepare_plugin_directory(bundle->plugin_files, &root);
  } else {
    result.plugin_dir = NULL;
  }
  g_path_buf_clear(&root);

  if (need_free)
    g_ptr_array_free(local_repl, TRUE);

  return result;
}
#include <gio/gio.h>

/* Recursively delete a directory */
static void remove_dir_recursive(const char *path) {
  GError *error = NULL;

  GFile *root = g_file_new_for_path(path);

  GFileEnumerator *enumerator = g_file_enumerate_children(
      root, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, &error);

  if (!enumerator) {
    /* Might not exist — ignore */
    g_clear_error(&error);
    g_object_unref(root);
    return;
  }

  GFileInfo *info;

  while ((info = g_file_enumerator_next_file(enumerator, NULL, &error))) {
    const char *name = g_file_info_get_name(info);

    GFile *child = g_file_get_child(root, name);

    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
      char *child_path = g_file_get_path(child);
      remove_dir_recursive(child_path);
      g_free(child_path);
    } else {
      g_file_delete(child, NULL, NULL);
    }

    g_object_unref(child);
    g_object_unref(info);
  }

  g_object_unref(enumerator);

  /* Now remove the empty directory */
  g_file_delete(root, NULL, NULL);
  g_object_unref(root);
}

void cleanup_environment(PrepareEnvironmentResult *env) {
  if (!env)
    return;

  if (env->root_dir) {
    remove_dir_recursive(env->root_dir);
  }

  g_free(env->isa_dir);
  g_free(env->config_dir);
  g_free(env->plugin_dir);
  g_free(env->root_dir);
}

/* ================================
   Env + Utils
================================ */

char *get_required_env_string(const char *name) {
  const char *val = g_getenv(name);

  if (!val || *val == '\0') {
    g_error("Missing env var: %s", name);
  }

  return g_strdup(val);
}

int get_required_env_int(const char *name) {
  char *val = get_required_env_string(name);

  char *end;
  long x = strtol(val, &end, 10);

  if (*end != '\0') {
    g_error("Invalid integer env var: %s", name);
  }

  g_free(val);
  return (int)x;
}

char *yaml_quote(const char *value) {
  GString *out = g_string_new("'");
  for (const char *p = value; *p; p++) {
    if (*p == '\'')
      g_string_append(out, "''");
    else
      g_string_append_c(out, *p);
  }
  g_string_append(out, "'");
  return g_string_free(out, FALSE);
}

char *write_script_to_temp(const char *contents) {
  GError *error = NULL;
  char *name = NULL;

  int fd = g_file_open_tmp("iss-script-XXXXXX.lua", &name, &error);
  if (fd < 0) {
    g_error("Failed to create temp file: %s", error->message);
    g_clear_error(&error);
    return NULL;
  }
  close(fd);

  if (!g_file_set_contents(name, contents, -1, &error)) {
    g_error("Failed to write script: %s", error->message);
    g_clear_error(&error);
    g_free(name);
    return NULL;
  }

  return name;
}
