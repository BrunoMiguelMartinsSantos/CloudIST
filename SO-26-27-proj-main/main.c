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

// Exercicio 1(alterações): Antes os comandos eram lidos de STDIN_FILENO. Agora recebemos 'fd', que é o descritor do ficheiro .conf atual.
static int process_commands(DataCenter *dc, int fd) {
	while(1){
		switch (get_next_command(fd)){ // Exercício 1: STDIN_FILENO -> fd
			case CMD_DEFINE: {
				VMType vmtype;
				// Exercício 1: lê os argumentos do ficheiro .conf atual
				if (parse_define(fd, &vmtype) != 0) {
					// Exercício 3: evita output entrelaçado.
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				/*
				* Exercício 3: D altera o estado do DataCenter.
				* O read lock permite outras operações independentes, mas impede L de listar durante esta alteração.
				*/
				pthread_rwlock_rdlock(&dc->state_lock);
				// Exercício 2: Bloqueia o mutex para garantir que só esta thread está a aceder ou modificar o DataCenter neste momento.
				pthread_mutex_lock(&dc->mutex);
				// Guarda o resultado da tentativa de definir o novo tipo de VM.
				int result = 
				// Adiciona o tipo de VM ao DataCenter. Como o mutex está bloqueado, a thread de espera não pode alterar o DataCenter ao mesmo tempo.
				datacenter_define_VM(dc, &vmtype);
				pthread_mutex_unlock(&dc->mutex); // Liberta o mutex porque já terminámos de aceder ao DataCenter nesta operação.
				pthread_rwlock_unlock(&dc->state_lock);
				// Verifica se ocorreu algum erro ao definir a VM.
				if (result != 0) {
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to define VM.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue; // Passa imediatamente para o próximo comando do ficheiro .conf
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("VM successfully defined!\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};
				// Exercicio 1(alterações): lê a reserva do ficheiro .conf atual
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);
				if (num_items == 0) {
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				/*
				* Exercício 3: Várias reservas podem decorrer simultaneamente.
				* datacenter_reserve() faz internamente a sincronização dos servidores utilizados pela reserva.
				*/
				pthread_rwlock_rdlock(&dc->state_lock); // permite outras alterações em paralelo, mas impede o comando L de listar enquanto esta operação decorre.
				int result = datacenter_reserve(dc,&reservation);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) { // Verifica se ocorreu algum erro durante a reserva
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to reserve VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue; // Passa para o próximo comando do ficheiro .conf.
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("Reservation made successfully!\n"); // Se não ocorreu nenhum erro, informa que a reserva foi efetuada com sucesso.
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];
				// Alterado: lê o ID do ficheiro .conf atual
				if(parse_execute(fd, id) != 0){
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				// Exercício 3: A altera o estado que pode ser observado por L.
				pthread_rwlock_rdlock(&dc->state_lock);
				// Exercicio 2: Bloqueia o mutex para garantir que só esta thread está a aceder ou modificar o DataCenter neste momento.
				pthread_mutex_lock(&dc->mutex);
				// Tenta iniciar a execução da reserva indicada e guarda o resultado da operação.
				int result = datacenter_execute(dc, id);
				// Liberta o mutex porque a operação sobre o DataCenter já terminou.
				pthread_mutex_unlock(&dc->mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) { // Verifica se ocorreu algum erro ao iniciar a execução da reserva.
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Failed to execute reservation.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex); // E3
				printf("Finished reservation execution!\n");
				pthread_mutex_unlock(&dc->output_mutex); // E3
				break;
			}

			case CMD_LIST: {
				/* Exercício 3: L precisa de observar uma fotografia consistente de todo o DataCenter.
				* O write lock espera que as alterações em curso acabem e impede novas alterações enquanto decorre a listagem.
				*/
				pthread_rwlock_wrlock(&dc->state_lock);
				// Exercício 3: impede outros outputs de aparecerem no meio da listagem.
				pthread_mutex_lock(&dc->output_mutex);
				// Mostra o estado atual do DataCenter e guarda o resultado da operação.
				int result = datacenter_list(dc);
				pthread_mutex_unlock(&dc->output_mutex);
				// Listagem terminou. As outras threads podem voltar a alterar o estado.
				pthread_rwlock_unlock(&dc->state_lock);
				if (result != 0) { // Verifica se ocorreu algum erro ao listar o estado do DataCenter.
					pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to list VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				break;
			}

			case CMD_WAIT: {
				unsigned int delay;
				// Exercício 1(alterações): lê o tempo do ficheiro .conf atual
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
				/*
				 * Exercício 1(alteração): EOC significa apenas que chegámos ao fim deste .conf.
				 * NÃO destruímos o DataCenter, porque ainda pode haver outros ficheiros .conf para processar.
				 */
				return 0;
		}
	}
}

// Exercício 3: Informação partilhada pelas worker threads.
typedef struct {
    DataCenter *dc; // DataCenter comum a todas as threads
    struct dirent **conf_files; // lista dos ficheiros .conf
    int conf_count; // número de ficheiros
    const char *input_dir; // diretoria onde estão os .conf
    int next_file; // próximo índice ainda não atribuído a um worker
    pthread_mutex_t work_mutex; // protege next_file para dois workers não pegarem no mesmo ficheiro
    int error; //indica se algum worker encontrou erro
} WorkerArgs;

// Exercício 3: Cada worker vai buscar um ficheiro .conf ainda não processado e executa os seus comandos.
static void *worker_thread(void *arg) {
    WorkerArgs *args = (WorkerArgs *)arg; // Converte o argumento genérico recebido pela thread para um ponteiro para WorkerArgs.
    while (1) {
        // Protege next_file para garantir que dois workers não escolhem o mesmo ficheiro.
        pthread_mutex_lock(&args->work_mutex);
        // Se todos os ficheiros já foram atribuídos, este worker pode terminar.
        if (args->next_file >= args->conf_count) {
            pthread_mutex_unlock(&args->work_mutex);
            return NULL;
        }
        // Guarda o índice do ficheiro que este worker vai processar e avança para o próximo.
        int i = args->next_file;
        args->next_file++;
        pthread_mutex_unlock(&args->work_mutex);
        char conf_path[MAX_PATH_SIZE];
		 // Constrói o caminho completo para o ficheiro .conf.
        int written = snprintf(conf_path, sizeof(conf_path), "%s/%s", args->input_dir, args->conf_files[i]->d_name);
        if (written < 0 || (size_t)written >= sizeof(conf_path)) {
			pthread_mutex_lock(&args->dc->output_mutex);
            fprintf(stderr, "Configuration file path is too long.\n");
            pthread_mutex_unlock(&args->dc->output_mutex);
			pthread_mutex_lock(&args->work_mutex); // Protege a variável error, partilhada entre as worker threads.
            args->error = 1;
            pthread_mutex_unlock(&args->work_mutex);
            return NULL;
        }
        // Abre o ficheiro apenas para leitura.
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
        // Processa sequencialmente os comandos deste ficheiro.
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
  			max_threads == 0) { // nao faz sentido executar com max_threads=0 pois isso significaria não criar nenhuma worker thread para processar os .conf
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}
	// Exercício1(adicionado): argv[6] contém a diretoria onde estão os ficheiros .conf.
	char *input_dir = argv[6];
	// Exercício 1: verifica se input_dir existe e corresponde a uma diretoria.
	if (!path_exists(input_dir)) {
		fprintf(stderr, "Invalid input directory.\n");
		// O DataCenter já foi inicializado, portanto libertamos os seus recursos antes de sair.
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
	// Exercício 1(adicionado): conf_files vai guardar as entradas correspondentes aos ficheiros .conf devolvidas por scandir().
	struct dirent **conf_files = NULL;
	int conf_count = 0;

	// Exercício1(adicionado): Obtém todos os .conf existentes em INPUT_DIR. A função também os coloca por ordem alfabética.
    if (list_conf_files(input_dir, &conf_files, &conf_count) != 0) {
        datacenter_destroy(&dc);
        return 1;
    }
	pthread_t wait_thread; // Variável onde fica guardado o identificador da nova thread.
	// Cria a thread responsável por esperar pelos processos das VMs.
	if (pthread_create(&wait_thread,NULL,vm_wait_thread,&dc) != 0) {
		fprintf(stderr, "Failed to create wait thread.\n");
		// Liberta cada entrada criada pelo scandir() e depois o vetor de ponteiros.
		free_file_list(conf_files, conf_count);
		datacenter_destroy(&dc); // Liberta os recursos associados ao DataCenter
		return 1; // Termina o programa com erro.
	}
	WorkerArgs worker_args; // Cria a estrutura que vai guardar os dados partilhados pelas worker threads.
	worker_args.dc = &dc; // Guarda o endereço do DataCenter comum a todas as workers.
	worker_args.conf_files = conf_files;
	worker_args.conf_count = conf_count;
	worker_args.input_dir = input_dir;
	worker_args.next_file = 0;
	worker_args.error = 0;
	pthread_mutex_init(&worker_args.work_mutex, NULL);
	// Calcula quantas worker threads serão realmente necessárias.
	size_t num_workers = max_threads;
	// se houver menos ficheiros .conf do que threads permitidas,reduzimos o número de workers.
	if ((size_t)conf_count < num_workers) num_workers = (size_t)conf_count;
	pthread_t *workers = NULL; // ponteiro para vários pthread_t, ou seja, para guardar os identificadores de várias worker threads.
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
			// Exercício 3: já podem existir outras workers a imprimir.
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
	// Espera que todas as worker threads terminem.
	for (size_t i = 0; i < workers_created; i++) {
		pthread_join(workers[i], NULL);
	}
	pthread_mutex_destroy(&worker_args.work_mutex);
	free(workers);
	free_file_list(conf_files, conf_count);
	pthread_mutex_lock(&dc.mutex); // Já não serão criadas novas VMs.
	dc.shutting_down = 1;
	pthread_cond_signal(&dc.child_cond);
	pthread_mutex_unlock(&dc.mutex);
	pthread_join(wait_thread, NULL); // Espera que a wait_thread
	// Guarda se ocorreu algum erro nas worker threads para verificar após a limpeza final.
	int worker_error = worker_args.error;
	datacenter_destroy(&dc);
	if (worker_error) return 1;
	return 0;
}