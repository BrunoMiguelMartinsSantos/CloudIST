#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // ADICIONADO: necessário para open() e O_RDONLY

#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h" // ADICIONADO: funções para obter os ficheiros .conf

/*
 * ALTERADO:
 * Antes os comandos eram lidos de STDIN_FILENO.
 * Agora recebemos 'fd', que é o descritor do ficheiro .conf atual.
 */
static int process_commands(DataCenter *dc, int fd) {
	while(1){
		switch (get_next_command(fd)){ // ALTERADO: STDIN_FILENO -> fd
			case CMD_DEFINE: {
				VMType vmtype;
				// ALTERADO: lê os argumentos do ficheiro .conf atual
				if (parse_define(fd, &vmtype) != 0) {
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					continue;
				}
				/*
				 * dc já é DataCenter *
				 * Por isso é dc e NÃO &dc.
				*/
				if(datacenter_define_VM(dc, &vmtype) != 0){
					fprintf(stderr, "Failed to define VM.\n");
					continue;
				}
				printf("VM successfully defined!\n");
				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};
				// ALTERADO: lê a reserva do ficheiro .conf atual
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

				if (num_items == 0) {
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					continue;
				}
				// CORRIGIDO: dc em vez de &dc
				if (datacenter_reserve(dc, &reservation) != 0) {
					fprintf(stderr, "Failed to reserve VMs.\n");
					continue;
				}
				printf("Reservation made successfully!\n");
				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];
				// ALTERADO: lê o ID do ficheiro .conf atual
				if(parse_execute(fd, id) != 0){
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					continue;
				}
				// CORRIGIDO: dc em vez de &dc
				if (datacenter_execute(dc, id) != 0) {
					fprintf(stderr, "Failed to execute reservation.\n");
					continue;
				}
				printf("Finished reservation execution!\n");
				break;
			}
			case CMD_LIST:
				// CORRIGIDO: dc em vez de &dc
				if (datacenter_list(dc) != 0) {
					fprintf(stderr, "Failed to list VMs.\n");
					continue;
				}
				break;

			case CMD_WAIT: {
				unsigned int delay;
				// ALTERADO: lê o tempo do ficheiro .conf atual
				if(parse_wait(fd, &delay) != 0){
					fprintf(stderr, "Invalid wait command. See H (help) for usage.\n");
					continue;
				}
				datacenter_wait(delay);
				break;
			}
			case CMD_INVALID:
				fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
				break;

			case CMD_HELP:
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
				break;

			case CMD_EMPTY:
				break;

			case EOC:
				/*
				 * ALTERADO:
				 * EOC significa apenas que chegámos ao fim deste .conf.
				 * NÃO destruímos o DataCenter, porque ainda pode haver
				 * outros ficheiros .conf para processar.
				 */
				return 0;
		}
	}
}

int main(int argc, char **argv){
		/*
	 * ALTERADO:
	 * Agora existe mais um argumento: INPUT_DIR.
	 */
	if (argc != 6) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <input_dir>\n", argv[0]);
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;

	DataCenter dc;
	datacenter_init(&dc);	

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	/*
	 * ADICIONADO:
	 * argv[5] contém a diretoria onde estão os ficheiros .conf.
	 */
	char *input_dir = argv[5];

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		return 1;
	}
	/*
	 * ADICIONADO:
	 * conf_files vai guardar os nomes dos ficheiros .conf.
	 * conf_count guarda quantos foram encontrados.
	 */
	char **conf_files = NULL;
    size_t conf_count = 0;

	/*
	 * ADICIONADO:
	 * Obtém todos os .conf existentes em INPUT_DIR.
	 * A função também os coloca por ordem alfabética.
	 */
    if (list_conf_files(input_dir, &conf_files, &conf_count) != 0) {
        datacenter_destroy(&dc);
        return 1;
    }
	/*
	 * ADICIONADO:
	 * Processamos cada ficheiro .conf, pela ordem devolvida
	 * por list_conf_files().
	 */
    for (size_t i = 0; i < conf_count; i++) {
        char conf_path[MAX_PATH_SIZE];
		/*
		 * ADICIONADO:
		 * Junta a diretoria ao nome do ficheiro.
		 *
		 * Exemplo:
		 * input_dir     = "configs"
		 * conf_files[i] = "1.conf"
		 *
		 * conf_path     = "configs/1.conf"
		 */
        snprintf(conf_path, sizeof(conf_path),
                               "%s/%s", input_dir, conf_files[i]);
		/*
		 * ADICIONADO:
		 * Abre o .conf apenas para leitura.
		 * fd é o descritor que será passado ao parser.
		 */
        int fd = open(conf_path, O_RDONLY);
        if (fd == -1) {
            free_file_list(conf_files, conf_count);
            datacenter_destroy(&dc);
            return 1;
        }
		/*
		 * ALTERADO:
		 * Os comandos passam a ser processados a partir
		 * deste ficheiro, em vez do terminal.
		 */
        process_commands(&dc, fd);
		/*
		 * ADICIONADO:
		 * Já acabámos de processar este .conf.
		 */
        close(fd);
	}
	/*
	 * ADICIONADO:
	 * liberta a memória utilizada para guardar
	 * os nomes dos ficheiros .conf.
	 */
	free_file_list(conf_files, conf_count);
    datacenter_destroy(&dc);
    return 0;
}