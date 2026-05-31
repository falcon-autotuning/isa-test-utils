# isa-test-utils

isa-test-utils is a C99 library for building fully hermetic instrument integration tests.

It allows you to embed ISA definitions, configuration templates, and optional plugins
directly into your test binary, extract them at runtime, and execute reproducible tests
with no reliance on external files.

Instead of managing runtime files manually, your test binary contains everything it needs.
At runtime, the library extracts these resources into a temporary directory, applies
configuration replacements, and prepares a fully functional test environment.

---------------------------------------------------------------------

## Quickstart

### Requirements

- CMake ≥ 3.20
- C compiler (gcc / clang)
- `instrument-script-server` installed and available on your system `PATH`

The test runtime depends on `instrument-script-server`. Tests will fail if it is not found.

---

### 1. Embed Files (CMake)

```cmake
find_package(isa-test-utils REQUIRED)

file(GLOB_RECURSE ISA_FILES    "${CMAKE_SOURCE_DIR}/isa/*")
file(GLOB_RECURSE CONFIG_FILES "${CMAKE_SOURCE_DIR}/config/*")
file(GLOB_RECURSE PLUGIN_FILES "${CMAKE_SOURCE_DIR}/plugins/*") # optional

isa_test_utils_embed_bundle(
  OUTPUT ${CMAKE_BINARY_DIR}/embedded_bundle.h
  ISA_FILES ${ISA_FILES}
  CONFIG_FILES ${CONFIG_FILES}
  PLUGIN_FILES ${PLUGIN_FILES}
)
````

***

### 2. Use in Tests

```c
#include "embedded_bundle.h"
#include <isa-test-utils.h>

EnvLocations env = prepare_environment(
    "__DEVICE_NAME__", "MyInstrument",
    NULL
);

start_server();
start_instrument(env.config_dir, NULL);

/* run tests */

stop_instrument("MyInstrument");
stop_server();

cleanup_environment(&env);
```

***

### 3. Build and Run

```bash
make build PRESET=<preset from CMakePresets.json>
```

***

## Test Setup (Detailed)

Tests are typically structured using global setup/teardown.

### Global Setup

```c
static EnvLocations env;
static Path *config_path = NULL;

static int global_group_setup(void **state) {
  (void)state;

  env = prepare_environment(
      "__DEVICE_NAME__", "MyInstrument",
      "__ADDRESS__", "127.0.0.1",
      NULL
  );

  config_path = path_clone(env.config_dir);
  path_push(config_path, "instrument.yml");

  return 0;
}
```

### Global Teardown

```c
static int global_group_teardown(void **state) {
  (void)state;

  cleanup_environment(&env);
  path_free(config_path);

  return 0;
}
```

### Per-Test Setup

```c
static void test_setup(void) {
  start_server();
  start_instrument(config_path, NULL);
}

static void test_teardown(void) {
  stop_instrument("MyInstrument");
  stop_server();
}
```

***

## Measurement Script Example

Most real tests use measurement scripts.

```c
const char *script =
    flatten_yaml(
        "function main(ctx, value)\n"
        "  ctx:call(\"device.SET\", value)\n"
        "  ctx:call(\"device.GET?\")\n"
        "end\n"
    );

Map *vars = map_new();
map_add_float(vars, "value", 5.0f);

const Result *res = perform_measurement(script, vars);

/* inspect results */

free(script);
free_result(res);
map_free(vars);
```

The returned `Result` contains step-by-step execution details that can be used
for validation.

***

## Environment Creation

The generated function:

```c
EnvLocations prepare_environment(const char *first_key, ...);
```

- Extracts embedded ISA files
- Processes configuration templates
- Applies key/value substitutions
- Extracts plugins (optional)
- Returns an environment layout

### Replacement Arguments

```c
EnvLocations env = prepare_environment(
    "KEY1", "value1",
    "KEY2", "value2",
    NULL
);
```

Rules:

- Must be key/value pairs
- Must terminate with `NULL`
- Keys and values must be `const char *`

***

## Template Behavior

### `.in` files

Configuration templates should use `.in`:

```
config.yml.in → config.yml
```

Values are replaced using arguments passed to `prepare_environment()`.

Example:

```
name: __DEVICE_NAME__
address: __ADDRESS__
```

***

### `.tmpl` files

- Processed via external template expansion
- Output replaces `.tmpl`

```
driver.yml.tmpl → driver.yml
```

***

## Plugin Handling

Plugins are optional.

If provided:

- Embedded into the binary
- Extracted at runtime
- Available at `env.plugin_dir`

If not provided:

```
env.plugin_dir == NULL
```

Supported formats:

- Linux: `.so`
- Windows: `.dll`

***

## Runtime Output

A temporary directory is created:

```
<system temp>/test_env/
    isa/
    config/
    plugins/   (optional)
```

***

## Memory Ownership

- EnvLocations → cleanup\_environment()
- Result → free\_result()
- Map → map\_free()
- Strings → free()

All other memory is managed internally.

***

## API Overview

Environment:

- prepare\_environment (generated)
- cleanup\_environment

Runtime:

- start\_server
- start\_instrument
- stop\_instrument
- stop\_server

Measurement:

- perform\_measurement
- free\_result

Inputs:

- map\_new
- map\_add\_\*
- map\_free

Utilities:

- flatten\_yaml
- name\_with\_index
- get\_required\_env\_\*

***

## Recommended Workflow

1. Embed ISA/config/plugin files with CMake
2. Generate embedded bundle
3. Call `prepare_environment(...)`
4. Start server and instrument
5. Execute measurement scripts
6. Validate results
7. Cleanup

***

## Building

```bash
cmake -B build -S .
cmake --build build
cmake --install build
```

***

## License

Mozilla Public License v2.0 (MPL-2.0)
