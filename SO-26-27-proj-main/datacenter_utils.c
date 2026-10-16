#include "datacenter_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pthread.h> // Exercicio 2
#include <errno.h> // Exercicio 2

VMType *VMType_exists(DataCenter *dc, const char* type_id){
	for(size_t i = 0; i < dc->num_vm_types; i++){
		if(strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0){
			return &dc->vm_types[i];
		}
	}
	return NULL;
}

/**
 * Checks if a reservation with the given id already exists in the Data Center.
 * 
 * @param dc Pointer to Data Center.
 * @param reservation_id Id of the reservation being checked.
 * 
 * @return 1 if the reservation exists.
 * @return 0 if the reservation does not exist.
 */	
static int reservation_exists(DataCenter *dc, const char* reservation_id){
	for(size_t i = 0; i < dc->num_reservations; i++){
		if(strncmp(reservation_id, dc->reservations[i].id, MAX_STRING_SIZE) == 0){
			return 1;
		}
	}
	return 0;
}

int reservation_validate(DataCenter *dc, Reservation *reservation) {
	if(reservation_exists(dc, reservation->id)){
		fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
		return 1;
	}

	Resources available[dc->num_servers];
	size_t hosted_count[dc->num_servers];
	size_t vms_count = 0;

	/* Copy current server resources and hosted_vms so we can simulate the allocation. */
	for (size_t i = 0; i < dc->num_servers; i++) {
		available[i] = dc->servers[i].available;
		hosted_count[i] = dc->servers[i].num_hosted_vms;
	}

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];

		if (item->amount == 0) {
			fprintf(stderr, "Reservation item must request at least one VM.\n");
			return 1;
		}

		VMType *vm_type = VMType_exists(dc, item->vm_type_id);

		if(vm_type == NULL){
			fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
			return 1;
		}

		if (item->server_id == 0 || item->server_id > dc->num_servers) {
			fprintf(stderr, "Invalid server in reservation.\n");
			return 1;
		}

		for (size_t j = 0; j < item->amount; j++) {
			if(vms_count >= MAX_RESERVATION_VMS){
				fprintf(stderr, "Reached max of VMS for a reservation.\n");
				return 1;
			}
			
			if (hosted_count[item->server_id - 1] >= MAX_HOSTED_VMS) {
        fprintf(stderr,
                "Server \"%zu\" reached the maximum number of hosted VMs.\n",
                item->server_id);
        return 1;
    	}

			if (!resources_can_fit(available[item->server_id-1], vm_type->required)) {
				fprintf(stderr, "Server \"%zu\" does not have enough resources.\n", item->server_id);
				return 1;
			}

			resources_sub(&available[item->server_id-1], vm_type->required);
			hosted_count[item->server_id-1]++;
			vms_count++; // One VM "reserved"
		}

		// Attach vm_type to item for later use.
		item->vm_type = vm_type;
	}

	return 0;
}

/**
 * Rolls back the changes made by a reservation operation.
 * 
 * @param reservation Pointer to the reservation being rolled back.
 * @param initial_vms Number of VMs that existed before the operation.
 */
static void reservation_rollback(Reservation *reservation, size_t initial_vms){
	while (reservation->num_vms > initial_vms) {
		VM *vm = reservation->vms[reservation->num_vms - 1];

		Server *server = vm->server;

		// Restore resources
		resources_add(&server->available, vm->type->required);

		// Remove VM from server hosted list
		server->num_hosted_vms--;

		server->hosted_vms[server->num_hosted_vms] = NULL;

		// Remove VM from reservation
		reservation->num_vms--;

		reservation->vms[reservation->num_vms] = NULL;

		free(vm);
	}
}

int reservation_commit(DataCenter *dc, Reservation *reservation){
	// Keep track of how many vms existed before.
	size_t initial_vms = reservation->num_vms;

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];
		Server *server = &dc->servers[item->server_id - 1];

		for (size_t j = 0; j < item->amount; j++) {
			VM *new_vm = calloc(1, sizeof(VM));
			if (new_vm == NULL) {
				fprintf(stderr, "Failed to allocate VM.\n");
				reservation_rollback(reservation, initial_vms);
				return 1;
			}

			snprintf(new_vm->id,
				MAX_VM_ID_STRING,
				"%s_%zu",
				reservation->id,
				reservation->num_vms + 1
			);

			new_vm->type = item->vm_type;
			new_vm->server = server;
			new_vm->state = VM_STATE_RESERVED;

			resources_sub(&server->available, item->vm_type->required);

			server->hosted_vms[server->num_hosted_vms++] = new_vm;
			reservation->vms[reservation->num_vms++] = new_vm;
		}
	}

	reservation->state = RES_STATE_PENDING;

	return 0;
}

