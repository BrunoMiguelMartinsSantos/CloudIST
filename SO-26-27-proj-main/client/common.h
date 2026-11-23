#ifndef COMMON__H
#define COMMON__H
#include <stddef.h>
#include <sys/types.h>

#include "constants.h"

typedef struct {
    size_t ram;  // RAM in GB
    size_t disk; // Disk Space in GB
    double cpu;  // Number of cores/Vcpu
} Resources;

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

    pthread_mutex_t lock;
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

#endif // COMMON_H