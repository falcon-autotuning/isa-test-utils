function(embed_bundle)
  set(options)
  set(oneValueArgs OUTPUT)
  set(multiValueArgs ISA_FILES CONFIG_FILES PLUGIN_FILES)
  cmake_parse_arguments(EMBED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT EMBED_OUTPUT)
    message(FATAL_ERROR "embed_bundle: OUTPUT required")
  endif()

  file(WRITE ${EMBED_OUTPUT}
"#pragma once
#include <glib.h>
#include <isa-test-utils/setup.h>

")

  file(APPEND ${EMBED_OUTPUT}
"static inline EmbeddedBundle get_embedded_bundle(void) {
  EmbeddedBundle bundle = {0};

  bundle.isa_files = g_ptr_array_new();
  bundle.config_files = g_ptr_array_new();
  bundle.plugin_files = g_ptr_array_new();
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
      file(APPEND ${EMBED_OUTPUT}
"  {
    EmbeddedFile *e = g_new0(EmbeddedFile, 1);
    e->relative_path = \"${rel}\";
    e->data = ${sym};
    e->size = ${sym}_len;
    g_ptr_array_add(bundle.${CATEGORY_NAME}, e);
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
