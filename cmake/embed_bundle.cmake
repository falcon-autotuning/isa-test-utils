function(embed_bundle)
  set(options)
  set(oneValueArgs OUTPUT)
  set(multiValueArgs ISA_FILES CONFIG_FILES PLUGIN_FILES)
  cmake_parse_arguments(EMBED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT EMBED_OUTPUT)
    message(FATAL_ERROR "embed_bundle: OUTPUT required")
  endif()

  # Write the public C header boilerplate using clean standard library allocations
  file(WRITE ${EMBED_OUTPUT} [[
#pragma once
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <utarray.h>
#include <isa-test-utils.h>
#include <isa-test-utils/embed_runtime.h>

// UT_array cleanup callbacks specifically matching EmbeddedFile pointers
static inline void _embed_free_file_cb(void *elt) {
  EmbeddedFile *f = *(EmbeddedFile **)elt;
  if (!f) return;
  free((void *)f->data);
  free(f);
}

static inline EmbeddedYamls get_embedded_bundle(void) {
  EmbeddedYamls bundle = {0};

  // Define the custom internal structural initialization control descriptor block
  static const UT_icd embedded_file_icd = {sizeof(EmbeddedFile *), NULL, NULL, _embed_free_file_cb};

  // Allocate arrays using the void* target format required by your new runtime signatures
  utarray_new(bundle.isa_files,    &embedded_file_icd);
  utarray_new(bundle.config_files, &embedded_file_icd);
  utarray_new(bundle.plugin_files, &embedded_file_icd);
]])

  macro(EMBED_CATEGORY FILE_LIST CATEGORY_NAME)
    foreach(f ${${FILE_LIST}})
      file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${f}")
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" sym "${rel}")
      string(TOLOWER "${sym}" sym)
      set(tmp "${CMAKE_CURRENT_BINARY_DIR}/embed_${sym}.h")

      execute_process(
        COMMAND xxd -i -n ${sym} "${f}"
        OUTPUT_FILE "${tmp}"
        RESULT_VARIABLE rc
      )
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "xxd failed for ${f}")
      endif()

      file(READ "${tmp}" content)
      file(APPEND ${EMBED_OUTPUT} "${content}\n")

      # Populate and push the items to UT_array
      file(APPEND ${EMBED_OUTPUT}
"  {
    EmbeddedFile *e = (EmbeddedFile *)calloc(1, sizeof(EmbeddedFile));
    e->relative_path = \"${rel}\";

    // We allocate and copy to the heap because your free_embedded_file_cb destructor releases it
    e->data = (const unsigned char *)malloc(${sym}_len);
    if (e->data) {
      memcpy((void *)e->data, ${sym}, ${sym}_len);
    }
    e->size = ${sym}_len;

    utarray_push_back(bundle.${CATEGORY_NAME}, &e);
  }
")
    endforeach()
  endmacro()

  EMBED_CATEGORY(EMBED_ISA_FILES "isa_files")
  EMBED_CATEGORY(EMBED_CONFIG_FILES "config_files")
  EMBED_CATEGORY(EMBED_PLUGIN_FILES "plugin_files")

  file(APPEND ${EMBED_OUTPUT}
[[  return bundle;
}
/**
 * @brief Prepare an instrument test environment using embedded files and
 *        variadic key/value replacement pairs.
 *
 * This function constructs a temporary test environment using embedded
 * configuration files and applies string replacements to template YAMLs.
 *
 * @param first_key The first key in a sequence of key/value string pairs,
 *                  followed by a variable argument list. May be NULL if no
 *                  replacements are needed.
 *
 * @param ... A sequence of `const char *` arguments representing replacement
 *            pairs in the form:
 *
 *                key1, value1, key2, value2, ..., NULL
 *
 *            Requirements:
 *            - Arguments must come in **key/value pairs**.
 *            - The list **must be terminated with a NULL key**.
 *            - Each key must have a corresponding non-NULL value.
 *
 *            Example:
 *                prepare_environment(
 *                    "mode", "debug",
 *                    "threads", "4",
 *                    NULL
 *                );
 *
 *            Invalid usage examples:
 *                prepare_environment("key1", "value1", "key2", NULL);
 *                // ERROR: missing value for "key2"
 *
 *                prepare_environment("key1", "value1");
 *                // ERROR: missing NULL terminator (undefined behavior)
 *
 * @return EnvLocations struct containing paths to the prepared environment.
 *
 * @note
 * - This function internally constructs and consumes both the embedded bundle
 *   and the replacement set. The caller does not need to free any intermediate
 *   resources.
 *
 * - The returned EnvLocations must be cleaned up using cleanup_environment().
 *
 * @warning
 * - Failure to provide properly paired arguments or a terminating NULL will
 *   result in undefined behavior or a runtime error.
 */
static inline EnvLocations prepare_environment(const char *first_key, ...) {
  EmbeddedYamls eya = get_embedded_bundle();

  Replacements *reps = replacements_new();

  va_list args;
  va_start(args, first_key);

  const char *key = first_key;
  int expecting_value = 0;

  while (key != NULL) {
    const char *value = va_arg(args, const char *);

    if (value == NULL) {
      fprintf(stderr,
              "ERROR: Missing value for key '%s' (arguments must be key/value pairs ending with NULL)\n",
              key);
      va_end(args);
      embedded_yamls_free(&eya);
      replacements_free(reps);
      exit(1);
    }

    replacements_add(reps, key, value);

    // Move to next key
    key = va_arg(args, const char *);
  }

  va_end(args);

  return _prepare_environment(eya, reps);
}
]])
endfunction()
