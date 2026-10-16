#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h> //E1(adicionado)
#include <fcntl.h> // E1(ADICIONADO): necessário para open() e O_RDONLY
#include <pthread.h> ///E2
#include "datacenter_utils.h" // E2
#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h" // E1(ADICIONADO): funções para obter os ficheiros .conf

/*
 * Exercicio 1 (ALTERADO): Antes os comandos eram lidos de STDIN_FILENO.
 * Agora recebemos 'fd', que é o descritor do ficheiro .conf atual.
 */
static int process_commands(DataCenter *dc, int fd) {
	while(1){
		switch (get_next_command(fd)){ // (E1)ALTERADO: STDIN_FILENO -> fd
			case CMD_DEFINE: {
				VMType vmtype;
				// Exercício 1 (ALTERADO): lê os argumentos do ficheiro .conf atual
				if (parse_define(fd, &vmtype) != 0) {
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					continue;
				}
				// Exercício 2
				/* Bloqueia o mutex para garantir que só esta thread
   				está a aceder/modificar o DataCenter neste momento. */
				pthread_mutex_lock(&dc->mutex);
				/* Guarda o resultado da tentativa de definir o novo tipo de VM. */
				int result = 
				/* Adiciona o tipo de VM ao DataCenter.
       			Como o mutex está bloqueado, a thread de espera
       			não pode alterar o DataCenter ao mesmo tempo. */
				datacenter_define_VM(dc, &vmtype);
				/* Liberta o mutex porque já terminámos de aceder
   				ao DataCenter nesta operação. */
				pthread_mutex_unlock(&dc->mutex);
				/* Verifica se ocorreu algum erro ao definir a VM. */
				if (result != 0) {
					fprintf(stderr, "Failed to define VM.\n");
					/* Passa imediatamente para o próximo comando
       				do ficheiro .conf. */
					continue;
				}
				printf("VM successfully defined!\n");
				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};
				// Exercicio 1 (ALTERADO): lê a reserva do ficheiro .conf atual
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

				if (num_items == 0) {
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					continue;
				}
				// Exercicio 2
				/* Bloqueia o mutex para garantir que só esta thread
   				está a aceder ou modificar o DataCenter neste momento. */
				pthread_mutex_lock(&dc->mutex);
				/* Tenta efetuar a reserva das VMs e guarda
   				o resultado da operação. */
				int result = datacenter_reserve(dc,&reservation);
				/* Liberta o mutex porque a operação sobre
   				o DataCenter já terminou. */
				pthread_mutex_unlock(&dc->mutex);
				/* Verifica se ocorreu algum erro durante a reserva. */
				if (result != 0) {
					/* Mostra uma mensagem de erro. */
					fprintf(stderr, "Failed to reserve VMs.\n");
					/* Passa para o próximo comando do ficheiro .conf. */
					continue;
				}
				/* Se não ocorreu nenhum erro,
   				informa que a reserva foi efetuada com sucesso. */
				printf("Reservation made successfully!\n");
				/* Termina o tratamento do caso CMD_RESERVE
   				dentro do switch. */
				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];
				// ALTERADO: lê o ID do ficheiro .conf atual
				if(parse_execute(fd, id) != 0){
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					continue;
				}
				// Exercicio 2
				/* Bloqueia o mutex para garantir que só esta thread
   				está a aceder ou modificar o DataCenter neste momento. */
				pthread_mutex_lock(&dc->mutex);
				/* Tenta iniciar a execução da reserva indicada
   				e guarda o resultado da operação. */
				int result = datacenter_execute(dc, id);
				/* Liberta o mutex porque a operação sobre
   				o DataCenter já terminou. */
				pthread_mutex_unlock(&dc->mutex);
				/* Verifica se ocorreu algum erro ao iniciar
   				a execução da reserva. */
				if (result != 0) {
					fprintf(stderr, "Failed to execute reservation.\n");
					/* Passa para o próximo comando do ficheiro .conf. */
					continue;
				}
				/* Informa que a execução da reserva foi iniciada.
   				Não diz "finished", porque as VMs continuam
   				a executar nos processos filhos. */
				printf("Reservation execution started!\n");
				/* Termina o tratamento do caso CMD_EXECUTE
   				dentro do switch. */
				break;
			}

			case CMD_LIST: {
				// Exercicio 2
				/* Bloqueia o mutex para garantir que a thread de espera
  				 não altera o DataCenter enquanto o estamos a listar. */
				pthread_mutex_lock(&dc->mutex);
				/* Mostra o estado atual do DataCenter
  				 e guarda o resultado da operação. */
				int result = datacenter_list(dc);
				/* Liberta o mutex porque já terminámos
   				de consultar o DataCenter. */
				pthread_mutex_unlock(&dc->mutex);
				/* Verifica se ocorreu algum erro ao listar
   				o estado do DataCenter. */
				if (result != 0) {
					fprintf(stderr, "Failed to list VMs.\n");
					/* Passa para o próximo comando do ficheiro .conf. */
					continue;
				}
				/* Termina o tratamento do caso CMD_LIST
   				dentro do switch. */
				break;
			}

			case CMD_WAIT: {
				unsigned int delay;
				// E1(ALTERADO): lê o tempo do ficheiro .conf atual
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
				 * E1(ALTERADO):
				 * EOC significa apenas que chegámos ao fim deste .conf.
				 * NÃO destruímos o DataCenter, porque ainda pode haver
				 * outros ficheiros .conf para processar.
				 */
				return 0;
		}
	}
}

