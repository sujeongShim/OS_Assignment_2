
#ifndef _OS_MQ_H
#define _OS_MQ_H

// os_mq commands
#define OS_MQ_OPEN   0
#define OS_MQ_SEND   1
#define OS_MQ_RECV   2
#define OS_MQ_CLOSE  3

// Return EAGAIN instead of waiting when a queue is full or empty.
#define OS_MQ_NONBLOCK  0x01

// Name storage includes the terminator; messages may contain arbitrary bytes.
#define OS_MQ_NAME_MAX  32
#define OS_MQ_MSG_MAX   256

// argument for OS_MQ_OPEN
struct os_mq_open_arg {
    char name[OS_MQ_NAME_MAX];
    int max_msg; // Capacity used when creating a new queue.
};

// argument for OS_MQ_SEND / OS_MQ_RECV
struct os_mq_io_arg {
    int qid; // Queue ID returned by OS_MQ_OPEN.
    void *buf;  // User-space source or destination buffer.
    unsigned int len;   // Send length or receive-buffer capacity in bytes.
    int flags;  // Zero for blocking I/O, or OS_MQ_NONBLOCK.
};

// argument for OS_MQ_CLOSE
struct os_mq_close_arg {
    int qid;
};

#endif