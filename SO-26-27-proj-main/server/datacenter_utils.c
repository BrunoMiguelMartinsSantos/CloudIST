#include "datacenter_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>

VMType *VMType_exists(DataCenter *dc, const char* type_id){
	for(size_t i = 0; i < dc->num_vm_types; i++){
		if(strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0){
			return &dc->vm_types[i];
		}
	}
	return NULL;
}


int reservation_validate(DataCenter *dc, Reservation *reservation) {
    Resources available[dc->num_servers];
    size_t hosted_count[dc->num_servers];
	// Indica quais os servidores cujos dados temporários já foram inicializados.
    int initialized[dc->num_servers];
    memset(initialized, 0, sizeof(initialized));
    size_t vms_count = 0;
    for (size_t i = 0; i < reservation->items_count; i++) {
        ReservationItem *item = &reservation->items[i];
        if (item->amount == 0) {
			pthread_mutex_lock(&dc->output_mutex);
            fprintf(stderr, "Reservation item must request at least one VM.\n");
			pthread_mutex_unlock(&dc->output_mutex);
            return 1;
        }
        VMType *vm_type = item->vm_type;
        size_t server_index = item->server_id - 1;
        if (!initialized[server_index]) {
            available[server_index] = dc->servers[server_index].available;
            hosted_count[server_index] = dc->servers[server_index].num_hosted_vms;
            initialized[server_index] = 1;
        }
        for (size_t j = 0; j < item->amount; j++) {
            if (vms_count >= MAX_RESERVATION_VMS) {
				pthread_mutex_lock(&dc->output_mutex);
                fprintf(stderr, "Reached max of VMS for a reservation.\n");
				pthread_mutex_unlock(&dc->output_mutex);
                return 1;
            }
            if (hosted_count[server_index] >= MAX_HOSTED_VMS) {
				pthread_mutex_lock(&dc->output_mutex);
                fprintf(stderr, "Server \"%zu\" reached the maximum number of hosted VMs.\n", item->server_id);
				pthread_mutex_unlock(&dc->output_mutex);
                return 1;
            }
            if (!resources_can_fit(available[server_index], vm_type->required)) {
				pthread_mutex_lock(&dc->output_mutex);
                fprintf(stderr, "Server \"%zu\" does not have enough resources.\n", item->server_id);
				pthread_mutex_unlock(&dc->output_mutex);
                return 1;
            }
            resources_sub(&available[server_index], vm_type->required);
            hosted_count[server_index]++;
            vms_count++;
        }
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
				pthread_mutex_lock(&dc->output_mutex);
				fprintf(stderr, "Failed to allocate VM.\n");
				pthread_mutex_unlock(&dc->output_mutex);
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
		pthread_mutex_lock(&dc->output_mutex);
		fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
		pthread_mutex_unlock(&dc->output_mutex);
		return NULL;
	}

	if (res->state != RES_STATE_PENDING) {
		pthread_mutex_lock(&dc->output_mutex);
		fprintf(stderr, "Reservation ID %s is not in a pending state.\n", reservation_id);
		pthread_mutex_unlock(&dc->output_mutex);
		return NULL;
	}

	return res;
}


