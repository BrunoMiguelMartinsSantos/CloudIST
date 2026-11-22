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
}

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


int main(int argc, char **argv){
	if (argc != 8) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <local_cores> <input_dir> <max_threads>\n", argv[0]);
    return 1;
  	}
	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;
	size_t local_cores;
	size_t max_threads;

	DataCenter dc;
	datacenter_init(&dc);	

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_size_t_arg(argv[5], &local_cores) != 0 ||
		    parse_size_t_arg(argv[7], &max_threads) != 0 ||
			local_cores == 0 ||
  			max_threads == 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}
	char *input_dir = argv[6];
	if (!path_exists(input_dir)) {
		fprintf(stderr, "Invalid input directory.\n");
		datacenter_destroy(&dc);
		return 1;
	}
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
	struct dirent **conf_files = NULL;
	int conf_count = 0;

    if (list_conf_files(input_dir, &conf_files, &conf_count) != 0) {
        datacenter_destroy(&dc);
        return 1;
    }
	pthread_t wait_thread;
	if (pthread_create(&wait_thread,NULL,vm_wait_thread,&dc) != 0) {
		fprintf(stderr, "Failed to create wait thread.\n");
		free_file_list(conf_files, conf_count);
		datacenter_destroy(&dc);
		return 1;
	}
	WorkerArgs worker_args; // Cria a estrutura que vai guardar os dados partilhados pelas worker threads.
	worker_args.dc = &dc;
	worker_args.conf_files = conf_files;
	worker_args.conf_count = conf_count;
	worker_args.input_dir = input_dir;
	worker_args.next_file = 0;
	worker_args.error = 0;
	pthread_mutex_init(&worker_args.work_mutex, NULL);
	size_t num_workers = max_threads;
	if ((size_t)conf_count < num_workers) num_workers = (size_t)conf_count;
	pthread_t *workers = NULL;
	if (num_workers > 0) {
		workers = malloc(num_workers * sizeof(pthread_t));
		if (workers == NULL) {
			fprintf(stderr, "Failed to allocate worker threads.\n");
			pthread_mutex_destroy(&worker_args.work_mutex);
			free_file_list(conf_files, conf_count);
			pthread_mutex_lock(&dc.mutex);
			dc.shutting_down = 1;
			pthread_cond_signal(&dc.child_cond);
			pthread_mutex_unlock(&dc.mutex);
			pthread_join(wait_thread, NULL);
			datacenter_destroy(&dc);
			return 1;
		}
	}
	size_t workers_created = 0;
	for (size_t i = 0; i < num_workers; i++) {
		if (pthread_create(&workers[i], NULL, worker_thread, &worker_args) != 0) {
			pthread_mutex_lock(&dc.output_mutex);
			fprintf(stderr, "Failed to create worker thread.\n");
			pthread_mutex_unlock(&dc.output_mutex);
			pthread_mutex_lock(&worker_args.work_mutex);
			worker_args.error = 1;
			pthread_mutex_unlock(&worker_args.work_mutex);
			break;
		}
		workers_created++;
	}
	for (size_t i = 0; i < workers_created; i++) {
		pthread_join(workers[i], NULL);
	}
	pthread_mutex_destroy(&worker_args.work_mutex);
	free(workers);
	free_file_list(conf_files, conf_count);
	pthread_mutex_lock(&dc.mutex);
	dc.shutting_down = 1;
	pthread_cond_signal(&dc.child_cond);
	pthread_mutex_unlock(&dc.mutex);
	pthread_join(wait_thread, NULL);
	int worker_error = worker_args.error;
	datacenter_destroy(&dc);
	if (worker_error) return 1;
	return 0;
}