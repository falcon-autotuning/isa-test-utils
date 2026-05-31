#include "isa-test-utils.h"
#include "internal/path.h"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#define F_OK 0
#define strdup _strdup
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* =========================================================================
   Path Buffer Implementation
   ========================================================================= */

Path *path_new(const char *initial_path) {
  Path *buf = calloc(1, sizeof(Path));
  if (!buf)
    return NULL;

  // Allocate an initial capacity with a small safety buffer
  buf->capacity = initial_path ? strlen(initial_path) + 32 : 64;
  buf->path_str = malloc(buf->capacity);
  if (!buf->path_str) {
    free(buf);
    return NULL;
  }

  if (initial_path) {
    strcpy(buf->path_str, initial_path);
    buf->length = strlen(initial_path);
  } else {
    buf->path_str[0] = '\0';
    buf->length = 0;
  }
  return buf;
}

void path_push(Path *buf, const char *element) {
  if (!buf || !element || strlen(element) == 0)
    return;

  size_t element_len = strlen(element);
  // Reserve extra space for directory separator, element text, and a null
  // terminator
  size_t needed = buf->length + element_len + 2;

  if (needed > buf->capacity) {
    buf->capacity = needed * 2;
    char *new_str = realloc(buf->path_str, buf->capacity);
    if (!new_str)
      return;
    buf->path_str = new_str;
  }

  // Append a trailing directory separator if the path string isn't empty
  // and doesn't already end with a slash boundary
  if (buf->length > 0 && buf->path_str[buf->length - 1] != '/' &&
      buf->path_str[buf->length - 1] != '\\') {
    buf->path_str[buf->length++] = DIR_SEPARATOR;
  }

  // Handle cross-platform path transformations (normalize inner slashes
  // seamlessly)
  for (size_t i = 0; i < element_len; i++) {
    char c = element[i];
    if (c == '/' || c == '\\') {
      buf->path_str[buf->length++] = DIR_SEPARATOR;
    } else {
      buf->path_str[buf->length++] = c;
    }
  }
  buf->path_str[buf->length] = '\0';
}

void path_pop(Path *buf) {
  if (!buf || buf->length == 0)
    return;

  // Trim trailing separators if any exist to start clean
  while (buf->length > 0 && (buf->path_str[buf->length - 1] == '/' ||
                             buf->path_str[buf->length - 1] == '\\')) {
    buf->length--;
  }

  // Scan backwards until we hit the parent separator boundary
  size_t i = buf->length;
  while (i > 0) {
    char c = buf->path_str[i - 1];
    if (c == '/' || c == '\\') {
      buf->length = i - 1;

      // Handle the case where the parent is the filesystem root (e.g. "/" or
      // "C:\")
      if (buf->length == 0) {
        buf->length = 1; // Preserve the root "/" separator
      }
#ifdef _WIN32
      if (buf->length == 2 && buf->path_str[1] == ':') {
        buf->length = 3; // Preserve "C:\" separator rules on Windows
      }
#endif
      buf->path_str[buf->length] = '\0';
      return;
    }
    i--;
  }

  // If no directory separator was encountered, the buffer was a flat name;
  // clear it.
  buf->length = 0;
  buf->path_str[0] = '\0';
}

void path_set_extension(Path *buf, const char *ext) {
  if (!buf || buf->length == 0)
    return;

  // 1. Strip away any existing extension by rewinding to the last '.'
  for (size_t i = buf->length; i > 0; i--) {
    char c = buf->path_str[i - 1];
    if (c == '/' || c == '\\')
      break; // Hit folder boundary, no extension exists
    if (c == '.') {
      buf->length = i - 1; // Slice string here
      buf->path_str[buf->length] = '\0';
      break;
    }
  }

  if (!ext)
    return;

  // 2. Append new extension
  size_t ext_len = strlen(ext);
  size_t offset =
      (ext[0] == '.') ? 0 : 1; // Check if we need to manually inject a '.'
  size_t needed = buf->length + ext_len + offset + 1;

  if (needed > buf->capacity) {
    buf->capacity = needed + 16;
    char *new_str = realloc(buf->path_str, buf->capacity);
    if (!new_str)
      return;
    buf->path_str = new_str;
  }

  if (offset == 1) {
    buf->path_str[buf->length++] = '.';
  }
  strcpy(buf->path_str + buf->length, ext);
  buf->length += ext_len;
}

char *path_free_to_path(Path *buf) {
  if (!buf)
    return NULL;
  char *final_path = strdup(buf->path_str);
  path_free(buf);
  return final_path;
}

