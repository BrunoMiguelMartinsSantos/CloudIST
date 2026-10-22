#include "datacenter.h"
#include "datacenter_utils.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

void datacenter_init(DataCenter *dc) {
  dc->servers = NULL;
  dc->num_servers = 0;
  dc->local_cores = 0; // alterado devido à nova fórmula2
  dc->num_vm_types = 0;

  dc->num_reservations = 0;

  dc->configured = 0;
  // Exercício 2. Inicializa a 0 o número de processos filhos de VMs ativos
  dc->active_children = 0;
  // Inicializa a 0 (false), indicando que o datacenter não está a encerrar.
  dc->shutting_down = 0;
  // Inicializa o mutex para proteger o acesso aos dados partilhados;
  // NULL indica que são usadas as configurações padrão.
  pthread_mutex_init(&dc->mutex, NULL);
  // Inicializa a variável de condição usada para acordar a thread
  // que espera pelos processos filhos; NULL usa as configurações padrão. 
  pthread_cond_init(&dc->child_cond, NULL);
  // E3 sincronização.
  pthread_rwlock_init(&dc->state_lock, NULL);
  pthread_mutex_init(&dc->output_mutex, NULL);
}

void datacenter_destroy(DataCenter *dc) {
  for (size_t i = 0; i < dc->num_servers; i++) {
    for(size_t j = 0; j < dc->servers[i].num_hosted_vms; j++){
      free(dc->servers[i].hosted_vms[j]);
    }
    // E3 sincronização
    pthread_mutex_destroy(&dc->servers[i].mutex);
  }
  free(dc->servers);
  // Exercicio 2
  // Destrói a variável de condição usada para acordar a thread que espera pelos processos filhos das VMs.
  pthread_cond_destroy(&dc->child_cond);
  // Destrói o mutex utilizado para proteger os dados partilhados do datacenter contra acessos simultâneos de várias threads.
  pthread_mutex_destroy(&dc->mutex);
  // E3 sincronização
  pthread_rwlock_destroy(&dc->state_lock);
  pthread_mutex_destroy(&dc->output_mutex);
}

int datacenter_configure(DataCenter *dc, size_t num_servers, Resources *resources){
  if(dc->configured){
    fprintf(stderr, "Data Center is already configured!\n");
    return 1;
  }

  if (num_servers == 0 || resources->ram == 0 || resources->disk == 0 || resources->cpu <= 0.0) {
    fprintf(stderr, "Invalid configuration values.\n");
    return 1;
  }

  dc->servers = (Server*)malloc(num_servers*sizeof(Server));

  if(dc->servers == NULL){
    fprintf(stderr, "Failed to allocate memory for servers.\n");
    return 1;
  }

  for (size_t i = 0; i < num_servers; i++) {
    dc->servers[i].id = i+1;
    dc->servers[i].total = *resources;
    dc->servers[i].available = *resources;
    dc->servers[i].num_hosted_vms = 0;
    /*
    * E3 sincronização: cada servidor tem o seu próprio mutex.
    */
    pthread_mutex_init(&dc->servers[i].mutex, NULL);
  }

  dc->num_servers = num_servers;
  dc->configured = 1;

  return 0;
}

