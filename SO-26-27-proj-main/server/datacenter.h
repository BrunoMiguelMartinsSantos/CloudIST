#ifndef DATACENTER__H
#define DATACENTER__H

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>

#include "resources.h"
#include "constants.h"
#include "filesystem.h"
#include "common.h"

typedef struct DataCenter {
	Server *servers;			     // List of Servers.
	size_t num_servers;              // Number of Servers.
    size_t local_cores;
	VMType vm_types[MAX_VM_TYPES];
    size_t num_vm_types;

	Reservation reservations[MAX_RESERVATIONS];
    size_t num_reservations;

	int configured;		      // Wheter the datacenter is configured or not.
    pthread_mutex_t mutex;
    pthread_cond_t child_cond;
    size_t active_children;
    int shutting_down;
    pthread_rwlock_t state_lock;
    pthread_mutex_t output_mutex;
} DataCenter;

/**
 * Initializes a Data Center.
 *
 * @param dc DataCenter being initialized.
 */
void datacenter_init(DataCenter *dc);

/**
 * Destroys a Data Center.
 *
 * @param dc DatacCenter being destroyed.
 */
void datacenter_destroy(DataCenter *dc);

/**
 * Configures a given Data Center.
 *
 * @param dc Pointer to Datacenter being configured.
 * @param resources Pointer to resources of every PC on the Datacenter.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_configure(DataCenter *dc, size_t num_pcs, Resources *resources);

/**
 * Defines a VM for the Data Center
 *
 * @param dc Pointer to a Data Center.
 * @param type Pointer to VM type.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_define_VM(DataCenter *dc, struct VMType *type);

/**
 * Reserves a set of VMs for a given reservation.
 *
 * @param dc Pointer to a Data Center.
 * @param reservation Pointer to a Reservation.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_reserve(DataCenter *dc, Reservation *reservation);

/**
 * Lists the current state of the Data Center.
 *
 * @param dc Pointer to a Data Center.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_list(DataCenter *dc);

/**
 * Executes a reservation, launching the VMs.
 *
 * @param dc Pointer to a Data Center.
 * @param reservation_id ID of the reservation to execute.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_execute(DataCenter *dc, const char *reservation_id);

/**
 * Executes a delay command.
 * 
 * @param delay_ms Delay in ms.
 */
void datacenter_wait(unsigned int delay_ms);

#endif // DATACENTER__H