Reservation *find_pending_reservation(DataCenter *dc, const char *reservation_id) {
	Reservation *res = NULL;

	for (size_t i = 0; i < dc->num_reservations; i++) {
		if (strcmp(dc->reservations[i].id, reservation_id) == 0) {
			res = &dc->reservations[i];
			break;
		}
	}

	if (!res) {
		fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
		return NULL;
	}

	if (res->state != RES_STATE_PENDING) {
		fprintf(stderr, "Reservation ID %s is not in a pending state.\n", reservation_id);
		return NULL;
	}

	return res;
}

void reservation_destroy(DataCenter *dc, Reservation *reservation) {
	// Free every VM belonging to the reservation.
	for (size_t i = 0; i < reservation->num_vms; i++) {
		VM *vm = reservation->vms[i];
		Server *server = vm->server;

		// Return the VM resources back to the hosting server.
		resources_add(&server->available, vm->type->required);

		// Remove the VM from the server's hosted VM list.
		for (size_t j = 0; j < server->num_hosted_vms; j++) {
			if (server->hosted_vms[j] == vm) {
				// Shift the remaining VMs one position to the left.
				memmove(&server->hosted_vms[j],
								&server->hosted_vms[j + 1],
								(server->num_hosted_vms - j - 1) * sizeof(VM *));

				server->num_hosted_vms--;
				break;
			}
		}

		free(vm);
		reservation->vms[i] = NULL;
	}

	reservation->num_vms = 0;

	// Find the reservation inside the Data Center reservation list.
	size_t idx;
	for (idx = 0; idx < dc->num_reservations; idx++) {
			if (&dc->reservations[idx] == reservation)
				break;
	}

	// Reservation not found (should never happen).
	if (idx == dc->num_reservations)
		return;

	// Remove the reservation by shifting the remaining ones.
	memmove(&dc->reservations[idx],
					&dc->reservations[idx + 1],
					(dc->num_reservations - idx - 1) * sizeof(Reservation));

	dc->num_reservations--;
}

// Exercicio 2
// Recebe a VM, o número de servidores do centro de dados
// e o número de cores locais atribuídos ao CloudIST.
void spawn_vm_child(VM *vm, size_t num_servers, size_t local_cores) { // nova alteracao devido à nova formula2
	// TODO: Limit RAM, DISK and use exec with cpulimit.
	// Cria uma estrutura que guarda os limites mínimo e máximo de recursos.
	struct rlimit lim;
	// Obtém a RAM necessária em GB, converte-a para rlim_t e multiplica por 2^30 para obter bytes.
	lim.rlim_cur = (rlim_t)vm->type->required.ram << 30;
	// Define o limite máximo igual ao limite atual.
	lim.rlim_max = lim.rlim_cur;
	// Tenta limitar a memória virtual do processo; se falhar, entra no if.
	if (setrlimit(RLIMIT_AS, &lim) != 0) {
		perror("setrlimit"); // Mostra uma mensagem de erro relativa à chamada setrlimit.
        _exit(1);
    }
    // Obtém o espaço de disco em GB e converte-o para bytes usando 2^30.
    lim.rlim_cur = (rlim_t)vm->type->required.disk << 30;
	// Define o limite máximo de tamanho de ficheiro igual ao limite atual.
    lim.rlim_max = lim.rlim_cur;
	// Tenta limitar o tamanho máximo de cada ficheiro criado pelo processo.
    if (setrlimit(RLIMIT_FSIZE, &lim) != 0) {
		// Apresenta o erro caso não seja possível estabelecer o limite.
        perror("setrlimit");
        _exit(1);
    }
	/*
	* Fórmula atualizada do enunciado:
	* CPULIMIT = 100 * vCPUs_da_VM * cores_locais_selecionados / (CPUs_por_maquina * numero_de_maquinas)
	*/
	double cpu_percentage = 100.0 * vm->type->required.cpu * (double)local_cores / (vm->server->total.cpu * (double)num_servers);
    // Converte a percentagem de CPU para um inteiro do tipo size_t
	size_t cpu_limit_value = (size_t)cpu_percentage;
	// Cria um array de caracteres para guardar a percentagem de CPU como texto.
    char cpu_limit[MAX_STRING_SIZE];
	// cpu_limit: array onde guardar a string; sizeof(cpu_limit): tamanho máximo; 
	// "%zu": formato para size_t; cpu_limit_value: valor a converter.
    snprintf(cpu_limit, sizeof(cpu_limit), "%zu", cpu_limit_value);
	// Substitui o processo atual pelo cpulimit (procurado no PATH)
	// Se execlp tiver sucesso, não retorna; se falhar, retorna -1.
	execlp("cpulimit", "cpulimit", "-q", "-f", "-l",
       cpu_limit, "--", vm->type->exec_path, (char *)NULL);
    perror("execlp"); // Só chega aqui se o exec falhar.
    _exit(1);
}

