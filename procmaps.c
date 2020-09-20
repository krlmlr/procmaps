#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procmaps.h"

#define INIT_BUFFER_SIZE (64 * 1024)
#define MAX_BUFFER_SIZE (10 * 1024 * 1024)

#if defined(__linux__) || defined(__CYGWIN__) || defined(__CYGWIN32__)
#define MAPS_PATH "/proc/%d/maps"
#define MAPS_SELF_PATH "/proc/self/maps"
#elif defined(__FreeBSD__)
#define MAPS_PATH "/proc/%d/map"
#define MAPS_SELF_PATH "/proc/curproc/map"
#elif defined(__sun__)
#define MAPS_PATH "/proc/%d/map"
#define MAPS_SELF_PATH "/proc/self/map"
#else
#define MAPS_PATH ""
#define MAPS_SELF_PATH ""
#endif

/**
 * Returns true if EOF was reached and the whole file is in the buffer,
 * otherwise false
 */
static bool read_file(const char *path, char *buffer, size_t buffer_size,
                      size_t *bytes_read) {
  bool res = false;
  FILE *file;

  file = fopen(path, "rb");
  if (!file) {
    return res;
  }

  *bytes_read = fread(buffer, 1, buffer_size, file);

  if (feof(file) && *bytes_read != 0) {
    res = true;
  }

  fclose(file);
  return res;
}

/*
 * Allocates a buffer and reads the whole file into it. Returns false if an
 * error occurred or the the file is bigger than MAX_BUFFER_SIZE
 */
static bool read_file_to_buffer(const char *path, char **buffer_out,
                                size_t *buffer_size_out,
                                size_t *file_size_out) {
  char *buffer = malloc(INIT_BUFFER_SIZE);
  size_t buffer_size = INIT_BUFFER_SIZE;
  bool res = false;

  while (buffer && buffer_size <= MAX_BUFFER_SIZE && !res) {
    res = read_file(path, buffer, buffer_size, file_size_out);
    if (!res) {
      /* In case of an error, increase buffer size and try again */
      buffer_size *= 2;
      buffer = realloc(buffer, buffer_size);
    }
  }

  if (res) {
    *buffer_out = buffer;
    *buffer_size_out = buffer_size;
  } else {
    free(buffer);
  }

  return res;
}

/*
 * Allocates a buffer big enough to read the whole file into memory
 */
static char *read_file_to_string(const char *path) {
  char *buffer;
  size_t buffer_size;
  size_t file_size;

  if (!read_file_to_buffer(path, &buffer, &buffer_size, &file_size)) {
    return NULL;
  }

  if (file_size == buffer_size) {
    /* Make room for the terminating \0 */
    buffer = realloc(buffer, buffer_size + 1);
    if (!buffer) {
      return NULL;
    }
  }

  /* Add a terminating 0 to turn the buffer into a string */
  buffer[file_size] = '\0';

  return buffer;
}

/**
 * Calls the callback cb for every correctly parsed row
 */
bool get_proc_self_maps(parse_callback cb, void *data) {
  char *content, *pos;
  unsigned int total_rows = 0, row = 0;

  uint64_t start, end, offset;
  int64_t inode;
  char flags[5];
  unsigned int major, minor;
  int filename_offset;
  char *filename;

  content = read_file_to_string(MAPS_SELF_PATH);
  if (!content) {
    return false;
  }

  pos = content;
  while (*pos != 0) {
    if (*pos == '\n') {
      ++total_rows;
    }
    ++pos;
  }

  pos = content;
  while (*pos != 0) {
    if (sscanf(pos,
               "%" SCNx64 "-%" SCNx64 " %4s %" SCNx64 " %x:%x %" SCNd64 "%n",
               &start, &end, flags, &offset, &major, &minor, &inode,
               &filename_offset) != 7) {
      break;
    }

    pos += filename_offset;

    /* Skip whitespace */
    while (*pos == ' ' || *pos == '\t' || *pos == '\r') {
      ++pos;
    }

    filename = pos;

    /* Find newline */
    while (*pos != 0 && *pos != '\n') {
      ++pos;
    }
    *pos = '\0';

    cb(data, row, total_rows, start, end, flags, offset, inode, filename);

    pos += 1;
    ++row;
  }

  free(content);
  return false;
}