void reservation_destroy(DataCenter *dc, Reservation *reservation) {
	// Free every VM belonging to the reservation.
	for (size_t i = 0; i < reservation->num_vms; i++) {
		VM *vm = reservation->vms[i];
		Server *server = vm->server;
		pthread_mutex_lock(&server->mutex);
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
        pthread_mutex_unlock(&server->mutex);
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

// Recebe a VM, o ID da reserva, o número de servidores e o número de cores locais atribuídos ao CloudIST.
void spawn_vm_child(VM *vm, const char *reservation_id, size_t num_servers, size_t local_cores) {
	char vm_dir[MAX_PATH_SIZE];
	int written = snprintf(vm_dir, sizeof(vm_dir), "/tmp/CloudIST-13/%s/%s", reservation_id, vm->id);
	if (written < 0 || (size_t)written >= sizeof(vm_dir)) {
		_exit(1);
	}
	if (chdir(vm_dir) != 0) {
		perror("chdir");
		_exit(1);
	}
	struct rlimit lim;
	// Obtém a RAM necessária em GB, converte-a para rlim_t e multiplica por 2^30 para obter bytes.
	lim.rlim_cur = (rlim_t)vm->type->required.ram << 30;
	lim.rlim_max = lim.rlim_cur;
	// Tenta limitar a memória virtual do processo
	if (setrlimit(RLIMIT_AS, &lim) != 0) {
		perror("setrlimit");
        _exit(1);
    }
    lim.rlim_cur = (rlim_t)vm->type->required.disk << 30;
    lim.rlim_max = lim.rlim_cur;
	// Tenta limitar o tamanho máximo dos ficheiros escritos pelo processo.
    if (setrlimit(RLIMIT_FSIZE, &lim) != 0) {
        perror("setrlimit");
        _exit(1);
    }
	double cpu_percentage = 100.0 * vm->type->required.cpu * (double)local_cores / (vm->server->total.cpu * (double)num_servers);
    long cpu_limit_value = lround(cpu_percentage);
	long max_cpu_limit = 100 * (long)local_cores;
	if (cpu_limit_value < 1) cpu_limit_value = 1;
	if (cpu_limit_value > max_cpu_limit) cpu_limit_value = max_cpu_limit;
    char cpu_limit[MAX_STRING_SIZE];
    snprintf(cpu_limit, sizeof(cpu_limit), "%ld", cpu_limit_value);
	execlp("cpulimit", "cpulimit", "-q", "-f", "-l", cpu_limit, "--", vm->type->exec_path, (char *)NULL);
    perror("execlp");
    _exit(1);
}

// Cria os processos filhos para todas as VMs da reserva. dc: ponteiro para o datacenter; res: ponteiro para a reserva.
// Retorna 0 se todos os forks tiverem sucesso e 1 se algum falhar.
int spawn_all_vms(DataCenter *dc, Reservation *res) {
	for (size_t i = 0; i < res->num_vms; i++) {
		VM *vm = res->vms[i];
		pid_t pid = fork();
        if (pid < 0) {
            pthread_mutex_lock(&dc->output_mutex);
			perror("fork");
			pthread_mutex_unlock(&dc->output_mutex);
            return 1;
        }
        if (pid == 0) spawn_vm_child(vm, res->id, dc->num_servers, dc->local_cores);
        vm->pid = pid;
        vm->state = VM_STATE_RUNNING;
        dc->active_children++;
        pthread_cond_signal(&dc->child_cond);
	}
	return 0;
}

// Procura uma VM pelo PID do processo terminado e remove-a, libertando a memória e devolvendo os recursos ao servidor.
static void destroy_finished_vm(DataCenter *dc, pid_t pid) {
    for (size_t i = 0; i < dc->num_reservations; i++) {
        Reservation *res = &dc->reservations[i];
        for (size_t j = 0; j < res->num_vms; j++) {
            VM *vm = res->vms[j];
            if (vm->pid != pid)
                continue;
            if (res->num_vms == 1) {
                reservation_destroy(dc, res);
                return;
            }
            Server *server = vm->server;
			pthread_mutex_lock(&server->mutex);
            resources_add(&server->available, vm->type->required);
            for (size_t k = 0; k < server->num_hosted_vms; k++) {
				if (server->hosted_vms[k] == vm) {
					// Desloca os ponteiros das VMs seguintes uma posição para a esquerda, eliminando a VM da lista sem deixar espaços vazios.
                    memmove(&server->hosted_vms[k],&server->hosted_vms[k + 1],(server->num_hosted_vms - k - 1)*sizeof(VM *));
                    server->num_hosted_vms--;
                    server->hosted_vms[server->num_hosted_vms] = NULL;
                    break;
                }
            }
			pthread_mutex_unlock(&server->mutex);
            memmove(&res->vms[j], &res->vms[j + 1], (res->num_vms - j - 1) * sizeof(VM *));
            res->num_vms--;
            res->vms[res->num_vms] = NULL;
            free(vm);
            return;
        }
    }
}

// Função executada por uma thread responsável por esperar pela terminação
// dos processos filhos das VMs e libertar os respetivos recursos.
void *vm_wait_thread(void *arg) {
    DataCenter *dc = (DataCenter *)arg;
    while (1) {
        pthread_mutex_lock(&dc->mutex);
        // Enquanto não existirem filhos ativos e o programa não estiver a encerrar, a thread fica à espera.
        while (dc->active_children == 0 && !dc->shutting_down) {
			// Adormece a thread, libertando temporariamente o mutex.
            // Quando for acordada, volta a adquirir o mutex antes de continuar a execução.
            pthread_cond_wait(&dc->child_cond, &dc->mutex);
		}
        // Se o datacenter estiver a encerrar e não existirem processos filhos ativos, a thread pode terminar.
        if (dc->shutting_down && dc->active_children == 0) {
            pthread_mutex_unlock(&dc->mutex);
            return NULL;
        }
        pthread_mutex_unlock(&dc->mutex);
        pid_t pid = wait(NULL);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
			pthread_mutex_lock(&dc->output_mutex);
            perror("wait");
			pthread_mutex_unlock(&dc->output_mutex);
            continue;
        }
		pthread_rwlock_rdlock(&dc->state_lock);
		pthread_mutex_lock(&dc->mutex);
		destroy_finished_vm(dc, pid);
		dc->active_children--;
		pthread_mutex_unlock(&dc->mutex);
		pthread_rwlock_unlock(&dc->state_lock);
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