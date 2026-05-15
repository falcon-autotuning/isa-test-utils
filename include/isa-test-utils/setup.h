#pragma once

#include <glib.h>

/*
 * NOTE:
 * - All paths are UTF-8 encoded filesystem paths.
 * - Caller owns all returned strings (must g_free).
 */

typedef struct {
  const char *template_expander_path;
} SetupConfig;

void setup_set_config(const SetupConfig *config);

typedef struct {
  char *first;
  char *second;
} PairString;

typedef struct {
  const char *relative_path;
  const unsigned char *data;
  size_t size;
} EmbeddedFile;

typedef struct {
  GPtrArray *isa_files;    // EmbeddedFile*
  GPtrArray *config_files; // EmbeddedFile*
  GPtrArray *plugin_files; // EmbeddedFile* (optional)
} EmbeddedBundle;

typedef struct {
  char *root_dir;
  char *isa_dir;
  char *config_dir;
  char *plugin_dir; // NULL if none
} PrepareEnvironmentResult;

char *prepare_isa_directory(GPtrArray *files, GPathBuf *root);

char *prepare_config_directory(GPtrArray *files, GPathBuf *root,
                               GPtrArray *replacements);

char *prepare_plugin_directory(GPtrArray *files, GPathBuf *root);

PrepareEnvironmentResult
prepare_full_environment_bundle(const EmbeddedBundle *bundle,
                                GPtrArray *replacements);

void cleanup_environment(PrepareEnvironmentResult *env);

char *get_required_env_string(const char *name);
int get_required_env_int(const char *name);
char *yaml_quote(const char *value);
