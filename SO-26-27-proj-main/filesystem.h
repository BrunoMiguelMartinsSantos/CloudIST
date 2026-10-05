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

/*
 * ADICIONADO:
 * Procura os ficheiros .conf existentes numa diretoria
 * e guarda os seus nomes por ordem alfabética.
 */
int list_conf_files(const char *dir_path, char ***files, size_t *count);

/*
 * ADICIONADO:
 * Liberta a memória usada pela lista de ficheiros .conf.
 */
void free_file_list(char **files, size_t count);

/*
 * ADICIONADO:
 * Cria a diretoria correspondente a uma VM em /tmp/CloudIST
 * e copia para lá os ficheiros da diretoria de input.
 */
int prepare_vm_filesystem(const char *reservation_id, const char *vm_id, const char *input_dir);

#endif // FILESYSTEM__H