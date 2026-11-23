#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include "api.h"
#include "parser.h"


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
    
    (void) req_pipe_path;
    (void) resp_pipe_path;
    (void) server_pipe_path;
    (void) client_id;

    //TODO: create pipes and connect to the server through the server_pipe_path
    printf("Connect: Implement me pls\n");

    // TODO: These values should be initialized with data received from the server
    size_t num_servers = 0, ram = 0, disk = 0;
    double cpus = 0.0;

    //Print server's specifications to stdout
    pretty_print_cloud_specs(num_servers, ram, disk, cpus);
    return 0;
}


int cloudIst_disconnect(void){
    //TODO close pipes
    printf("Implement me pls\n");
    return 0;
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