int main(int argc, char **argv){
	// E1(ALTERADO): Agora existe mais um argumento: INPUT_DIR.
	if (argc != 7) { // nova alteracao devido à nova formula2
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <local_cores> <input_dir>\n", argv[0]); // alterado devido à nova fórmula2
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;
	size_t local_cores; // alterado devido à nova fórmula2

	DataCenter dc;
	datacenter_init(&dc);	

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_size_t_arg(argv[5], &local_cores) != 0) { // alterado devido à nova fórmula2
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	// E1(ADICIONADO): argv[6] contém a diretoria onde estão os ficheiros .conf.
	char *input_dir = argv[6]; // alterado devido à nova fórmula2

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		return 1;
	}
	dc.local_cores = local_cores; // alterado devido à nova fórmula2
	// E1(ADICIONADO): conf_files vai guardar as entradas correspondentes aos ficheiros .conf devolvidas por scandir().
	struct dirent **conf_files = NULL;
	int conf_count = 0;

	// E1(ADICIONADO): Obtém todos os .conf existentes em INPUT_DIR. A função também os coloca por ordem alfabética.
    if (list_conf_files(input_dir, &conf_files, &conf_count) != 0) {
        datacenter_destroy(&dc);
        return 1;
    }
	// E2(Adicionado): Variável onde fica guardado o identificador da nova thread.
	pthread_t wait_thread;
	/* Cria a thread responsável por esperar pelos processos das VMs. */
	if (pthread_create(&wait_thread,NULL,vm_wait_thread,&dc) != 0) {
		/* &wait_thread: Guarda aqui o identificador da thread criada. */	
		/* NULL: Usa os atributos padrão da thread. */
		/* vm_wait_thread: Função que a nova thread vai começar a executar. */
		/* &dc: Argumento passado para vm_wait_thread(): o endereço do DataCenter. */
		/* Se pthread_create() falhar, mostra uma mensagem de erro. */
		fprintf(stderr, "Failed to create wait thread.\n");
		// Liberta cada entrada criada pelo scandir() e depois o vetor de ponteiros.
		free_file_list(conf_files, conf_count);
		/* Liberta os recursos associados ao DataCenter. */
		datacenter_destroy(&dc);
		return 1; /* Termina o programa com erro. */
	}
	// E1(ADICIONADO): Processamos cada ficheiro .conf, pela ordem devolvida por list_conf_files().
    for (int i = 0; i < conf_count; i++) {
        char conf_path[MAX_PATH_SIZE];
		/*
		 * E1(ADICIONADO):
		 * Junta a diretoria ao nome do ficheiro.
		 * Exemplo:
		 * input_dir     = "configs"
		 * conf_files[i]->d_name = "1.conf" "
		 * conf_path     = "configs/1.conf"
		 */
        snprintf(conf_path, sizeof(conf_path), "%s/%s", input_dir, conf_files[i]->d_name);
		// E1(ADICIONADO): Abre o .conf apenas para leitura. fd é o descritor que será passado ao parser.
        int fd = open(conf_path, O_RDONLY);
        if (fd == -1) {
            free_file_list(conf_files, conf_count);
            datacenter_destroy(&dc);
            return 1;
        }
		// E1(ALTERADO): Os comandos passam a ser processados a partir deste ficheiro, em vez do terminal.
        process_commands(&dc, fd);
		// E1(ADICIONADO): Já acabámos de processar este .conf.
        close(fd);
	}
	// E1(ADICIONADO): Liberta cada entrada criada pelo scandir() e depois o vetor de ponteiros.
	free_file_list(conf_files, conf_count);
	// E2: Bloqueia o mutex antes de alterar shutting_down, porque esta variável também é lida pela thread de espera.
	pthread_mutex_lock(&dc.mutex);
	// Indica que todos os ficheiros .conf já foram processados e que já não vão ser criadas novas VMs.
	dc.shutting_down = 1;
	/*
	* Acorda a thread de espera caso ela esteja adormecida no pthread_cond_wait().
	* Isto permite que ela volte a verificar se ainda existem VMs em execução ou se já pode terminar.
	*/
	pthread_cond_signal(&dc.child_cond);
	// Liberta o mutex depois de terminar a alteração do estado partilhado do DataCenter.
	pthread_mutex_unlock(&dc.mutex);
	/*
	* Espera que a thread de espera termine.
	* Se ainda existirem VMs em execução, essa thread continua
	* a fazer wait() pelos processos filhos.
	* Só quando active_children chegar a 0 é que a thread termina
	* e o pthread_join() deixa a thread principal continuar.
	*/
	pthread_join(wait_thread, NULL);
    datacenter_destroy(&dc);
    return 0;
}