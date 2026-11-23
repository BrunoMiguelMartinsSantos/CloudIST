#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int path_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size){
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size) {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}

int read_all(int fd, void *buffer, size_t size) {

  size_t bytes_read = 0;
  while (bytes_read < size) {
    ssize_t result = read(fd, buffer + bytes_read, size - bytes_read);
    if (result == -1) {
      perror("Failed to read from pipe");
      return -1;
    } else if (result == 0) {
      return 0;
    }

    bytes_read += (size_t)result;
  }

  return 1;
}

int write_all(int fd, const void *buffer, size_t size) {
  size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd, buffer + bytes_written, size - bytes_written);
    if (result == -1) {
      perror("Failed to write to pipe");
      return -1;
    }

    bytes_written += (size_t)result;
  }

  return 1;
}

static int copy_file(const char *src, const char *dst) {
  int src_fd = open(src, O_RDONLY);

  if (src_fd == -1)
    return 1;

  int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (dst_fd == -1) {
    close(src_fd);
    return 1;
  }

  char buf[4096];
  int ret = 0;
  ssize_t bytes_read;

  while ((bytes_read = read(src_fd, buf, sizeof(buf))) > 0) {
    if (write_all(dst_fd, buf, (size_t)bytes_read) != 1) {
      ret = 1;
      break;
    }
  }

  if (bytes_read < 0)
    ret = 1;

  close(src_fd);
  close(dst_fd);

  return ret;
}

int copy_dir_recursive(const char *src, const char *dst) {
  DIR *dir = opendir(src);

  // opendir() fails when src is not a directory, treating it as a plain file.
  if (dir == NULL)
    return copy_file(src, dst);

  // Creates dst itself. Its parents must already exist.
  if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
    closedir(dir);
    return 1;
  }

  int ret = 0;
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];

    int src_len = snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
    int dst_len = snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

    if (src_len < 0 || (size_t)src_len >= sizeof(src_path) ||
        dst_len < 0 || (size_t)dst_len >= sizeof(dst_path)) {
      ret = 1;
      continue;
    }

    if (copy_dir_recursive(src_path, dst_path) != 0)
      ret = 1;
  }

  closedir(dir);

  return ret;
}

int remove_dir_recursive(const char *path) {
  DIR *dir = opendir(path);

  // Same trick as copy_dir_recursive: opendir() fails when path is not a
  // directory, so it is a plain file and a single unlink() is enough.
  if (dir == NULL) {
    if (unlink(path) != 0 && errno != ENOENT)
      return 1;
    return 0;
  }

  int ret = 0;
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char child[PATH_MAX];

    int len = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

    if (len < 0 || (size_t)len >= sizeof(child)) {
      ret = 1;
      continue;
    }

    if (remove_dir_recursive(child) != 0)
      ret = 1;
  }

  closedir(dir);

  if (rmdir(path) != 0 && errno != ENOENT)
    ret = 1;

  return ret;
}