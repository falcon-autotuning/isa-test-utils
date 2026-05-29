#pragma once

#include "isa-test-utils/isa-test-utils-export.h"
#include <stddef.h>

/* =========================================================================
   Cross-Platform File System Macros
   ========================================================================= */
#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#define DIR_SEPARATOR_STR "\\"
#else
#define DIR_SEPARATOR '/'
#define DIR_SEPARATOR_STR "/"
#endif

/*
 * NOTE:
 * - All paths are UTF-8 encoded filesystem paths.
 * - Memory ownership follows standard C library rules:
 *   dynamically allocated strings must be created via strdup / malloc
 *   and ownership is transferred to the caller, who must release
 *   the memory using standard free().
 * - Functions accepting `void *` array parameters expect an initialized
 *   `UT_array *` container managed via `utarray.h`.
 */

// Lightweight, platform-invariant path buffer
typedef struct {
  char *path_str;  // Managed heap string tracking the absolute/relative path
  size_t length;   // Current character count
  size_t capacity; // Reserved memory safety limit
} PathBuffer;

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
  void *isa_files;    // Managed UT_array* containing EmbeddedFile* elements
  void *config_files; // Managed UT_array* containing EmbeddedFile* elements
  void *plugin_files; // Managed UT_array* containing EmbeddedFile* elements
                      // (optional)
} EmbeddedBundle;

typedef struct {
  char *root_dir;
  char *isa_dir;
  char *config_dir;
  char *plugin_dir; // NULL if none
} PrepareEnvironmentResult;

/* =========================================================================
   Path Buffer Invariant API Methods
   ========================================================================= */
ISA_TEST_UTILS_EXPORT PathBuffer *path_buf_new(const char *initial_path);
ISA_TEST_UTILS_EXPORT void path_buf_push(PathBuffer *buf, const char *element);
ISA_TEST_UTILS_EXPORT void
path_buf_pop(PathBuffer *buf); // Exposes going up to the parent directory
ISA_TEST_UTILS_EXPORT void path_buf_set_extension(PathBuffer *buf,
                                                  const char *ext);
ISA_TEST_UTILS_EXPORT char *path_buf_free_to_path(PathBuffer *buf);

/* =========================================================================
   Environment Core Logic
   ========================================================================= */
ISA_TEST_UTILS_EXPORT char *prepare_isa_directory(void *files,
                                                  PathBuffer *root);

ISA_TEST_UTILS_EXPORT char *
prepare_config_directory(void *files, PathBuffer *root, void *replacements);

ISA_TEST_UTILS_EXPORT char *prepare_plugin_directory(void *files,
                                                     PathBuffer *root);

ISA_TEST_UTILS_EXPORT PrepareEnvironmentResult prepare_full_environment_bundle(
    const EmbeddedBundle *bundle, void *replacements);

ISA_TEST_UTILS_EXPORT void cleanup_environment(PrepareEnvironmentResult *env);

/* =========================================================================
   Utilities
   ========================================================================= */
ISA_TEST_UTILS_EXPORT char *get_required_env_string(const char *name);
ISA_TEST_UTILS_EXPORT int get_required_env_int(const char *name);
ISA_TEST_UTILS_EXPORT char *yaml_quote(const char *value);

// contents are the contents of the file and it returns the path to the file
ISA_TEST_UTILS_EXPORT char *write_script_to_temp(const char *contents);
