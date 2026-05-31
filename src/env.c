#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "isa-test-utils.h"
#ifdef _WIN32
#define strdup _strdup
#endif
char *get_required_env_string(const char *name) {
  // Replaces g_getenv with standard C library getenv
  const char *val = getenv(name);

  if (!val || *val == '\0') {
    fprintf(stderr, "FATAL ERROR: Missing env var: %s\n", name);
    exit(1); // Mimics g_error abortion behavior
  }

  return strdup(val);
}

int get_required_env_int(const char *name) {
  char *val = get_required_env_string(name);

  char *end;
  long x = strtol(val, &end, 10);

  if (*end != '\0') {
    fprintf(stderr, "FATAL ERROR: Invalid integer env var: %s\n", name);
    free(val);
    exit(1);
  }

  free(val);
  return (int)x;
}
