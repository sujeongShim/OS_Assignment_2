/*
 * Companion module that prints mqhooking counters on unload.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "mqhooking.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("os2024402041");
MODULE_DESCRIPTION("os_mq usage statistics monitor");

// Announce loading; statistics are printed when the monitor is unloaded.
static int __init mqmonitor_init(void)
{
    pr_info("[mqmonitor] module loaded\n");
    return 0;
}

// Read command counters and print the usage summary during module removal.
static void __exit mqmonitor_exit(void)
{
    unsigned long long open_count;
    unsigned long long send_count;
    unsigned long long recv_count;
    unsigned long long close_count;
    unsigned long long total;

    // Read counters separately; concurrent calls may occur between these reads.
    open_count  = mq_get_count(OS_MQ_OPEN);
    send_count  = mq_get_count(OS_MQ_SEND);
    recv_count  = mq_get_count(OS_MQ_RECV);
    close_count = mq_get_count(OS_MQ_CLOSE);

    total = open_count + send_count +
            recv_count + close_count;

    pr_info("===== os_mq usage summary =====\n");

    pr_info("2024402041 OPEN:%llu SEND:%llu RECV:%llu CLOSE:%llu\n",
            open_count, send_count,
            recv_count, close_count);

    pr_info("2024402041 total : %llu calls\n", total);

    pr_info("===============================\n");
}

module_init(mqmonitor_init);
module_exit(mqmonitor_exit);
