#include "parser.h"
#include "api.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

void run_commands(int);

int main(int argc, char **argv){
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s <client_id> <request_pipe_path> <response_pipe_path> <register_pipe_path> [.conf_file]\n", argv[0]);
        return 1;
    }

    //stdin by default
    int input_fd = STDIN_FILENO;
    unsigned int client_id;
    const char *req_pipe_path = argv[2];
    const char *resp_pipe_path = argv[3];
    const char *register_pipe_path = argv[4];

    if (argc == 6) {
        const char* dot = strrchr(argv[5], '.');
        if (dot == NULL || dot == argv[5] || strlen(dot) != 5 || strcmp(dot, ".conf") ||
            strlen(argv[5]) > MAX_PATH_SIZE) {
            fprintf(stderr, "The provided .conf file path is not valid. Path: %s\n", argv[5]);
            return 1;
        }

        input_fd = open(argv[5], O_RDONLY);
        if (input_fd == -1) {
            fprintf(stderr, "Failed to open input file. Path: %s\n", argv[5]);
            return 1;
        }
    }

    size_t parsed_long;
    if(parse_size_t_arg(argv[1], &parsed_long)) {
        fprintf(stderr, "client_id must be a number. Got: %s instead.\n", argv[1]);
        return 1;
    }
    client_id = (unsigned int) parsed_long;
    
    if (cloudIst_connect(req_pipe_path, resp_pipe_path, register_pipe_path, client_id)) {
        return 1;
    }

    run_commands(input_fd);

    int ret = 0;

    if (cloudIst_disconnect() != 0)
        ret = 1;
    
    if (close(input_fd) == -1) {
        perror("Error closing input file");
        return 1;
    }

    return ret;
}

void run_commands(int fd) {
    
    if (fd == STDIN_FILENO) {
            printf("Enter \"H\" to check available commands!\n\n");
            fflush(stdout);
        }

    while (1) {
        if (fd == STDIN_FILENO) {
            printf("> ");
            fflush(stdout);
        }

        ParsedCommand cmd = {0};
        parse_cmd(fd, &cmd);

        if (cmd.type == EOC)
            break;
        
        if (cmd.type == CMD_INVALID)
            continue;

        if (send_command(&cmd)) {
           //EOC and CMD_INVALID should not be passed to send_command
           break;
        }
    }
}