// Exercicio 2
// Cria os processos filhos para todas as VMs da reserva. dc: ponteiro para o datacenter; res: ponteiro para a reserva.
// Retorna 0 se todos os forks tiverem sucesso e 1 se algum falhar.
int spawn_all_vms(DataCenter *dc, Reservation *res) {
	// Percorre todas as VMs da reserva.
	for (size_t i = 0; i < res->num_vms; i++) {
		// Obtém o ponteiro para a VM na posição i.
		VM *vm = res->vms[i];
		// TODO: Implement fork code. Set VM PID and update VM state to running.
		// Cria um processo filho; retorna -1 se falhar, 0 no filho e o PID do filho no pai.
		pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
		// No processo filho, configura os limites de recursos e executa a VM através do cpulimit.
        if (pid == 0) spawn_vm_child(vm, dc->num_servers, dc->local_cores); // nova alteracao devido à nova formula2
        // Processo pai: guarda o PID do filho e marca a VM como em execução.
        vm->pid = pid;
        vm->state = VM_STATE_RUNNING;
        // Incrementa o número de processos filhos ativos no datacenter.
        // O mutex já está bloqueado pela thread principal, evitando acessos concorrentes.
        dc->active_children++;
        // Sinaliza a variável de condição para acordar a thread que espera por processos filhos.
        pthread_cond_signal(&dc->child_cond);
	}
	return 0; // Indica que todos os processos filhos foram criados com sucesso.
}

// Exercicio 2
// Procura uma VM pelo PID do processo terminado e remove-a,
// libertando a memória e devolvendo os recursos ao servidor.
static void destroy_finished_vm(DataCenter *dc, pid_t pid) {
	// Percorre todas as reservas existentes no datacenter.
    for (size_t i = 0; i < dc->num_reservations; i++) {
		// Obtém o endereço da reserva na posição i.
        Reservation *res = &dc->reservations[i];
		// Percorre todas as VMs pertencentes à reserva.
        for (size_t j = 0; j < res->num_vms; j++) {
			// Obtém o ponteiro para a VM na posição j.
            VM *vm = res->vms[j];
			// Se o PID da VM for diferente do procurado, continue passa diretamente para a próxima VM.
            if (vm->pid != pid)
                continue;
            // Se a reserva tiver apenas uma VM, destrói a reserva inteira e termina a função com return.
            if (res->num_vms == 1) {
                reservation_destroy(dc, res);
                return;
            }
			// Obtém o servidor físico onde a VM estava alojada.
            Server *server = vm->server;
            // Devolve ao servidor os recursos (RAM, disco e CPU)
			// que estavam reservados para esta VM.
            resources_add(&server->available, vm->type->required);
            // Percorre a lista de VMs alojadas no servidor.
            for (size_t k = 0;
                 k < server->num_hosted_vms;
                 k++) {
				// Procura a posição da VM que terminou.		                
				if (server->hosted_vms[k] == vm) {
					// Desloca os ponteiros das VMs seguintes uma
                    // posição para a esquerda, eliminando a VM
                    // da lista sem deixar espaços vazios.
                    // Destino: &server->hosted_vms[k]
                    // Origem: &server->hosted_vms[k + 1]
                    // Tamanho: nº de elementos seguintes * sizeof(VM *)
                    memmove(
                        &server->hosted_vms[k],
                        &server->hosted_vms[k + 1],
                        (server->num_hosted_vms - k - 1)
                            * sizeof(VM *)
                    );
					// Decrementa o número de VMs alojadas.
                    server->num_hosted_vms--;
					// Coloca NULL na última posição utilizada
                    // anteriormente, eliminando o ponteiro duplicado.
                    server->hosted_vms[
                        server->num_hosted_vms
                    ] = NULL;
					 // Termina o ciclo de procura da VM no servidor.
                    break;
                }
            }
			// Remove a VM da lista da reserva, deslocando
            // os ponteiros seguintes uma posição para a esquerda.
            // Destino: &res->vms[j]
            // Origem: &res->vms[j + 1]
            // Tamanho: nº de elementos seguintes * sizeof(VM *).
            memmove(&res->vms[j], &res->vms[j + 1], (res->num_vms - j - 1) * sizeof(VM *));
            res->num_vms--; // Decrementa o número de VMs da reserva.
			// Coloca NULL na última posição anteriormente utilizada.
            res->vms[res->num_vms] = NULL;
            free(vm); // Liberta a memória dinâmica ocupada pela estrutura VM.
            return; // Termina a função, pois a VM pretendida já foi removida.
        }
    }
}

