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
					/* E3 sincronização: evita output entrelaçado. */
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				/*
				* E3 sincronização: D altera o estado do DataCenter.
				* O read lock permite outras operações independentes, mas impede L de listar durante esta alteração.
				*/
				pthread_rwlock_rdlock(&dc->state_lock);
				// Exercício 2
				// Bloqueia o mutex para garantir que só esta thread está a aceder/modificar o DataCenter neste momento.
				pthread_mutex_lock(&dc->mutex);
				// Guarda o resultado da tentativa de definir o novo tipo de VM.
				int result = 
				/* Adiciona o tipo de VM ao DataCenter. Como o mutex está bloqueado, a thread de espera
       			não pode alterar o DataCenter ao mesmo tempo. */
				datacenter_define_VM(dc, &vmtype);
				// Liberta o mutex porque já terminámos de aceder ao DataCenter nesta operação.
				pthread_mutex_unlock(&dc->mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				// Verifica se ocorreu algum erro ao definir a VM.
				if (result != 0) {
					// E3 sincronização: protege apenas o output.
    				pthread_mutex_lock(&dc->output_mutex);
					fprintf(stderr, "Failed to define VM.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					// Passa imediatamente para o próximo comando do ficheiro .conf
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex);
				printf("VM successfully defined!\n");
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};
				// Exercicio 1 (ALTERADO): lê a reserva do ficheiro .conf atual
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

				if (num_items == 0) {
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				/*
				* E3 sincronização:
				* várias reservas podem decorrer simultaneamente.
				* datacenter_reserve() faz internamente a sincronização dos servidores utilizados pela reserva.
				*/
				pthread_rwlock_rdlock(&dc->state_lock);
				int result = datacenter_reserve(dc,&reservation);
				pthread_rwlock_unlock(&dc->state_lock);
				// Verifica se ocorreu algum erro durante a reserva
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Failed to reserve VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex); // E3
					// Passa para o próximo comando do ficheiro .conf.
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex); // E3
				// Se não ocorreu nenhum erro, informa que a reserva foi efetuada com sucesso.
				printf("Reservation made successfully!\n");
				// Termina o tratamento do caso CMD_RESERVE dentro do switch.
				pthread_mutex_unlock(&dc->output_mutex);
				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];
				// ALTERADO: lê o ID do ficheiro .conf atual
				if(parse_execute(fd, id) != 0){
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				// E3 sincronização: A altera o estado que pode ser observado por L.
				pthread_rwlock_rdlock(&dc->state_lock);
				// Exercicio 2
				// Bloqueia o mutex para garantir que só esta thread está a aceder ou modificar o DataCenter neste momento.
				pthread_mutex_lock(&dc->mutex);
				// Tenta iniciar a execução da reserva indicada e guarda o resultado da operação.
				int result = datacenter_execute(dc, id);
				// Liberta o mutex porque a operação sobre o DataCenter já terminou.
				pthread_mutex_unlock(&dc->mutex);
				pthread_rwlock_unlock(&dc->state_lock);
				// Verifica se ocorreu algum erro ao iniciar a execução da reserva.
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Failed to execute reservation.\n");
					// Passa para o próximo comando do ficheiro .conf
					pthread_mutex_unlock(&dc->output_mutex);
					continue;
				}
				pthread_mutex_lock(&dc->output_mutex); // E3
				/* Informa que a execução da reserva foi iniciada.
   				Não diz "finished", porque as VMs continuam a executar nos processos filhos. */
				printf("Finished reservation execution!\n");
				pthread_mutex_unlock(&dc->output_mutex); // E3
				// Termina o tratamento do caso CMD_EXECUTE dentro do switch.
				break;
			}

			case CMD_LIST: {
				/*
				* E3 sincronização:
				* L precisa de observar uma fotografia consistente
				* de todo o DataCenter.
				* O write lock espera que as alterações em curso acabem
				* e impede novas alterações enquanto decorre a listagem.
				*/
				pthread_rwlock_wrlock(&dc->state_lock);
				// E3 sincronização: impede outros outputs de aparecerem no meio da listagem.
				pthread_mutex_lock(&dc->output_mutex);
				// Mostra o estado atual do DataCenter e guarda o resultado da operação.
				int result = datacenter_list(dc);
				pthread_mutex_unlock(&dc->output_mutex); // E3
				// A listagem terminou. As outras threads podem voltar a alterar o estado.
				pthread_rwlock_unlock(&dc->state_lock);
				// Verifica se ocorreu algum erro ao listar o estado do DataCenter.
				if (result != 0) {
					pthread_mutex_lock(&dc->output_mutex); // E3
					fprintf(stderr, "Failed to list VMs.\n");
					pthread_mutex_unlock(&dc->output_mutex);
					// Passa para o próximo comando do ficheiro .conf
					continue;
				}
				// Termina o tratamento do caso CMD_LIST dentro do switch.
				break;
			}

			case CMD_WAIT: {
				unsigned int delay;
				// E1(ALTERADO): lê o tempo do ficheiro .conf atual
				if(parse_wait(fd, &delay) != 0){
					pthread_mutex_lock(&dc->output_mutex); // E3
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
				 * E1(ALTERADO):
				 * EOC significa apenas que chegámos ao fim deste .conf.
				 * NÃO destruímos o DataCenter, porque ainda pode haver
				 * outros ficheiros .conf para processar.
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

/*
 * Exercício 3:
 * Cada worker vai buscar um ficheiro .conf ainda não processado e executa os seus comandos.
 */
static void *worker_thread(void *arg) {
    WorkerArgs *args = (WorkerArgs *)arg;
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
        // Constrói o caminho completo para o ficheiro .conf.
        char conf_path[MAX_PATH_SIZE];
        int written = snprintf(
            conf_path,
            sizeof(conf_path),
            "%s/%s",
            args->input_dir,
            args->conf_files[i]->d_name
        );
        if (written < 0 ||
            (size_t)written >= sizeof(conf_path)) {
			pthread_mutex_lock(&args->dc->output_mutex); // E3
            fprintf(stderr, "Configuration file path is too long.\n");
            pthread_mutex_unlock(&args->dc->output_mutex);
			pthread_mutex_lock(&args->work_mutex);
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
	// E1(ALTERADO): Agora existe mais um argumento: INPUT_DIR.
	if (argc != 8) { // nova alteracao devido à nova formula2 + E3
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <local_cores> <input_dir> <max_threads>\n", argv[0]); // alterado devido à nova fórmula2
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;
	size_t local_cores; // alterado devido à nova fórmula2
	size_t max_threads;   // E3

	DataCenter dc;
	datacenter_init(&dc);	

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_size_t_arg(argv[5], &local_cores) != 0 || // alterado devido à nova fórmula2
		    parse_size_t_arg(argv[7], &max_threads) != 0 || // E3
  			max_threads == 0) { // E3
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	// E1(ADICIONADO): argv[6] contém a diretoria onde estão os ficheiros .conf.
	char *input_dir = argv[6]; // alterado devido à nova fórmula2
	// E1: verifica se INPUT_DIR existe e corresponde a uma diretoria.
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
	// Foi apagado o for a que se referia o comentario anterior
	// Exercício 3: Estado partilhado pelas worker threads.
	WorkerArgs worker_args;
	worker_args.dc = &dc;
	worker_args.conf_files = conf_files;
	worker_args.conf_count = conf_count;
	worker_args.input_dir = input_dir;
	worker_args.next_file = 0;
	worker_args.error = 0;
	pthread_mutex_init(&worker_args.work_mutex, NULL);
	// Calcula quantas worker threads serão realmente necessárias.
	size_t num_workers = max_threads;
	if ((size_t)conf_count < num_workers)
		num_workers = (size_t)conf_count;
	// AQUI entra o vetor de worker threads.
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
	// Depois disto crias efetivamente as workers.
	size_t workers_created = 0;
	for (size_t i = 0; i < num_workers; i++) {
		if (pthread_create(&workers[i], NULL, worker_thread, &worker_args) != 0) {
			// E3: já podem existir outras workers a imprimir.
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
	// Já não serão criadas novas VMs.
	pthread_mutex_lock(&dc.mutex);
	dc.shutting_down = 1;
	pthread_cond_signal(&dc.child_cond);
	pthread_mutex_unlock(&dc.mutex);
	// Espera que a thread do Exercício 2 termine.
	pthread_join(wait_thread, NULL);
	int worker_error = worker_args.error;
	datacenter_destroy(&dc);
	if (worker_error)
		return 1;
	return 0;
}