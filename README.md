# isa-test-utils

A C99 test utility library for building fully hermetic instrument integration tests.

This library allows you to embed ISA definitions, config templates, and optional plugins directly into your test binary, extract them at runtime, and run reproducible tests with no reliance on the local filesystem.

---------------------------------------------------------------------

## ✅ Core Idea

Instead of depending on external files:

- ISA definitions
- Config files
- Plugins

You embed everything into your test binary at build time.

At runtime, the library extracts them into a temporary directory and prepares a complete environment.

---------------------------------------------------------------------

## 🚀 Key Feature

You should always use:

```c
PrepareEnvironmentResult env =
    prepare_full_environment_bundle(&bundle, replacements);
```

This function:

- Extracts ISA files
- Expands `.tmpl` files
- Generates config from `.in` templates
- Extracts plugin binaries (optional)

---------------------------------------------------------------------

## 📦 Building

Requirements:

- CMake ≥ 3.20
- C compiler (gcc/clang/MSVC)

Build:

```
cmake -B build -S .
cmake --build build
```

Install:

```
cmake --install build
```

---------------------------------------------------------------------

## 🔧 Using in Your Project

### Find Package

```
find_package(isa-test-utils REQUIRED)
```

### Link

```
target_link_libraries(my_test
    PRIVATE isa-test-utils::isa-test-utils
)
```

---------------------------------------------------------------------

## 📦 Embedding Files (IMPORTANT)

You must embed all required files at build time.

### Example layout

```
isa/
config/
plugins/
```

### Collect files

```
file(GLOB_RECURSE ISA_FILES    "${CMAKE_SOURCE_DIR}/isa/*")
file(GLOB_RECURSE CONFIG_FILES "${CMAKE_SOURCE_DIR}/config/*")
file(GLOB_RECURSE PLUGIN_FILES "${CMAKE_SOURCE_DIR}/plugins/*")
```

### Generate embedded bundle

```
isa_test_utils_embed_bundle(
  OUTPUT ${CMAKE_BINARY_DIR}/embedded_bundle.h
  ISA_FILES ${ISA_FILES}
  CONFIG_FILES ${CONFIG_FILES}
  PLUGIN_FILES ${PLUGIN_FILES}
)
```

---------------------------------------------------------------------

## 🧪 Writing Tests

### Include bundle and data structures

```c
#include "embedded_bundle.h"
#include "utarray.h" // For handling argument and replacement lists
```

### Build bundle

```c
EmbeddedBundle bundle = get_embedded_bundle();
```

### Define replacements (for `.in` files)

Initialize a `UT_array` using the pair-string control descriptor to pass dynamic replacement options safely:

```c
// Define the custom pair tracking metadata descriptor
static void free_pair_string_cb(void *elt) {
  PairString *p = *(PairString **)elt;
  if (!p) return;
  free(p->first);
  free(p->second);
  free(p);
}
static const UT_icd pair_string_icd = {sizeof(PairString *), NULL, NULL, free_pair_string_cb};

// Instantiate the replacements container
UT_array *replacements;
utarray_new(replacements, &pair_string_icd);

PairString *p = calloc(1, sizeof(PairString));
p->first = strdup("__NAME__");
p->second = strdup("MyInstrument");

utarray_push_back(replacements, &p);
```

### Prepare environment

```c
PrepareEnvironmentResult env =
    prepare_full_environment_bundle(&bundle, (void *)replacements);
```

### Run your tests

```c
start_server();
```

### Cleanup

```c
cleanup_environment(&env);
utarray_free(replacements); // Free the container after cleanup
```

---------------------------------------------------------------------

## 📂 Template Behavior

### `.tmpl` files

- Passed to external template-expander
- Output replaces `.tmpl`

Example:

```
driver.yml.tmpl → driver.yml
```

---

### `.in` files

- Processed internally
- Uses `PairString` replacements

Example:

```
config.yml.in → config.yml
```

---------------------------------------------------------------------

## 🔌 Plugin Handling

Plugins are:

- Embedded as raw binaries
- Extracted as-is

Supported platforms:

- Linux: `.so`
- Windows: `.dll`

Plugins are optional.

---------------------------------------------------------------------

## 📁 Runtime Output

The system creates a temporary directory:

```
[System Temp]/test_env/
    isa/
    config/
    plugins/
```

---------------------------------------------------------------------

## ⚠️ Important

- The library cannot discover files automatically
- Everything must be embedded via CMake
- Templates must use `.in` extension for replacement
- All allocations transfer to standard `free()` structures instead of custom library macros

Example:

```
name: __NAME__
address: __ADDRESS__
```

---------------------------------------------------------------------

## ✅ Recommended Workflow

1. Embed ISA/config/plugin files with CMake
2. Generate embedded bundle
3. Use `prepare_full_environment_bundle`
4. Run tests
5. Cleanup

---------------------------------------------------------------------

## 🤝 Contributing

Contributions welcome!
Focus areas:

- platform support improvements
- plugin handling enhancements
- template tooling extensions

---------------------------------------------------------------------

## 📄 License

MPLv2.0
