#ifndef FILESYSTEM__H
#define FILESYSTEM__H

#include <stddef.h>

/**
 * Checks whether a path exists and is a directory.
 *
 * @param path Directory path.
 *
 * @return 1 if it exists and is a directory, 0 otherwise.
 */
int path_exists(const char *path);

/**
 * Checks whether a path exists and is a regular file.
 *
 * @param path File path.
 *
 * @return 1 if it exists and is a regular file, 0 otherwise.
 */
int file_exists(const char *path);

/**
 * Converts the given path into an absolute path, resolving symbolic links,
 * relative components ('.' and '..'), and redundant separators. The resolved
 * path is copied into the provided buffer.
 *
 * @param path Path to resolve.
 * @param buffer Destination buffer where the absolute path will be stored.
 * @param size Size of the destination buffer, in bytes.
 *
 * @return 0 if the path was successfully resolved and copied to the buffer
 * @return 1 if the path could not be resolved or the buffer is too small.
 */
int absolute_path(const char *path, char *buffer, size_t size);

/**
 * Copies src into dst recursively, keeping the same structure. If src is a
 * directory, dst is created and every entry inside is copied; if src is a
 * file, it is simply copied.
 *
 * Only dst itself is created, so the parent directories of dst must already
 * exist.
 *
 * Only directories and regular files are handled, using read()/write() and
 * opendir()/readdir() (no stdio streams).
 *
 * @param src Source directory (or file) to copy.
 * @param dst Destination path.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int copy_dir_recursive(const char *src, const char *dst);

/**
 * Removes path recursively: if it is a directory, everything inside it is
 * removed first and then the directory itself; if it is a file, it is just
 * unlinked. A path that does not exist is not an error.
 *
 * @param path Directory (or file) to remove.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int remove_dir_recursive(const char *path);

/// Reads a given number of bytes from a file descriptor. Will block until all
/// bytes are read, or fail if not all bytes could be read.
/// @param fd File descriptor to read from.
/// @param buffer Buffer to read into.
/// @param size Number of bytes to read.
/// @return On success, returns 1, on end of file, returns 0, on error, returns -1
int read_all(int fd, void *buffer, size_t size);

/// Writes a given number of bytes to a file descriptor. Will block until all
/// bytes are written, or fail if not all bytes could be written.
/// @param fd File descriptor to write to.
/// @param buffer Buffer to write from.
/// @param size Number of bytes to write.
/// @return On success, returns 1, on error, returns -1
int write_all(int fd, const void *buffer, size_t size);

#endif // FILESYSTEM__H