// Exercicio 2
// Função executada por uma thread responsável por esperar pela terminação
// dos processos filhos das VMs e libertar os respetivos recursos.
// Recebe um ponteiro genérico (void *arg) e devolve um ponteiro genérico.
void *vm_wait_thread(void *arg) {
	// Converte o ponteiro genérico arg para um ponteiro DataCenter.
    DataCenter *dc = (DataCenter *)arg;
	// Ciclo infinito para continuar a monitorizar os processos filhos.
    while (1) {
		// Bloqueia o mutex para aceder com segurança às variáveis
        // partilhadas active_children e shutting_down.
        pthread_mutex_lock(&dc->mutex);
        // Enquanto não existirem filhos ativos e o programa não
        // estiver a encerrar, a thread fica à espera.
        while (dc->active_children == 0 && !dc->shutting_down) {
			// Adormece a thread, libertando temporariamente o mutex.
            // Quando for acordada, volta a adquirir o mutex
            // antes de continuar a execução.
            pthread_cond_wait(&dc->child_cond, &dc->mutex);
		}
        // Se o datacenter estiver a encerrar e não existirem
        // processos filhos ativos, a thread pode terminar.
        if (dc->shutting_down && dc->active_children == 0) {
			// Desbloqueia o mutex antes de terminar a thread.
            pthread_mutex_unlock(&dc->mutex);
			// Termina a thread, devolvendo NULL.
            return NULL;
        }
		// Liberta o mutex para não bloquear outras threads
        // enquanto espera pela terminação de um processo filho.
        pthread_mutex_unlock(&dc->mutex);
        // Espera de forma bloqueante pela terminação de um processo filho.
        // Retorna o PID do filho terminado ou -1 em caso de erro.
        // Apenas esta thread fica bloqueada, não a thread principal.
        pid_t pid = wait(NULL);
		// Verifica se a chamada wait() falhou.
        if (pid < 0) {
			// Se wait() foi interrompido por um sinal (EINTR),
            // volta ao início do ciclo para tentar novamente.
            if (errno == EINTR)
                continue;
			// Apresenta a mensagem de erro associada a wait().
            perror("wait");
			// Volta ao início do ciclo para continuar a monitorização.
            continue;
        }
		// Bloqueia o mutex porque vai alterar estruturas
        // partilhadas do datacenter.
        pthread_mutex_lock(&dc->mutex);
		// Procura a VM correspondente ao PID terminado,
        // remove-a e liberta os recursos que utilizava.
        destroy_finished_vm(dc, pid);
		// Decrementa o contador de processos filhos ativos,
        // porque um deles terminou.
        dc->active_children--;
		// Desbloqueia o mutex, permitindo que outras threads
        // voltem a aceder aos dados partilhados.
        pthread_mutex_unlock(&dc->mutex);
    }
}

const char *vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        case VM_STATE_TERMINATED: return "TERMINATED";
        default:                  return "UNKNOWN";
    }
}

const char *res_state_to_string(ReservationState state) {
    switch (state) {
			case RES_STATE_PENDING:  return "PENDING";
			case RES_STATE_RUNNING:  return "RUNNING";
			case RES_STATE_FINISHED: return "FINISHED";
			default:                 return "UNKNOWN";
    }
}