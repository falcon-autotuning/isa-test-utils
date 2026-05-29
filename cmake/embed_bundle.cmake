function(embed_bundle)
  set(options)
  set(oneValueArgs OUTPUT)
  set(multiValueArgs ISA_FILES CONFIG_FILES PLUGIN_FILES)
  cmake_parse_arguments(EMBED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT EMBED_OUTPUT)
    message(FATAL_ERROR "embed_bundle: OUTPUT required")
  endif()

  # Write the public C header boilerplate using clean standard library allocations
  file(WRITE ${EMBED_OUTPUT}
"#pragma once
#include <stdlib.h>
#include <utarray.h>
#include <isa-test-utils/setup.h>

// UT_array cleanup callbacks specifically matching EmbeddedFile pointers
static inline void _embed_free_file_cb(void *elt) {
  EmbeddedFile *f = *(EmbeddedFile **)elt;
  if (!f) return;
  free((void *)f->data);
  free(f);
}

static inline EmbeddedBundle get_embedded_bundle(void) {
  EmbeddedBundle bundle = {0};

  // Define the custom internal structural initialization control descriptor block
  static const UT_icd embedded_file_icd = {sizeof(EmbeddedFile *), NULL, NULL, _embed_free_file_cb};

  // Allocate arrays using the void* target format required by your new runtime signatures
  utarray_new(*(UT_array **)&bundle.isa_files,    &embedded_file_icd);
  utarray_new(*(UT_array **)&bundle.config_files, &embedded_file_icd);
  utarray_new(*(UT_array **)&bundle.plugin_files, &embedded_file_icd);
")

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

    utarray_push_back(*(UT_array **)&bundle.${CATEGORY_NAME}, &e);
  }
")
    endforeach()
  endmacro()

  EMBED_CATEGORY(EMBED_ISA_FILES "isa_files")
  EMBED_CATEGORY(EMBED_CONFIG_FILES "config_files")
  EMBED_CATEGORY(EMBED_PLUGIN_FILES "plugin_files")

  file(APPEND ${EMBED_OUTPUT}
"  return bundle;
}
")
endfunction()
