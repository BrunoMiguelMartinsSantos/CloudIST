#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include "datacenter_utils.h"
#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h"
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

// Informação partilhada pelas worker threads.
typedef struct {
    DataCenter *dc;
    struct dirent **conf_files;
    int conf_count;
    const char *input_dir; // diretoria onde estão os .conf
    int next_file;
    pthread_mutex_t work_mutex; // protege next_file para dois workers não pegarem no mesmo ficheiro
    int error;
} WorkerArgs;

/*
// Cada worker vai buscar um ficheiro .conf ainda não processado e executa os seus comandos.
static void *worker_thread(void *arg) {
    WorkerArgs *args = (WorkerArgs *)arg;
    while (1) {
        pthread_mutex_lock(&args->work_mutex);
        if (args->next_file >= args->conf_count) {
            pthread_mutex_unlock(&args->work_mutex);
            return NULL;
        }
        int i = args->next_file;
        args->next_file++;
        pthread_mutex_unlock(&args->work_mutex);
        char conf_path[MAX_PATH_SIZE];
        int written = snprintf(conf_path, sizeof(conf_path), "%s/%s", args->input_dir, args->conf_files[i]->d_name);
        if (written < 0 || (size_t)written >= sizeof(conf_path)) {
			pthread_mutex_lock(&args->dc->output_mutex);
            fprintf(stderr, "Configuration file path is too long.\n");
            pthread_mutex_unlock(&args->dc->output_mutex);
			pthread_mutex_lock(&args->work_mutex);
            args->error = 1;
            pthread_mutex_unlock(&args->work_mutex);
            return NULL;
        }

        int fd = open(conf_path, O_RDONLY);
        if (fd == -1) {
			pthread_mutex_lock(&args->dc->output_mutex);
            perror("open");
			pthread_mutex_unlock(&args->dc->output_mutex);
            pthread_mutex_lock(&args->work_mutex);
            args->error = 1;
            pthread_mutex_unlock(&args->work_mutex);
            return NULL;
        }
        process_commands(args->dc, fd);
        close(fd);
    }
}
*/


int main(int argc, char **argv){
	if (argc != 8) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <local_cores> <max_clients> <register_fifo>\n", argv[0]);
    return 1;
  	}
	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;
	size_t local_cores;
	size_t max_clients;

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_size_t_arg(argv[5], &local_cores) != 0 ||
			parse_size_t_arg(argv[6], &max_clients) != 0 ||
			local_cores == 0 ||
			max_clients == 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}
	char *register_fifo = argv[7];

	DataCenter dc;
	datacenter_init(&dc);	

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		return 1;
	}
	dc.local_cores = local_cores;

	if (mkfifo(register_fifo, S_IRUSR | S_IWUSR) == -1) {
    	perror("mkfifo");
    	return 1;
	}

	int register_fd = open(register_fifo, O_RDONLY);
	if (register_fd == -1) {
		perror("open");
		unlink(register_fifo);
		return 1;
	}
    
	int register_write_fd = open(register_fifo, O_WRONLY);
    if (register_write_fd == -1) {
        perror("open");
        close(register_fd);
        unlink(register_fifo);
        return 1;
    }

	ConnectRequest request;

    while ((read_all(register_fd, &request, sizeof request)) == 1) {
        if (request.opcode != 1) {
            fprintf(stderr, "Unexpected opcode on register FIFO.\n");
            continue;
        }

        int request_fd = open(request.request_path, O_RDONLY);
        if (request_fd == -1) {
            perror(request.request_path);
            continue;
        }

        int response_fd = open(request.response_path, O_WRONLY);
        if (response_fd == -1) {
            perror(request.response_path);
            close(request_fd);
            continue;
        }

        ConnectResponse response;
        response.result = 0;
        response.num_servers = dc.num_servers;
        response.ram = dc.servers[0].total.ram;
        response.disk = dc.servers[0].total.disk;
        response.cpus = dc.servers[0].total.cpu;

		// O cliente espera pela resposta ao CONNECT antes de enviar comandos.
        if (write_all(response_fd, &response, sizeof response) != 1) {
            close(request_fd);
            close(response_fd);
            continue;
        }

        
        char opcode;
        bool end_client = false;
		while (read_all(request_fd, &opcode, sizeof opcode) == 1) {
			switch (opcode) {
				case DISCONNECT: {
					end_client = true;
                    break;
                }

				case DEFINE: {
					// O opcode já foi lido. Falta receber a definição da VM.
					VMType vm_type;
					if (read_all(request_fd, &vm_type, sizeof vm_type) != 1) {
						
                        break;
					}

					char result = 0;

					pthread_rwlock_rdlock(&dc.state_lock);
					pthread_mutex_lock(&dc.mutex);
					if (datacenter_define_VM(&dc, &vm_type) != 0) {
						result = 1;
					}
					pthread_mutex_unlock(&dc.mutex);
					pthread_rwlock_unlock(&dc.state_lock);

					// O cliente recebe 0 se a definição foi aceite e 1 se falhou.
                    if (write_all(response_fd, &result, sizeof result) != 1) {
					    break;
					}
                }
				case 4:  // R: reservar
				case 5:  // A: arrancar
				case 6:  // L: listar
				
                default:
					break;
                
            }
            if(end_client) break;
		}

        close(request_fd);
        close(response_fd);
    }

    close(register_write_fd);
    close(register_fd);
    unlink(register_fifo);
    datacenter_destroy(&dc);
    return 0;
}