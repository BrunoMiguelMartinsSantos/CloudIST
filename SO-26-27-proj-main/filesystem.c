#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include "constants.h"

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

// Exercício 1: Verifica se o nome do ficheiro termina em ".conf".
static int has_conf_extension(const char *name) {
    size_t len = strlen(name);
    const char *extension = ".conf";
    size_t extension_len = strlen(extension);
    if (len < extension_len)
        return 0;
    return strcmp(name + len - extension_len, extension) == 0;
}

// Exercício 1: Liberta os nomes dos ficheiros e depois o próprio vetor que os contém.
void free_file_list(struct dirent **files, int count) {
    // Liberta cada entrada criada pelo scandir().
    for (int i = 0; i < count; i++)
        free(files[i]);
    // Liberta o vetor de entradas.
    free(files);
}

// Exercício 1: Aceita apenas entradas cujo nome termina em ".conf".
static int is_conf(const struct dirent *entry) {
    return has_conf_extension(entry->d_name);
}

// Exercício 1: Percorre uma diretoria, guarda apenas os ficheiros .conf e ordena os seus nomes alfabeticamente.
int list_conf_files(const char *dir_path, struct dirent ***files, int *count) {
    // Percorre a diretoria, guarda apenas os ficheiros .conf e ordena-os alfabeticamente.
    int n = scandir(dir_path, files, is_conf, alphasort);
    if (n < 0) {
        perror("scandir");
        return 1;
    }
    *count = n;
    return 0;
}

// Exercício 1: Cria uma diretoria. Se já existir uma diretoria com esse nome, não é considerado erro.
static int create_directory(const char *path) {
    if (mkdir(path, S_IRWXU) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    return 1;
}

// Exercício 1
static int copy_file(const char *src, const char *dst) {
    int src_fd = open(src, O_RDONLY);
    if (src_fd == -1)
        return 1;
    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (dst_fd == -1) {
        close(src_fd);
        return 1;
    }
    char buffer[BUF_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t bytes_written = write(dst_fd, buffer + total_written, (size_t)(bytes_read - total_written));
            if (bytes_written <= 0) {
                close(src_fd);
                close(dst_fd);
                return 1;
            }
            total_written += bytes_written;
        }
    }
    close(src_fd);
    close(dst_fd);
    if (bytes_read == -1) return 1;
    return 0;
}

// Exercício 1: Copia recursivamente todo o conteúdo de uma diretoria, preservando os seus subdiretórios.
static int copy_directory_contents(const char *src, const char *dst) {
    DIR *dir = opendir(src);
    if (dir == NULL) return 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Ignora a própria diretoria e a diretoria pai.
        * "." : ignorar para não voltar à mesma pasta
        * ".." : ignorar para não subir para a pasta pai
        */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char src_path[MAX_PATH_SIZE];
        char dst_path[MAX_PATH_SIZE];
        // Caminho do elemento na diretoria de origem.
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        // Caminho correspondente na diretoria de destino.
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        struct stat st;
        if (stat(src_path, &st) != 0) {
            closedir(dir);
            return 1;
        }
        // Se for uma diretoria: cria-a no destino e copia o seu conteúdo recursivamente.
        if (S_ISDIR(st.st_mode)) {
            if (create_directory(dst_path) != 0) {
                closedir(dir);
                return 1;
            }
            if (copy_directory_contents(src_path, dst_path) != 0) {
                closedir(dir);
                return 1;
            }
        }
        // Se for um ficheiro normal, copia-o.
        else if (S_ISREG(st.st_mode)) {
            if (copy_file(src_path, dst_path) != 0) {
                closedir(dir);
                return 1;
            }
        }
    }
    closedir(dir);
    return 0;
}

// Exercício 1: Cria a diretoria correspondente à VM: /tmp/CloudIST/<ID_RESERVA>/<ID_VM> e copia para lá todo o conteúdo de input_dir.
int prepare_vm_filesystem(const char *reservation_id, const char *vm_id, const char *input_dir) {
    char reservation_dir[MAX_PATH_SIZE];
    char vm_dir[MAX_PATH_SIZE];
    // Cria a diretoria base.
    if (create_directory("/tmp/CloudIST-13") != 0) return 1; // acrescentei -13
    // Exemplo: /tmp/CloudIST-13/R1
    snprintf(reservation_dir, sizeof(reservation_dir), "/tmp/CloudIST-13/%s", reservation_id); // acrescentei -13
    if (create_directory(reservation_dir) != 0) return 1;
    // Constrói o caminho /tmp/CloudIST/<ID_RESERVA>/<ID_VM> e guarda em written o número de caracteres necessários para o caminho.
    int written = snprintf(vm_dir, sizeof(vm_dir), "%s/%s", reservation_dir, vm_id);
    if (written < 0 || (size_t)written >= sizeof(vm_dir)) return 1;
    if (create_directory(vm_dir) != 0) return 1;
    // Copia os ficheiros de input para a diretoria da VM.
    return copy_directory_contents(input_dir, vm_dir);
}