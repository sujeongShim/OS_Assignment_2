#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

// Command values and argument layouts must match the kernel ABI.
#define OS_MQ_OPEN   0
#define OS_MQ_SEND   1
#define OS_MQ_RECV   2
#define OS_MQ_CLOSE  3

#define OS_MQ_NONBLOCK 0x01

#define OS_MQ_NAME_MAX 32
#define OS_MQ_MSG_MAX  256

// Must match the custom syscall number in the target Linux kernel.
#define __NR_os_mq 336

struct os_mq_open_arg {
    char name[OS_MQ_NAME_MAX];
    int max_msg;
};

struct os_mq_io_arg {
    int qid;
    void *buf;
    unsigned int len;
    int flags;
};

struct os_mq_close_arg {
    int qid;
};

// Track unique queue IDs for cleanup when the exit command is entered.
static int opened_qids[100];
static int opened_count = 0;

// libc syscall() converts kernel errors to -1 and sets errno.
static long os_mq(int cmd, void *arg)
{
    return syscall(__NR_os_mq, cmd, arg);
}

// Record each ID once, within the fixed tracking-array capacity.
static void add_qid(int qid)
{
    int i;

    for (i = 0; i < opened_count; i++) {
        if (opened_qids[i] == qid)
            return;
    }

    if (opened_count < 100)
        opened_qids[opened_count++] = qid;
}

// Destroy all tracked queues and reset the tracking list.
static void close_all(void)
{
    int i;
    struct os_mq_close_arg arg;

    for (i = 0; i < opened_count; i++) {
        arg.qid = opened_qids[i];

        if (os_mq(OS_MQ_CLOSE, &arg) < 0)
            perror("close");
    }

    opened_count = 0;
}

// Read and dispatch shell commands until exit or end of input.
int main(void)
{
    char line[512];

    printf("mqsh - os_mq interactive shell (type 'help')\n");

    while (1) {
        char *cmd;

        printf("mq> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        // Remove the newline before tokenizing the command.
        line[strcspn(line, "\n")] = '\0';

        cmd = strtok(line, " ");

        if (cmd == NULL)
            continue;

        if (strcmp(cmd, "help") == 0) {
            printf("  open <name> <max_msg>\n");
            printf("  send <qid> <message>\n");
            printf("  recv <qid>\n");
            printf("  nbsend <qid> <message>\n");
            printf("  nbrecv <qid>\n");
            printf("  close <qid>\n");
            printf("  exit\n");
        }

        else if (strcmp(cmd, "open") == 0) {
            char *name = strtok(NULL, " ");
            char *max_str = strtok(NULL, " ");
            struct os_mq_open_arg arg;
            long ret;

            if (name == NULL || max_str == NULL) {
                printf("  usage: open <name> <max_msg>\n");
                continue;
            }

            memset(&arg, 0, sizeof(arg));
            strncpy(arg.name, name, OS_MQ_NAME_MAX - 1);
            arg.max_msg = atoi(max_str);

            ret = os_mq(OS_MQ_OPEN, &arg);

            if (ret < 0) {
                perror("open");
            } else {
                printf("  qid=%ld (name=%s, max_msg=%d)\n",
                       ret, arg.name, arg.max_msg);

                add_qid((int)ret);
            }
        }

        else if (strcmp(cmd, "send") == 0 ||
                 strcmp(cmd, "nbsend") == 0) {
            char *qid_str = strtok(NULL, " ");
            // Keep the remaining input, including spaces, as one message.
            char *message = strtok(NULL, "");
            struct os_mq_io_arg arg;
            long ret;

            if (qid_str == NULL || message == NULL) {
                printf("  usage: %s <qid> <message>\n", cmd);
                continue;
            }

            arg.qid = atoi(qid_str);
            arg.buf = message;
            // Include the null terminator in the transmitted text.
            arg.len = strlen(message) + 1;
            arg.flags = (strcmp(cmd, "nbsend") == 0)
                            ? OS_MQ_NONBLOCK : 0;

            if (strcmp(cmd, "send") == 0)
                printf("  sending... (blocks if full)\n");
            
            fflush(stdout);

            ret = os_mq(OS_MQ_SEND, &arg);

            if (ret < 0) 
            {
                 printf("  send failed (ret=%ld, errno=%d %s)\n", ret, errno, strerror(errno));
            } 
            else 
            {
                printf("  sent \"%s\" (ret=%ld bytes)\n", message, ret);
            }
        }

        else if (strcmp(cmd, "recv") == 0 ||
                 strcmp(cmd, "nbrecv") == 0) {
            char *qid_str = strtok(NULL, " ");
            char buf[OS_MQ_MSG_MAX + 1];
            struct os_mq_io_arg arg;
            long ret;

            if (qid_str == NULL) {
                printf("  usage: %s <qid>\n", cmd);
                continue;
            }

            // Keep an extra zero byte after the receive-buffer capacity.
            memset(buf, 0, sizeof(buf));

            arg.qid = atoi(qid_str);
            arg.buf = buf;
            arg.len = OS_MQ_MSG_MAX;
            arg.flags = (strcmp(cmd, "nbrecv") == 0)
                            ? OS_MQ_NONBLOCK : 0;

            if (strcmp(cmd, "recv") == 0)
                printf("  receiving... (blocks if empty)\n");
            else
                printf("  receiving... (non-blocking)\n");

            fflush(stdout);

            ret = os_mq(OS_MQ_RECV, &arg);

            if (ret < 0) {
                perror(cmd);
            } else {
                if (ret > 0 && ret <= OS_MQ_MSG_MAX)
                    buf[ret - 1] = '\0';

                printf("  received \"%s\" (ret=%ld bytes)\n",
                       buf, ret);
            }
        }

        else if (strcmp(cmd, "close") == 0) 
        {
            char *qid_str = strtok(NULL, " ");
            struct os_mq_close_arg arg;
            long ret;
            int i;

            if (qid_str == NULL) {
                printf("usage: close <qid>\n");
                continue;
            }

            arg.qid = atoi(qid_str);

            ret = os_mq(OS_MQ_CLOSE, &arg);

            if (ret < 0) {
                perror("close");
            } else {
                printf("  closed qid=%d\n", arg.qid);

                
                // Shift remaining IDs to remove the successfully closed queue.
                for (i = 0; i < opened_count; i++) {
                    if (opened_qids[i] == arg.qid) {
                        memmove(&opened_qids[i],
                                &opened_qids[i + 1],
                                (opened_count - i - 1) *
                                sizeof(opened_qids[0]));
                        opened_count--;
                        break;
                    }
                }
            }
        }

        else if (strcmp(cmd, "exit") == 0) 
        {
            close_all();
            break;
        }

        else {
            printf("  unknown command: %s\n", cmd);
        }
    }

    return 0;
}
