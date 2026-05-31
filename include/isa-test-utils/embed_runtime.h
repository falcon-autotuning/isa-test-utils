#pragma once
// ⚠️ WARNING:
// This header is NOT part of the stable public API.
// It is only intended for use by generated embedding code.
// Layout may change between versions.

#include <stddef.h>
#include <utarray.h>
#include "isa-test-utils/isa-test-utils-export.h"
#include "isa-test-utils.h"
typedef struct {
  const char *relative_path;
  const unsigned char *data;
  size_t size;
} EmbeddedFile;

typedef struct {
  UT_array *isa_files;
  UT_array *config_files;
  UT_array *plugin_files;
} EmbeddedYamls;
ISA_TEST_UTILS_EXPORT void embedded_yamls_free(EmbeddedYamls *eya);

// String pair replacements for config yaml templates
// Concrete implementation hidden from public header consumers
typedef struct {
  UT_array *pairs;
} Replacements;
ISA_TEST_UTILS_EXPORT Replacements *replacements_new(void);
ISA_TEST_UTILS_EXPORT void replacements_free(Replacements *repls);
// Adds a replacement pair to the stack
ISA_TEST_UTILS_EXPORT void replacements_add(Replacements *repls,
                                            const char *key, const char *value);
ISA_TEST_UTILS_EXPORT EnvLocations _prepare_environment(EmbeddedYamls eya,
                                                        Replacements reps);
