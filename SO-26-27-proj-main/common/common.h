#ifndef COMMON__H
#define COMMON__H
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>
#include "constants.h"
#include "resources.h"

typedef struct VMType{
    char id[MAX_STRING_SIZE];        // Id of VM type. eg: "V1"
    char input_folder[MAX_PATH_SIZE];  // Folder with input for executable
    char exec_path[MAX_PATH_SIZE];     // Path for executable
    Resources required;
} VMType;

typedef enum {
    VM_STATE_RESERVED,   // placed on a Server, process not started yet (waiting for 'A' command)
    VM_STATE_RUNNING,    // process launched (fork()/exec() called), `pid` is valid
    VM_STATE_TERMINATED  // wait() called, process finished
} VMState;

typedef struct Server Server;

typedef struct VM {
  char id[MAX_VM_ID_STRING]; // VM id. eg: "R1_V1_1"
  VMType *type;   // VM type.
  Server *server; // Server where the VM is placed.
  pid_t pid;      // PID of executable file.
  VMState state;  // State of the VM.
  int client_id;  //TODO use this in the List operation
} VM;

typedef struct Server {
    size_t id;             // Server ID (0, 1, 2, ...)
    Resources total;       // Resources that Server has.
    Resources available;   // Resources still available.

    VM *hosted_vms[MAX_HOSTED_VMS];
    size_t num_hosted_vms;

    pthread_mutex_t mutex;
} Server;

typedef enum {
    RES_STATE_PENDING,   // Reserved via 'R', waiting for 'A'
    RES_STATE_RUNNING,   // 'A' was called
    RES_STATE_FINISHED   // All VMs in this reservation have terminated
} ReservationState;

typedef struct ReservationPair {
    char vm_type_id[MAX_STRING_SIZE]; // VM type. e.g: "V1"       
    size_t amount;                    // Amount of VMs with the given type being reserved.
    size_t server_id;                 // Server where each VM will be running on.

    VMType *vm_type;                   // Used for search optimization.
} ReservationItem;

typedef struct Reservation {
    char id[MAX_STRING_SIZE];                    // id of reservation.

    ReservationItem items[MAX_RESERVATIONS_ITEMS];  // List of reservations items 
    size_t items_count;

    VM *vms[MAX_RESERVATION_VMS];                   // List of VMs in this reservation.
    size_t num_vms;                                 // Number of VMs in this reservation.
    size_t running_vms;                           
    ReservationState state;                         // State of the reservation.
} Reservation;

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

// Pedido CONNECT enviado pelo cliente no FIFO de registo.
// Os caminhos identificam os dois FIFOs privados da sessão.
typedef struct {
    char opcode;
    char request_path[MAX_PIPE_PATH_LENGTH];
    char response_path[MAX_PIPE_PATH_LENGTH];
    unsigned int client_id;
} ConnectRequest;

// Resposta ao CONNECT enviada pelo servidor no FIFO de respostas.
// Se result for 0, a ligação foi aceite e os restantes campos indicam
// o número de servidores e os recursos disponíveis por servidor.
typedef struct {
    char result;
    size_t num_servers;
    size_t ram;
    size_t disk;
    double cpus;
} ConnectResponse;

typedef struct{
    char vm_type_id[MAX_STRING_SIZE];
    size_t amount;
    size_t server_id;
} ReserveRequestItems;

typedef struct{
    char reservation_id[MAX_STRING_SIZE];
    size_t items_count;
} ReserveRequest;

typedef struct {
    char result;
    size_t num_vms;
} ListResponse;

typedef struct {
    char vm_id[44];
    char vm_type_id[MAX_STRING_SIZE];
    int client_id;
    size_t server_id;
    char vm_state;
} ListResponseItem;

#endif // COMMON_H