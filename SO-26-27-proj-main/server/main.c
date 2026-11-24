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

/*
static int process_commands(DataCenter *dc, int fd) {
	while(1){
		switch (get_next_command(fd)){
			case CMD_DEFINE: {
				VMType vmtype;
				// Lê os argumentos do ficheiro .conf atual
				if (parse_define(fd, &vmtype) != 0) {
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_rwlock_rdlock(&dc->state_lock);
				pthread_mutex_lock(&dc->mutex);
				int result = datacenter_define_VM(dc, &vmtype);
				pthread_mutex_unlock(&dc->mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) {
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to define VM.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("VM successfully defined!\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);
				if (num_items == 0) {
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_rwlock_rdlock(&dc->state_lock);
				int result = datacenter_reserve(dc,&reservation);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to reserve VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("Reservation made successfully!\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];
				if(parse_execute(fd, id) != 0){
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_rwlock_rdlock(&dc->state_lock);
				pthread_mutex_lock(&dc->mutex);
				int result = datacenter_execute(dc, id);
				pthread_mutex_unlock(&dc->mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to execute reservation.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("Finished reservation execution!\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_LIST: {
				pthread_rwlock_wrlock(&dc->state_lock);
				pthread_mutex_lock(&dc->output_mutex);
				int result = datacenter_list(dc);
				pthread_mutex_unlock(&dc->output_mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to list VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				break;
			}

			case CMD_WAIT: {
				unsigned int delay;
				if(parse_wait(fd, &delay) != 0){
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid wait command. See H (help) for usage.\n");		
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				datacenter_wait(delay);
				break;
			}
			case CMD_INVALID:
				pthread_mutex_lock(&dc->output_mutex);
				fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;

			case CMD_HELP:
				pthread_mutex_lock(&dc->output_mutex);
				printf(
					"Spaces between arguments are allowed, but not after command end.\n"
					"Available commands:\n"
					" D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> <RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>\n"
					" R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+\n"
					" A <RESERVATION_ID>\n"
					" L\n"
					" E <DELAY_MS>\n"
					" H\n"
				);
				pthread_mutex_unlock(&dc->output_mutex);
				break;

			case CMD_EMPTY:
				break;

			case EOC:
				return 0;
		}
	}
}*/

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

int accept_client(int register_fd, const DataCenter *dc, unsigned int *client_id, int *request_fd, int *response_fd) {
	char msg[1 + 2 * MAX_PIPE_PATH_LENGTH + sizeof(*client_id)];
	if (read_all(register_fd, msg, sizeof(msg)) != 1 || msg[0] != 1) return 1;
	char const *req_path = msg + 1;
	char const *resp_path = msg + 1 + MAX_PIPE_PATH_LENGTH;
	memcpy(client_id, msg + 1 + 2 * MAX_PIPE_PATH_LENGTH, sizeof(*client_id));
	*request_fd = open(req_path, O_RDONLY);
    if (*request_fd == -1) {
        return 1;
    }
    *response_fd = open(resp_path, O_WRONLY);
    if (*response_fd == -1) {
        close(*request_fd);
        return 1;
    }
	char result = 0;
	const Resources *total = &dc->servers[0].total;
	if (write_all(*response_fd, &result, sizeof(result)) != 1 ||
        write_all(*response_fd, &dc->num_servers, sizeof(dc->num_servers)) != 1 ||
        write_all(*response_fd, &total->ram, sizeof(total->ram)) != 1 ||
        write_all(*response_fd, &total->disk, sizeof(total->disk)) != 1 ||
        write_all(*response_fd, &total->cpu, sizeof(total->cpu)) != 1) {
        close(*request_fd);
        close(*response_fd);
        return 1;
    }
	return 0;
}


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

	int status;

	while (1) {
        unsigned int client_id;
        int request_fd, response_fd;
        status = accept_client(register_fd, &dc, &client_id, &request_fd, &response_fd);
        if (status != 0) break;
        char opcode;
        int received = read_all(request_fd, &opcode, sizeof(opcode));
        close(request_fd);
        close(response_fd);
    }
    close(register_write_fd);
    close(register_fd);
    unlink(register_fifo);
    datacenter_destroy(&dc);
    return status;
}