#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include "api.h"
#include "parser.h"
#include "filesystem.h"

// Guarda os descritores da sessão e os caminhos dos FIFOs para os remover no disconnect.
static int request_fd = -1;
static int response_fd = -1;
static char request_path[MAX_PIPE_PATH_LENGTH];
static char response_path[MAX_PIPE_PATH_LENGTH];

void pretty_print_cloud_specs(size_t num_servers, size_t ram, size_t disk, double cpus) {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║         CloudIST - Connected!            ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║                                          ║\n");
    printf("║  Servers       : %-24zu║\n", num_servers);
    printf("║  RAM / Server  : %-24zu║\n", ram);
    printf("║  Disk / Server : %-24zu║\n", disk);
    printf("║  CPUs / Server : %-24.2f║\n", cpus);
    printf("║                                          ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("\n");
}

char *convert_vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        case VM_STATE_TERMINATED: return "TERMINATED";
        default:                  return "UNKNOWN";
    }
}

void print_vm(char * vm_id, char * vm_type_id, size_t server_id, char vm_state) {
    printf("VM ID : %s | VM Type: %s | Server ID: %zu | State: %s\n",
                vm_id,
                vm_type_id,
                server_id,
                convert_vm_state_to_string((VMState) vm_state));
}

// O cliente cria os dois FIFOs privados. Se a ligação falhar, remove os que já criou.
int cloudIst_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path, unsigned int client_id) {
    if (request_fd >= 0) return 1;
    if (strlen(req_pipe_path) >= MAX_PIPE_PATH_LENGTH || strlen(resp_pipe_path) >= MAX_PIPE_PATH_LENGTH){
        perror("invalid path to new fifo");
        return 1;
    }

    if (mkfifo(req_pipe_path, S_IRUSR | S_IWUSR | S_IRGRP) == -1) {
        if (errno == EEXIST) {
            fprintf(stderr,"Path (%s) to fifo already exists.", req_pipe_path);
        }
        else{
            perror("[ERR]: mkfifo failed");
        }
        return 1;
    }

    if (mkfifo(resp_pipe_path, S_IRUSR | S_IWUSR | S_IWGRP) == -1) {
        if (errno == EEXIST) {
            fprintf(stderr,"Path (%s) to fifo already exists.", req_pipe_path);
        }
        else{
            perror("[ERR]: mkfifo failed");
        }
        unlink(req_pipe_path);
        return 1;
    }
    // O CONNECT envia o ID do cliente e os dois caminhos em campos de tamanho fixo.
    // O strncpy preenche com zeros o espaço que sobra em cada caminho.
    ConnectRequest request;
    request.opcode = CONNECT;
    request.client_id = client_id;
    strncpy(request.request_path, req_pipe_path, sizeof request.request_path);
    strncpy(request.response_path, resp_pipe_path, sizeof request.response_path);

    int reg_fd = open(server_pipe_path, O_WRONLY);
    if (reg_fd == -1) {
        perror(server_pipe_path);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    int sent = write_all(reg_fd, &request, sizeof request);
    close(reg_fd);

    if (sent != 1) {
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    request_fd = open(req_pipe_path, O_WRONLY);
    if (request_fd == -1) {
        perror(req_pipe_path);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    response_fd = open(resp_pipe_path, O_RDONLY);
    if (response_fd == -1) {
        perror(resp_pipe_path);
        close(request_fd);
        request_fd = -1;
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    ConnectResponse response;
    if (read_all(response_fd, &response, sizeof response) != 1 ||
        response.result != 0) {
        close(request_fd);
        close(response_fd);
        request_fd = -1;
        response_fd = -1;
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    strcpy(request_path, req_pipe_path);
    strcpy(response_path, resp_pipe_path);

    pretty_print_cloud_specs(response.num_servers, response.ram, response.disk, response.cpus);
    return 0;
}

// O DISCONNECT não tem resposta. Após o envio, o cliente fecha e remove os seus FIFOs.
int cloudIst_disconnect(void){
    if (request_fd == -1) return 1;

    char opcode = (char) DISCONNECT;
    int sent = write_all(request_fd, &opcode, sizeof(char));

    close(request_fd);
    close(response_fd);
    unlink(request_path);
    unlink(response_path);

    request_fd = -1;
    response_fd = -1;

    if (sent == 1) return 0;

    return 1;
}

// Envia o opcode seguido da definição da VM pelo FIFO de pedidos.
// O servidor devolve um byte pelo FIFO de respostas: 0 indica sucesso.
int cloudIst_define(VMType vm_type) {
    char opcode = (char) DEFINE;
    VMType pedido;

    strncpy(pedido.id, vm_type.id, sizeof pedido.id);
    strncpy(pedido.input_folder, vm_type.input_folder, sizeof pedido.input_folder);
    strncpy(pedido.exec_path, vm_type.exec_path, sizeof pedido.exec_path);
    pedido.required = vm_type.required;

    if (write_all(request_fd, &opcode, sizeof (char)) != 1 || write_all(request_fd, &pedido, sizeof (VMType)) != 1) {
        return 1;
    }

    char result;
    if (read_all(response_fd, &result, sizeof (char)) != 1 || result != 0) {
        return 1;
    }

    return 0;
}


int cloudIst_reserve(Reservation reservation) {
    char opcode = (char) RESERVE;
    ReserveRequest pedido = {0};

    strncpy(pedido.reservation_id,reservation.id, sizeof(pedido.reservation_id));
    pedido.items_count=reservation.items_count;

    if (write_all(request_fd, &opcode, sizeof (char)) != 1 || write_all(request_fd, &pedido, sizeof (pedido)) != 1) {
        return 1;
    }
    // Envia um item de cada vez, pela ordem em que aparece na reserva.
    for (size_t i = 0; i < reservation.items_count; i++){
        ReserveRequestItems item = {0} ;
        strcpy(item.vm_type_id, reservation.items[i].vm_type_id);
        item.amount = reservation.items[i].amount;
        item.server_id = reservation.items[i].server_id;

        if (write_all(request_fd, &item, sizeof (ReserveRequestItems)) != 1) {
            return 1;
        }
    }
    // Depois de enviar todos os itens, espera pela resposta do servidor.
    char result;
    if (read_all(response_fd, &result, sizeof result) != 1 || result != 0) {
        return 1;
    }
    return 0;
}

int cloudIst_run(char *reservation_id) {

    //TODO: send run/arranca command to the server (through the request pipe) and wait for the response (through the response pipe)

    return 0;
}

int cloudIst_list_vms() {
    //TODO: send List command to the server (through the request pipe) and wait for the response (through the response pipe)

    //TODO: dont forget to print output to stdout using the format from function print_vm()
    return 0;
}


int send_command(ParsedCommand *cmd) {

    switch (cmd->type) {
        case CMD_DEFINE:
            if (cloudIst_define(cmd->data.vmtype)) {
                fprintf(stderr, "Error defining VM type %s\n", cmd->data.vmtype.id);
            } else {
                fprintf(stdout, "Defined VM %s\n", cmd->data.vmtype.id);
            }
            break;

        case CMD_RESERVE:
            if (cloudIst_reserve(cmd->data.reservation)) {
                fprintf(stderr, "Error making reservation %s\n", cmd->data.reservation.id);
            } else {
                fprintf(stdout, "Reserved %s\n", cmd->data.reservation.id);
            }
            break;

        case CMD_EXECUTE:
            if (cloudIst_run(cmd->data.id)) {
                fprintf(stderr, "Failed to execute reservation %s.\n", cmd->data.id);
            } else {
                printf("Running reservation %s\n", cmd->data.id);
            }
            break;

        case CMD_LIST:
            if (cloudIst_list_vms()) {
                fprintf(stderr, "Failed to list VMs.\n");
            } else {
                printf("show list");
            }
            break;

        case CMD_WAIT:
            if (cmd->data.delay > 0) {
                printf("Waiting...\n");
                struct timespec delay = {
                    .tv_sec = (time_t)(cmd->data.delay / 1000U),
                    .tv_nsec = (long)(cmd->data.delay % 1000U) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
            break;
            
        case CMD_EMPTY:
            break;

        case CMD_HELP: 
            break;

        case CMD_INVALID:
            return 1;

        case EOC:
            return 1;

    }

    return 0;
}