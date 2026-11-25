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

int cloudIst_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path, unsigned int client_id) {
    size_t req_len = strlen(req_pipe_path);
    size_t resp_len = strlen(resp_pipe_path);

    // nao sei se é preciso verificar tbm se request_fd >= 0?
    // se o cliente só chama a função uma vez por sessão, acho que é desnecessario
    
    // acho q é preciso, pq se o fifo do servidor ainda não existir o código continua a correr e tenta enviar algo para um pipe que não existe
    // agora se é suposto que o cliente espere ou termine, n sei
    if (req_len >= MAX_PIPE_PATH_LENGTH || resp_len >= MAX_PIPE_PATH_LENGTH){
        perror("invalid path to new fifo");
        return 1;
    }

    // retirei a verificação anterior pq o mkfifo já faz isso e dá pra distinguir pelo erro retornado.
    // E não é possível o cliente estar ligado a um pipe, sendo que n


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

    request_fd = open(req_pipe_path, O_WRONLY);
    if (request_fd == -1) {
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    response_fd = open(resp_pipe_path, O_RDONLY);
    if (response_fd == -1) {
        close(request_fd);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    int reg_fd = open(server_pipe_path, O_WRONLY);
    if (reg_fd == -1) {
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    char msg[4 + 2 * MAX_PIPE_PATH_LENGTH + sizeof(client_id)];
    memset(msg, '\0', sizeof(msg));
    msg[0] = '1';
    msg[1] = '|';

    memcpy(msg + 2, req_pipe_path, req_len);
    msg[2 + MAX_PIPE_PATH_LENGTH] = '|';
    memcpy(msg + 3 + MAX_PIPE_PATH_LENGTH, resp_pipe_path, resp_len);
    msg[3 + 2 * MAX_PIPE_PATH_LENGTH] = '|';
    memcpy(msg + 4 + 2 * MAX_PIPE_PATH_LENGTH, &client_id, sizeof(client_id));

    int sent = write_all(reg_fd, msg, sizeof(msg));

    close(reg_fd);

    
    if (sent == -1) {
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }


    // TODO: These values should be initialized with data received from the server
    char result;
    size_t num_servers = 0, ram = 0, disk = 0;
    double cpus = 0.0;

    if (read_all(response_fd, &result, sizeof(result)) != 1 || result != 0 ||
        read_all(response_fd, &num_servers, sizeof(num_servers)) != 1 ||
        read_all(response_fd, &ram, sizeof(ram)) != 1 ||
        read_all(response_fd, &disk, sizeof(disk)) != 1 ||
        read_all(response_fd, &cpus, sizeof(cpus)) != 1) {
        close(request_fd);
        close(response_fd);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }
    strcpy(request_path, req_pipe_path);
    strcpy(response_path, resp_pipe_path);

    pretty_print_cloud_specs(num_servers, ram, disk, cpus);
    return 0;
}


int cloudIst_disconnect(void){
    if (request_fd == -1) return 1;

    char opcode = '2';
    int sent = write_all(request_fd, &opcode, sizeof(opcode));

    close(request_fd);
    close(response_fd);
    unlink(request_path);
    unlink(response_path);

    request_fd = -1;
    response_fd = -1;

    if (sent == 1) {
        return 0;
    }
    return 1;
}

int cloudIst_define(VMType vm_type) {
    (void) vm_type;
    //TODO: send define command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_reserve(Reservation reservation) {
    (void) reservation;
    //TODO: send reserve command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_run(char *reservation_id) {
    (void) reservation_id;
    //TODO: send run/arranca command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_list_vms() {
    //TODO: send List command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");

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