void path_free(Path *buf) {
  if (!buf) {
    return;
  }
  // Free the underlying string tracker allocation if it exists
  if (buf->path_str) {
    free(buf->path_str);
  }
  // Free the wrapper structure itself
  free(buf);
}

Path *path_clone(const Path *other) {
  if (!other) {
    return NULL;
  }

  Path *clone = calloc(1, sizeof(Path));
  if (!clone) {
    return NULL;
  }

  // Preserve the internal properties and lengths
  clone->length = other->length;
  clone->capacity = other->capacity;

  // Allocate exactly the same capacity layout to ensure memory predictability
  clone->path_str = malloc(clone->capacity);
  if (!clone->path_str) {
    free(clone);
    return NULL;
  }

  // Safely copy string payload context across tracking zones
  if (other->path_str) {
    memcpy(clone->path_str, other->path_str, other->length + 1);
  } else {
    clone->path_str[0] = '\0';
  }

  return clone;
}
bool file_exists(const char *path) {
  if (!path)
    return false;
  return access(path, F_OK) == 0;
}
void remove_path(const char *path) {
  if (!path)
    return;
#ifdef _WIN32
  // Tries removing as a directory first, falls back to standard file unlinking
  if (!RemoveDirectoryA(path)) {
    DeleteFileA(path);
  }
#else
  if (rmdir(path) != 0) {
    unlink(path);
  }
#endif
}
// Replaces g_mkdir_with_parents
bool create_dir_recursive(const char *path) {
  if (!path || strlen(path) == 0) {
    return false;
  }

  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);

  // Clean trailing slashes
  if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
    tmp[len - 1] = '\0';
  }

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char c = *p;
      *p = '\0'; // Temporarily truncate string
#ifdef _WIN32
      CreateDirectoryA(tmp, NULL);
#else
      mkdir(tmp, 0755);
#endif
      *p = c; // Restore character
    }
  }
#ifdef _WIN32
  return CreateDirectoryA(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(tmp, 0755) == 0 || errno == EEXIST;
#endif
}
bool path_file_exists(const Path *path) {
  char *temp = path_free_to_path(path_clone(path));
  bool out = file_exists(temp);
  free(temp);
  return out;
}
void path_remove_path(const Path *path) {
  char *temp = path_free_to_path(path_clone(path));
  remove_path(temp);
  free(temp);
}
// Replaces g_mkdir_with_parents
bool path_create_dir_recursive(const Path *path) {
  char *temp = path_free_to_path(path_clone(path));
  bool out = create_dir_recursive(temp);
  free(temp);
  return out;
}
void remove_dir_recursive(const char *path) {
  if (!path || strlen(path) == 0)
    return;

#ifdef _WIN32
  // --- WINDOWS IMPLEMENTATION ---
  char search_path[MAX_PATH];
  snprintf(search_path, sizeof(search_path), "%s\\*", path);

  WIN32_FIND_DATAA find_data;
  HANDLE hFind = FindFirstFileA(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return; // Directory might not exist or can't be opened
  }

  do {
    // Skip the current (.) and parent (..) directory links
    if (strcmp(find_data.cFileName, ".") == 0 ||
        strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    char child_path[MAX_PATH];
    snprintf(child_path, sizeof(child_path), "%s\\%s", path,
             find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      remove_dir_recursive(child_path);
    } else {
      // Force remove files (even if read-only)
      SetFileAttributesA(child_path, FILE_ATTRIBUTE_NORMAL);
      DeleteFileA(child_path);
    }
  } while (FindNextFileA(hFind, &find_data));

  FindClose(hFind);
  RemoveDirectoryA(path);

#else
  // --- POSIX LINUX IMPLEMENTATION ---
  DIR *d = opendir(path);
  if (!d) {
    return; // Directory doesn't exist or can't be opened
  }

  struct dirent *p;
  while ((p = readdir(d))) {
    if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
      continue;
    }

    size_t len = strlen(path) + strlen(p->d_name) + 2;
    char *child_path = malloc(len);
    if (!child_path)
      continue;
    snprintf(child_path, len, "%s/%s", path, p->d_name);

    struct stat statbuf;
    if (stat(child_path, &statbuf) == 0) {
      if (S_ISDIR(statbuf.st_mode)) {
        remove_dir_recursive(child_path);
      } else {
        unlink(child_path);
      }
    }
    free(child_path);
  }
  closedir(d);
  rmdir(path);
#endif
}
void path_remove_dir_recursive(const Path *path) {
  return remove_dir_recursive(path_free_to_path(path_clone(path)));
}
