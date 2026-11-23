#ifndef API__H
#define API__H

#include <stddef.h>

#include "common.h"
#include "parser.h"

// Opcodes for client-server communication
typedef enum {
  CONNECT = 1,
  DISCONNECT = 2,
  DEFINE = 3,
  RESERVE = 4,
  EXE = 5,
  LIST = 6,
  NOOP = 7
} Opcode;


/// Connects to an CloudIst server.
/// @param req_pipe_path Path to the name pipe to be created for requests.
/// @param resp_pipe_path Path to the name pipe to be created for responses.
/// @param server_pipe_path Path to the name pipe where the server is listening.
/// @param client_id unique number to identify the client in the server.
/// @return 0 if the connection was established successfully, 1 otherwise.
int cloudIst_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path, unsigned int client_id);

/// Disconnects from an CloudIst server.
/// @return 0 in case of success, 1 otherwise.
int cloudIst_disconnect(void);


/// @param VMType vm_type
/// @return 0 if the type was created successfully, 1 otherwise.
int cloudIst_define(VMType vm_type);


/// Creates a new reservation for repective items
/// @param Reservation reservation
/// @return 0 if the reservation was created successfully, 1 otherwise.
int cloudIst_reserve(Reservation reservation);

/// Starts the given reservation
/// @param reservation_id string that identifies the reservation
/// @return 0 if the reservation was started succefully, 1 otherwise.
int cloudIst_run(char* reservation_id);

/// lists all the vms of the client that are associated to reservations 
/// @return 0 if the reservation was started succefully, 1 otherwise.
int cloudIst_list_vms();

/// Send the command cmd to the server
/// @param cmd The command after being parsed
/// @return 1 if the command is invalid, 0 otherwise.
int send_command(ParsedCommand *cmd);


#endif // API__H