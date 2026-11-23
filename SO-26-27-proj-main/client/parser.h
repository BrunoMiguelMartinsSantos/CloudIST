#ifndef PARSER__H
#define PARSER__H

#include <stddef.h>

#include "constants.h"
#include "common.h"

typedef enum {
  CMD_DEFINE,
  CMD_RESERVE,
  CMD_EXECUTE,
  CMD_LIST,
  CMD_WAIT,
  CMD_HELP,
  CMD_EMPTY,
  CMD_INVALID,
  EOC // End of commands
} Command;

typedef struct {
    Command type;

    union {
        VMType vmtype;
        Reservation reservation;
        char id[MAX_STRING_SIZE];
        unsigned int delay;
    } data;
} ParsedCommand;

typedef int (*CommandHandler)(ParsedCommand *cmd, void *context);

/**
 * Parses the given null-terminated string as an unsigned decimal integer.
 * The conversion succeeds only if the entire string is consumed and no
 * overflow or conversion error occurs.
 *
 * @param str Null-terminated string to convert.
 * @param out Pointer where the converted value will be stored.
 *
 * @return 0 if the conversion succeeds.
 * @return 1 otherwise.
 */
int parse_size_t_arg(const char *str, size_t *out);

/**
 * Parses the given null-terminated string as a floating-point number.
 * The conversion succeeds only if the entire string is consumed and no
 * overflow or conversion error occurs.
 *
 * @param str Null-terminated string to convert.
 * @param out Pointer where the converted value will be stored.
 *
 * @return 0 if the conversion succeeds.
 * @return 1 otherwise.
 */
int parse_double_arg(const char *str, double *out);

/**
 * Reads a line and returns the corresponding command.
 *
 * @param fd File descriptor to read from.
 * @return Command that was read.
 */
Command get_next_command(int fd);

/**
 * Parses a define command.
 *
 * @param fd File descriptor to read from.
 * @param vm Pointer where the VM type is stored.
 *
 * @return 0 on success
 * @return 1 on error.
 */
int parse_define(int fd, VMType *vm);

/**
 * Parses a reserve command.
 *
 * @param fd File descriptor to read from.
 * @param reservation Pointer where reservation data is stored.
 * @param max_pairs Max number of <VM_TYPE> <COUNT> that is going to be read.
 *
 * @return number of pairs read (0 pairs is returned for any error).
 */
size_t parse_reserve(int fd, Reservation *reservation, size_t max_pairs);

/**
 * Parses an execute command.
 *
 * @param fd File descriptor to read from.
 * @param id String where reservation id being executed is stored.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int parse_execute(int fd, char *id);

/**
 * Parses a wait command.
 *
 * @param fd File descriptor to read from.
 * @param delay Delay that will be applied.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int parse_wait(int fd, unsigned int *delay);

/// Prints an unsigned integer to the given file descriptor.
/// @param fd The file descriptor to write to.
/// @param value The value to write.
/// @return 0 if the integer was written successfully, 1 otherwise.
int print_uint(int fd, unsigned int value);

/// Parses the commands received through the given file descriptor
/// @param fd The file descriptor to read from.
/// @param handler The callback function to handle each command 
/// @param context Contains all the objects that the handler requires to process command
/// @return 0 if the integer was written successfully, 1 otherwise.
int parse_commands(int fd, CommandHandler handler, void * context);


/// Parses a single command from the fd provided
/// @param fd The file descriptor to read from.
/// @param cmd The command parsed will be stored on this variable
void parse_cmd(int fd, ParsedCommand* cmd);

#endif // PARSER__H