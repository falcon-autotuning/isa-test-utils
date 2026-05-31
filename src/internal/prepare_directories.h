#pragma once
#include "isa-test-utils/isa-test-utils-export.h"
#include "isa-test-utils/embed_runtime.h"
#include "isa-test-utils.h"

ISA_TEST_UTILS_EXPORT Path *prepare_isa_directory(void *files, Path *root);
ISA_TEST_UTILS_EXPORT Path *
prepare_config_directory(void *files, Path *root,
                         const Replacements *replacements);
ISA_TEST_UTILS_EXPORT Path *prepare_plugin_directory(void *files, Path *root);

ISA_TEST_UTILS_EXPORT char *yaml_quote(const char *value);
