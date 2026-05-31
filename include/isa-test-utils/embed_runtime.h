#pragma once
// ⚠️ WARNING:
// This header is NOT part of the stable public API.
// It is only intended for use by generated embedding code.
// Layout may change between versions.

#include <stddef.h>
#include "isa-test-utils/isa-test-utils-export.h"
#include "isa-test-utils.h"
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
} EmbeddedYamls;
void embedded_yamls_free(EmbeddedYamls *eya);

// String pair replacements for config yaml templates
typedef struct Replacements Replacements;
ISA_TEST_UTILS_EXPORT Replacements *replacements_new(void);
// Adds a replacement pair to the stack
ISA_TEST_UTILS_EXPORT void replacements_add(Replacements *repls,
                                            const char *key, const char *value);
ISA_TEST_UTILS_EXPORT EnvLocations _prepare_environment(EmbeddedYamls eya,
                                                        Replacements reps);