int datacenter_define_VM(DataCenter *dc, VMType *type){
  if (!dc->configured) {
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Data Center needs to be configured!\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }

  if(VMType_exists(dc, type->id) != NULL){
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "VM type with id %s already exists.\n", type->id);
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }

  if (!file_exists(type->exec_path)) {
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Invalid executable path.\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }
  /* E2:
  * Guarda o caminho absoluto do executável porque
  * a VM vai mudar para a sua própria diretoria antes do exec.
  */
  char absolute_exec[MAX_PATH_SIZE];
  if (absolute_path(type->exec_path, absolute_exec, sizeof(absolute_exec)) != 0) {
    pthread_mutex_lock(&dc->output_mutex);
    fprintf(stderr, "Invalid executable path.\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }

  strcpy(type->exec_path, absolute_exec);
  if (!path_exists(type->input_folder)) {
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Invalid input folder.\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }

  if(dc->num_vm_types >= MAX_VM_TYPES){
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Maximum of VM types reached.\n");
    pthread_mutex_unlock(&dc->output_mutex); // E3
    return 1;
  }
  
  dc->vm_types[dc->num_vm_types++] = *type;

  return 0;
}


// E3 sincronização: verifica se uma reserva utiliza determinado servidor.
static int reservation_uses_server(Reservation *reservation, size_t server_id) {
    for (size_t i = 0; i < reservation->items_count; i++) {
        if (reservation->items[i].server_id == server_id) return 1;
    }
    return 0;
}


/*
 * E3 sincronização: bloqueia apenas os servidores utilizados por esta reserva.
 * São sempre bloqueados por ordem crescente para evitar deadlock.
 */
static void lock_reservation_servers(DataCenter *dc,Reservation *reservation) {
    for (size_t i = 0; i < dc->num_servers; i++) {
        if (reservation_uses_server(reservation,dc->servers[i].id)) {
            pthread_mutex_lock(&dc->servers[i].mutex);
        }
    }
}


// E3 sincronização: liberta os servidores bloqueados pela reserva.
static void unlock_reservation_servers(DataCenter *dc,Reservation *reservation) {
    for (size_t i = dc->num_servers; i > 0; i--) {
        if (reservation_uses_server(reservation, dc->servers[i - 1].id)) {
            pthread_mutex_unlock(&dc->servers[i - 1].mutex);
        }
    }
}


// E3 sincronização: deve ser chamada com dc->mutex bloqueado.
static int reservation_id_exists(DataCenter *dc, const char *reservation_id) {
    for (size_t i = 0; i < dc->num_reservations; i++) {
        if (strncmp(reservation_id, dc->reservations[i].id, MAX_STRING_SIZE) == 0) {
            return 1;
        }
    }
    return 0;
}

int datacenter_reserve(DataCenter *dc, Reservation *reservation) {
  if (!dc->configured) {
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Data Center needs to be configured!\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }
  /*
  * E3 sincronização: consulta apenas informação global do DataCenter.
  * Este lock é curto e não inclui a reserva dos recursos.
  */
  pthread_mutex_lock(&dc->mutex);
  // Verifica se já existe uma reserva com este ID.
  if (reservation_id_exists(dc, reservation->id)) {
      pthread_mutex_lock(&dc->output_mutex); // E3
      fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
      pthread_mutex_unlock(&dc->output_mutex);
      pthread_mutex_unlock(&dc->mutex);
      return 1;
  }
  // Verifica se ainda existe espaço na lista global de reservas.
  if (dc->num_reservations >= MAX_RESERVATIONS) {
        pthread_mutex_lock(&dc->output_mutex); // E3
        fprintf(stderr, "Max number of reservations reached\n");
        pthread_mutex_unlock(&dc->output_mutex);
        pthread_mutex_unlock(&dc->mutex);
        return 1;
  }
  // Valida os IDs dos servidores e resolve os tipos de VM enquanto os dados globais estão protegidos.
  for (size_t i = 0; i < reservation->items_count; i++) {
      ReservationItem *item = &reservation->items[i];
      if (item->server_id == 0 || item->server_id > dc->num_servers) {
          pthread_mutex_lock(&dc->output_mutex); // E3
          fprintf(stderr, "Invalid server in reservation.\n");
          pthread_mutex_unlock(&dc->output_mutex);
          pthread_mutex_unlock(&dc->mutex);
          return 1;
        }
      item->vm_type = VMType_exists(dc, item->vm_type_id);
      if (item->vm_type == NULL) {
            pthread_mutex_lock(&dc->output_mutex); // E3
            fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
            pthread_mutex_unlock(&dc->output_mutex); // E3
            pthread_mutex_unlock(&dc->mutex);
            return 1;
      }
  }
  // Já não precisamos do lock global.
  pthread_mutex_unlock(&dc->mutex);
  // E3 sincronização: daqui para a frente bloqueamos APENAS os servidores utilizados por esta reserva.
  lock_reservation_servers(dc, reservation);
  // Simula a reserva usando o estado atual dos servidores já protegidos.
  if (reservation_validate(dc, reservation) != 0) {
      unlock_reservation_servers(dc, reservation);
      return 1;
  }
  if (reservation_commit(dc, reservation) != 0) {
      unlock_reservation_servers(dc, reservation);
      return 1;
  }
  // A alteração dos servidores terminou.
  unlock_reservation_servers(dc, reservation);
  // Só a inserção na lista global das reservas precisa novamente do mutex global.
  pthread_mutex_lock(&dc->mutex);
  dc->reservations[dc->num_reservations] = *reservation;
  dc->num_reservations++;
  pthread_mutex_unlock(&dc->mutex);
  return 0;
}

int datacenter_execute(DataCenter *dc, const char *reservation_id) {
  if(!dc->configured){
    pthread_mutex_lock(&dc->output_mutex); // E3
    fprintf(stderr, "Data Center needs to be configured!\n");
    pthread_mutex_unlock(&dc->output_mutex);
    return 1;
  }

  Reservation *res = find_pending_reservation(dc, reservation_id);
  if (!res) return 1;

  /*
   * Exercício 1 (ADICIONADO):
   * Antes de executar as VMs, cria a diretoria de cada uma
   * em /tmp/CloudIST/<ID_RESERVA>/<ID_VM> e copia para lá os ficheiros da diretoria de input.
   */
  for (size_t i = 0; i < res->num_vms; i++) {
    VM *vm = res->vms[i];
    if (prepare_vm_filesystem(res->id, vm->id, vm->type->input_folder) != 0) {
      pthread_mutex_lock(&dc->output_mutex);  // E3
      fprintf(stderr, "Failed to prepare filesystem for VM %s.\n", vm->id);
      pthread_mutex_unlock(&dc->output_mutex);
      return 1;
    }
  }
  // Exercício 2: cria um processo por VM.
  if (spawn_all_vms(dc,res) != 0) return 1;
  res->state = RES_STATE_RUNNING;
  return 0;
}

int datacenter_list(DataCenter *dc) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  printf("=================================== DATA CENTER STATUS ===================================\n");
  printf("\n[ PHYSICAL SERVERS ] (%zu total)\n", dc->num_servers);
  printf("------------------------------------------------------------------------------------------\n");

  for (size_t i = 0; i < dc->num_servers; i++) {
    /*
    * E3 sincronização:
    * usamos um ponteiro para não copiar o mutex existente em Server.
    * dentro do ciclo for tudo o que era server. passa a server ->
    */
    Server *server = &dc->servers[i];

    Resources used = server->total;
    resources_sub(&used, server->available);

    printf(" Server #%zu\n", server->id);
    printf("   ├─ Resources Total     : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server->total.cpu, server->total.ram, server->total.disk);
    printf("   ├─ Resources Used      : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            used.cpu, used.ram, used.disk);
    printf("   ├─ Resources Available : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server->available.cpu, server->available.ram, server->available.disk);
    printf("   └─ Hosted VMs (%zu):\n", server->num_hosted_vms);

    if (server->num_hosted_vms == 0) {
      printf("        (None)\n");
    } else {
      for (size_t j = 0; j < server->num_hosted_vms; j++) {
        VM *vm = server->hosted_vms[j];
        const char *prefix = (j == server->num_hosted_vms - 1) ? "└──" : "├──";

        printf("        %s VM ID : %-16s | Type: %-10s | State: %-10s",
                prefix,
                vm->id,
                vm->type ? vm->type->id : "Unknown",
                vm_state_to_string(vm->state));

        if (vm->state == VM_STATE_RUNNING) {
          printf(" | PID: %d", (int)vm->pid);
        }
        printf("\n");
      }
    }
    printf("\n");
  }

  printf("[ DEFINED VM TYPES ] (%zu registered)\n", dc->num_vm_types);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_vm_types == 0) {
    printf(" (None)\n\n");
  } else {
      for (size_t i = 0; i < dc->num_vm_types; i++) {
        VMType vt = dc->vm_types[i];
        printf("  * Type ID: %-12s | Requirements: %.2f vCPU, %zu GB RAM, %zu GB Disk\n",
                vt.id, vt.required.cpu, vt.required.ram, vt.required.disk);
        printf("    ├─ Executable : %s\n", vt.exec_path);
        printf("    └─ Input Dir  : %s\n\n", vt.input_folder);
      }
      printf("\n");
  }

  printf("[ RESERVATIONS ] (%zu total)\n", dc->num_reservations);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_reservations == 0) {
    printf(" (None)\n\n");
  } else {
    for (size_t i = 0; i < dc->num_reservations; i++) {
      Reservation res = dc->reservations[i];
      printf("  * Reservation ID: %-12s | State: %-8s | Total VMs: %zu\n",
              res.id, res_state_to_string(res.state), res.num_vms);

      printf("    ├─ Requested Pairs:\n");
      for (size_t k = 0; k < res.items_count; k++) {
        printf("    │    • %-12s : %zu unit(s)\n", res.items[k].vm_type_id, res.items[k].amount);
      }

      printf("    └─ Allocated VMs:\n");
      if (res.num_vms == 0) {
        printf("         (None)\n");
      } else {
          for (size_t k = 0; k < res.num_vms; k++) {
            VM *vm = res.vms[k];
            const char *prefix = (k == res.num_vms - 1) ? "└──" : "├──";
            printf("         %s ID: %-15s | Host Server: #%zu\n",
                    prefix,
                    vm->id,
                    vm->server->id);
          }
      }
      printf("\n");
    }
  }

  printf("================================================================--------------------------\n\n");
  return 0;
}

void datacenter_wait(unsigned int delay_ms){
  struct timespec delay = (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}