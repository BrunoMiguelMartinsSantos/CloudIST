#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>  // ADICIONADO: opendir(), readdir(), closedir()
#include <errno.h>   // ADICIONADO: errno e EEXIST
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

// ADICIONADO: Verifica se o nome do ficheiro termina em ".conf".
static int has_conf_extension(const char *name) {
    size_t len = strlen(name);
    const char *extension = ".conf";
    size_t extension_len = strlen(extension);
    if (len < extension_len)
        return 0;
    return strcmp(name + len - extension_len, extension) == 0;
}
/*
 * ADICIONADO:
 * Função de comparação utilizada pelo qsort()
 * para ordenar alfabeticamente os nomes.
 */
static int compare_names(const void *a, const void *b) {
    const char *const *name_a = a;
    const char *const *name_b = b;
    return strcmp(*name_a, *name_b);
}
/*
 * ADICIONADO:
 * Liberta os nomes dos ficheiros e depois
 * o próprio vetor que os contém.
 */
void free_file_list(char **files, size_t count) {
    for (size_t i = 0; i < count; i++)
        free(files[i]);
    free(files);
}
/*
 * ADICIONADO:
 * Percorre uma diretoria, guarda apenas os ficheiros .conf
 * e ordena os seus nomes alfabeticamente.
 */
int list_conf_files(const char *dir_path, char ***files, size_t *count) {
    // Abre a diretoria recebida em INPUT_DIR.
    DIR *dir = opendir(dir_path);
    if (dir == NULL)
        return 1;
    // vai ser o vetor onde vais guardar os nomes dos ficheiros encontrados
    char **result = NULL;
    size_t result_count = 0;
    // variável que vai apontar para a entrada atual da diretoria
    struct dirent *entry;
    // Percorre todas as entradas da diretoria.
    while ((entry = readdir(dir)) != NULL) {
         // Ignora tudo o que não termina em .conf.
        if (!has_conf_extension(entry->d_name))
            continue;
        char full_path[MAX_PATH_SIZE];
        // constrói a string com este formato e guarda-a em full_path
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (!file_exists(full_path))
          continue;
        // Reserva memória para guardar uma cópia do nome do ficheiro.
        char *name_copy = strdup(entry->d_name);
        if (name_copy == NULL) {
            free_file_list(result, result_count);
            closedir(dir);
            return 1;
        }
        // Aumenta o vetor para caber mais um nome.
        char **new_result = realloc(result, (result_count + 1) * sizeof(char *));
        if (new_result == NULL) {
            free(name_copy);
            free_file_list(result, result_count);
            closedir(dir);
            return 1;
        }
        result = new_result;
        result[result_count] = name_copy;
        result_count++;
    }
    closedir(dir);
    // ADICIONADO: Ordena alfabeticamente os .conf
    qsort(result, result_count, sizeof(char *), compare_names);
    // Devolve ao main a lista e o número de ficheiros encontrados.
    *files = result;
    *count = result_count;
    return 0;
}
/*
 * ADICIONADO:
 * Cria uma diretoria.
 * Se já existir uma diretoria com esse nome, não é considerado erro.
 */
static int create_directory(const char *path) {
    if (mkdir(path, S_IRWXU) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    return 1;
}
/*
 * ADICIONADO: Copia um ficheiro usando descritores POSIX:
 * open(), read(), write() e close().
 * src -> ficheiro de onde se vai copiar
 * dst -> ficheiro para onde se vai copiar
 */
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
            ssize_t bytes_written =
                write(dst_fd, buffer + total_written, (size_t)(bytes_read - total_written));
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
    if (bytes_read == -1)
        return 1;
    return 0;
}
/*
 * ADICIONADO:
 * Copia recursivamente todo o conteúdo de uma diretoria, preservando os seus subdiretórios.
*/
static int copy_directory_contents(const char *src, const char *dst) {
    DIR *dir = opendir(src);
    if (dir == NULL)
        return 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Ignora a própria diretoria e a diretoria pai.
        * "."  -> ignorar para não voltar à mesma pasta
        * ".." -> ignorar para não subir para a pasta pai (Ex: input/.. -> vai para fora do input)
        */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
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
/*
 * ADICIONADO: Cria a diretoria correspondente à VM:
 * /tmp/CloudIST/<ID_RESERVA>/<ID_VM> e copia para lá todo o conteúdo de input_dir.
 */
int prepare_vm_filesystem(const char *reservation_id, const char *vm_id, const char *input_dir) {
    char reservation_dir[MAX_PATH_SIZE];
    char vm_dir[MAX_PATH_SIZE];
    // Cria a diretoria base.
    if (create_directory("/tmp/CloudIST") != 0)
        return 1;
    // Exemplo: /tmp/CloudIST/R1
    snprintf(reservation_dir, sizeof(reservation_dir), "/tmp/CloudIST/%s", reservation_id);
    if (create_directory(reservation_dir) != 0)
        return 1;
    // Constrói o caminho /tmp/CloudIST/<ID_RESERVA>/<ID_VM>
    // e guarda em written o número de caracteres necessários para o caminho.
    int written = snprintf(vm_dir, sizeof(vm_dir), "%s/%s", reservation_dir, vm_id);
    if (written < 0 || (size_t)written >= sizeof(vm_dir))
        return 1;
    if (create_directory(vm_dir) != 0)
        return 1;
    // Copia os ficheiros de input para a diretoria da VM.
    return copy_directory_contents(input_dir, vm_dir);
}