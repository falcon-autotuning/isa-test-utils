#pragma once

#include "isa-test-utils/isa-test-utils-export.h"
#include <glib.h>

/*
 * NOTE:
 * - All paths are UTF-8 encoded filesystem paths.
 * - Caller owns all returned strings (must g_free).
 */

typedef struct {
  const char *template_expander_path;
} SetupConfig;

ISA_TEST_UTILS_EXPORT void setup_set_config(const SetupConfig *config);

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

ISA_TEST_UTILS_EXPORT char *prepare_isa_directory(GPtrArray *files,
                                                  GPathBuf *root);

ISA_TEST_UTILS_EXPORT char *prepare_config_directory(GPtrArray *files,
                                                     GPathBuf *root,
                                                     GPtrArray *replacements);

ISA_TEST_UTILS_EXPORT char *prepare_plugin_directory(GPtrArray *files,
                                                     GPathBuf *root);

ISA_TEST_UTILS_EXPORT PrepareEnvironmentResult prepare_full_environment_bundle(
    const EmbeddedBundle *bundle, GPtrArray *replacements);

ISA_TEST_UTILS_EXPORT void cleanup_environment(PrepareEnvironmentResult *env);

ISA_TEST_UTILS_EXPORT char *get_required_env_string(const char *name);
ISA_TEST_UTILS_EXPORT int get_required_env_int(const char *name);
ISA_TEST_UTILS_EXPORT char *yaml_quote(const char *value);
// contents are the contents of the file and it returns the path to the file
ISA_TEST_UTILS_EXPORT char *write_script_to_temp(const char *contents);
