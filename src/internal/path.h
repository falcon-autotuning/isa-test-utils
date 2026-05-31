#pragma once
#include "isa-test-utils.h"
#include "isa-test-utils/isa-test-utils-export.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
#define F_OK 0
#else
#include <unistd.h>
#endif

// Replaces g_file_test(path, G_FILE_TEST_EXISTS)
ISA_TEST_UTILS_EXPORT bool file_exists(const char *path);

// Replaces g_rmdir (wipes directory or files cross-platform)
void remove_path(const char *path);

// Replaces g_mkdir_with_parents
bool create_dir_recursive(const char *path);

/* Recursively delete a directory and all of its contents */
void remove_dir_recursive(const char *path);

// Lightweight, platform-invariant path buffer
struct Path {
  char *path_str;  // Managed heap string tracking the absolute/relative path
  size_t length;   // Current character count
  size_t capacity; // Reserved memory safety limit
};

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#define DIR_SEPARATOR_STR "\\"
#define OPPOSITE_SEPARATOR '/'
#else
#define DIR_SEPARATOR '/'
#define DIR_SEPARATOR_STR "/"
#define OPPOSITE_SEPARATOR '\\'
#endif

ISA_TEST_UTILS_EXPORT Path *path_new(const char *initial_path);
ISA_TEST_UTILS_EXPORT char *path_free_to_path(Path *buf);

// Replaces g_file_test(path, G_FILE_TEST_EXISTS)
bool path_file_exists(const Path *path);

// Replaces g_rmdir (wipes directory or files cross-platform)
void path_remove_path(const Path *path);

// Replaces g_mkdir_with_parents
bool path_create_dir_recursive(const Path *path);

/* Recursively delete a directory and all of its contents */
void path_remove_dir_recursive(const Path *path);

void path_set_extension(Path *buf, const char *ext);
