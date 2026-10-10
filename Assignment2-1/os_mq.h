#ifndef _OS_MQ_H
#define _OS_MQ_H

/* os_mq commands */
#define OS_MQ_OPEN   0
#define OS_MQ_SEND   1
#define OS_MQ_RECV   2
#define OS_MQ_CLOSE  3

/* flags */
#define OS_MQ_NONBLOCK  0x01

/* limits */
#define OS_MQ_NAME_MAX  32
#define OS_MQ_MSG_MAX   256

/* argument for OS_MQ_OPEN */
struct os_mq_open_arg {
    char name[OS_MQ_NAME_MAX];
    int max_msg;
};

/* argument for OS_MQ_SEND / OS_MQ_RECV */
struct os_mq_io_arg {
    int qid; //what queue?
    void *buf;  //where is message data?
    unsigned int len;   //data size
    int flags;  //blocking
};

/* argument for OS_MQ_CLOSE */
struct os_mq_close_arg {
    int qid;
};

#endif