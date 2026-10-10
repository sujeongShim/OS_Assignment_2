#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include "os_mq.h"

/* Maximum number of queues */
#define OS_MQ_MAX_QUEUES 16

/* One message stored in a queue */
struct os_mq_message {
    char data[OS_MQ_MSG_MAX];
    unsigned int len;
};

/* One message queue */
struct os_mq_queue {
    int used;
    int qid;

    char name[OS_MQ_NAME_MAX];
    int max_msg;

    struct os_mq_message *messages;

    int head;
    int tail;
    int count;

    struct mutex lock;

    wait_queue_head_t not_empty;
    wait_queue_head_t not_full;
};

/* All message queues managed by the kernel */
static struct os_mq_queue queues[OS_MQ_MAX_QUEUES];

/* Protects creation and deletion of queues */
static DEFINE_MUTEX(queues_lock);


SYSCALL_DEFINE2(os_mq, int, cmd, void __user *, arg)
{
    int i;

    switch (cmd) {
    case OS_MQ_OPEN:
    {
        struct os_mq_open_arg open_arg;
        struct os_mq_message *messages;

        /* Copy arguments from user space */
        if (copy_from_user(&open_arg, arg, sizeof(open_arg)))
            return -EFAULT;

        /* Check arguments */
        if (open_arg.max_msg <= 0)
            return -EINVAL;

        open_arg.name[OS_MQ_NAME_MAX - 1] = '\0';

        mutex_lock(&queues_lock);

        /* If a queue with the same name exists, return its qid */
        for (i = 0; i < OS_MQ_MAX_QUEUES; i++) {
            if (queues[i].used &&
                strcmp(queues[i].name, open_arg.name) == 0) {

                mutex_unlock(&queues_lock);
                return queues[i].qid;
            }
        }

        /* Find an unused queue slot */
        for (i = 0; i < OS_MQ_MAX_QUEUES; i++) {
            if (!queues[i].used)
                break;
        }

        if (i == OS_MQ_MAX_QUEUES) {
            mutex_unlock(&queues_lock);
            return -ENOSPC;
        }

        /* Allocate message storage */
        messages = kcalloc(open_arg.max_msg,
                           sizeof(struct os_mq_message),
                           GFP_KERNEL);

        if (!messages) {
            mutex_unlock(&queues_lock);
            return -ENOMEM;
        }

        /* Initialize new queue */
        queues[i].used = 1;
        queues[i].qid = i;

        strncpy(queues[i].name,
                open_arg.name,
                OS_MQ_NAME_MAX);

        queues[i].name[OS_MQ_NAME_MAX - 1] = '\0';

        queues[i].max_msg = open_arg.max_msg;
        queues[i].messages = messages;

        queues[i].head = 0;
        queues[i].tail = 0;
        queues[i].count = 0;

        mutex_init(&queues[i].lock);
        init_waitqueue_head(&queues[i].not_empty);
        init_waitqueue_head(&queues[i].not_full);

        mutex_unlock(&queues_lock);

        return queues[i].qid;
    }

        case OS_MQ_SEND:
    {
        struct os_mq_io_arg io_arg;
        struct os_mq_queue *q;
        unsigned int len;
        int ret;

        /* Copy arguments from user space */
        if (copy_from_user(&io_arg, arg, sizeof(io_arg)))
            return -EFAULT;

        /* Check qid */
        if (io_arg.qid < 0 || io_arg.qid >= OS_MQ_MAX_QUEUES)
            return -EINVAL;

        q = &queues[io_arg.qid];

        /* Check whether the queue exists */
        if (!q->used)
            return -EINVAL;

        /* Check message length */
        if (io_arg.len == 0 || io_arg.len > OS_MQ_MSG_MAX)
            return -EINVAL;

        len = io_arg.len;

        mutex_lock(&q->lock);

        /* Wait while the queue is full */
        while (q->count >= q->max_msg) {

            /* NONBLOCK mode */
            if (io_arg.flags & OS_MQ_NONBLOCK) {
                mutex_unlock(&q->lock);
                return -EAGAIN;
            }

            mutex_unlock(&q->lock);

            ret = wait_event_interruptible(
                q->not_full,
                q->count < q->max_msg
            );

            /* Interrupted by a signal */
            if (ret)
                return ret;

            mutex_lock(&q->lock);
        }

        /* Copy message from user space */
        if (copy_from_user(q->messages[q->tail].data,
                           io_arg.buf,
                           len)) {
            mutex_unlock(&q->lock);
            return -EFAULT;
        }

        q->messages[q->tail].len = len;

        /* Move tail to the next position */
        q->tail = (q->tail + 1) % q->max_msg;
        q->count++;

        mutex_unlock(&q->lock);

        /* Wake up a receiver waiting for a message */
        wake_up_interruptible(&q->not_empty);

        return len;
    }

    case OS_MQ_RECV:
{
    struct os_mq_io_arg io_arg;
    struct os_mq_queue *q;
    unsigned int len;
    int ret;

    /* Copy arguments from user space */
    if (copy_from_user(&io_arg, arg, sizeof(io_arg)))
        return -EFAULT;

    /* Check qid */
    if (io_arg.qid < 0 || io_arg.qid >= OS_MQ_MAX_QUEUES)
        return -EINVAL;

    q = &queues[io_arg.qid];

    /* Check whether the queue exists */
    if (!q->used)
        return -EINVAL;

    mutex_lock(&q->lock);

    /* Wait while the queue is empty */
    while (q->count == 0) {

        /* NONBLOCK mode */
        if (io_arg.flags & OS_MQ_NONBLOCK) {
            mutex_unlock(&q->lock);
            return -EAGAIN;
        }

        mutex_unlock(&q->lock);

        ret = wait_event_interruptible(
            q->not_empty,
            q->count > 0
        );

        /* Interrupted by a signal */
        if (ret)
            return ret;

        mutex_lock(&q->lock);
    }

    /* Length of the oldest message */
    len = q->messages[q->head].len;

    /*
     * User buffer must be large enough to receive
     * the entire message.
     */
    if (io_arg.len < len) {
        mutex_unlock(&q->lock);
        return -EINVAL;
    }

    /* Copy message to user space */
    if (copy_to_user(io_arg.buf,
                     q->messages[q->head].data,
                     len)) {
        mutex_unlock(&q->lock);
        return -EFAULT;
    }

    /* Remove the message from the queue */
    q->head = (q->head + 1) % q->max_msg;
    q->count--;

    mutex_unlock(&q->lock);

    /* Wake up a sender waiting for free space */
    wake_up_interruptible(&q->not_full);

    return len;
}


    case OS_MQ_CLOSE:
{
    struct os_mq_close_arg close_arg;
    struct os_mq_queue *q;

    /* Copy arguments from user space */
    if (copy_from_user(&close_arg, arg, sizeof(close_arg)))
        return -EFAULT;

    /* Check qid */
    if (close_arg.qid < 0 ||
        close_arg.qid >= OS_MQ_MAX_QUEUES)
        return -EINVAL;

    mutex_lock(&queues_lock);

    q = &queues[close_arg.qid];

    /* Check whether the queue exists */
    if (!q->used) {
        mutex_unlock(&queues_lock);
        return -EINVAL;
    }

    mutex_lock(&q->lock);

    /* Free dynamically allocated message storage */
    kfree(q->messages);

    q->messages = NULL;
    q->used = 0;
    q->qid = -1;
    q->name[0] = '\0';
    q->max_msg = 0;
    q->head = 0;
    q->tail = 0;
    q->count = 0;

    mutex_unlock(&q->lock);
    mutex_unlock(&queues_lock);

    return 0;
}

    default:
        return -EINVAL;
    }
}