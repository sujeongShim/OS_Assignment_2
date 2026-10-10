/*
 * Shared syscall constants and statistics interface for both modules.
 */

#ifndef MQHOOKING_H
#define MQHOOKING_H

// Command values shared by the syscall hook and statistics monitor.
#define OS_MQ_OPEN   0
#define OS_MQ_SEND   1
#define OS_MQ_RECV   2
#define OS_MQ_CLOSE  3

// Custom syscall number in the target kernel; four commands are counted.
#define OS_MQ_NR 336
#define OS_MQ_CMD_COUNT 4

// Return a command call count, or zero for an invalid command.
unsigned long long mq_get_count(int cmd);

#endif
