/*
 * Linux x86-64 module that hooks os_mq and counts attempts by command.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>
#include <linux/atomic.h>
#include <linux/errno.h>

#include <asm/special_insns.h>
#include <asm/processor-flags.h>

#include "mqhooking.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("os2024402041");
MODULE_DESCRIPTION("os_mq syscall hooking and counting");

// Address of the kernel system call table.
static unsigned long *sys_call_table;

// Saved os_mq handler for forwarding and restoration.
static asmlinkage long (*original_mq)(
    const struct pt_regs *regs);

// Function pointer for resolving kernel symbols by name.
static unsigned long (*lookup_name)(const char *name);

// Atomic counters support updates from concurrent syscall callers.
static atomic64_t mq_counts[OS_MQ_CMD_COUNT];

// Read a command counter; invalid command numbers return zero.
unsigned long long mq_get_count(int cmd)
{
    if (cmd < 0 || cmd >= OS_MQ_CMD_COUNT)
        return 0;

    return (unsigned long long)
        atomic64_read(&mq_counts[cmd]);
}

// Export the accessor for use by the mqmonitor module.
EXPORT_SYMBOL(mq_get_count);

// Return a readable command name for kernel logging.
static const char *get_cmd_name(int cmd)
{
    switch (cmd) {
    case OS_MQ_OPEN:
        return "OPEN";
    case OS_MQ_SEND:
        return "SEND";
    case OS_MQ_RECV:
        return "RECV";
    case OS_MQ_CLOSE:
        return "CLOSE";
    default:
        return "UNKNOWN";
    }
}

// Write CR0 directly to control its write-protect bit.
static void write_cr0_direct(unsigned long value)
{
    asm volatile("mov %0, %%cr0"
                 :
                 : "r"(value)
                 : "memory");
}

// Replace the syscall entry with local interrupts and preemption disabled.
static void change_syscall(unsigned long new_addr)
{
    unsigned long cr0;
    unsigned long flags;

    preempt_disable();
    local_irq_save(flags);

    cr0 = read_cr0();

    // Temporarily disable write protection for the table update.
    write_cr0_direct(cr0 & ~X86_CR0_WP);

    WRITE_ONCE(sys_call_table[OS_MQ_NR], new_addr);

    // Restore the saved CR0 value after updating the table.
    write_cr0_direct(cr0);

    local_irq_restore(flags);
    preempt_enable();
}

// Count and log calls before forwarding them to the original handler.
static asmlinkage long my_mq(const struct pt_regs *regs)
{
    int cmd = (int)regs->di;

    // Count valid command attempts, including calls that later fail or block.
    if (cmd >= 0 && cmd < OS_MQ_CMD_COUNT)
        atomic64_inc(&mq_counts[cmd]);

    // Log the command and PID of the calling process.
    pr_info("[mqhooking] os_mq called: cmd=%d (%s), pid=%d\n",
            cmd, get_cmd_name(cmd), current->pid);

    // Forward unchanged register arguments and return the original result.
    return original_mq(regs);
}

// Resolve kallsyms_lookup_name through a temporary kprobe.
static int resolve_kallsyms(void)
{
    struct kprobe kp = {
        .symbol_name = "kallsyms_lookup_name",
    };
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0)
        return ret;

    lookup_name = (void *)kp.addr;
    unregister_kprobe(&kp);

    return lookup_name ? 0 : -ENOENT;
}

// Initialize counters, resolve symbols, and install the hook.
static int __init mqhooking_init(void)
{
    int ret;
    int i;

    pr_info("[mqhooking] module loading\n");

    // Reset all command counters before installing the hook.
    for (i = 0; i < OS_MQ_CMD_COUNT; i++)
        atomic64_set(&mq_counts[i], 0);

    // Obtain the kernel symbol lookup function address.
    ret = resolve_kallsyms();
    if (ret < 0) {
        pr_err("[mqhooking] kallsyms lookup failed\n");
        return ret;
    }

    // Locate the kernel system call table.
    sys_call_table =
        (unsigned long *)lookup_name("sys_call_table");

    if (!sys_call_table) {
        pr_err("[mqhooking] sys_call_table not found\n");
        return -ENOENT;
    }

    // Save the existing handler for forwarding and restoration.
    original_mq =
        (void *)READ_ONCE(sys_call_table[OS_MQ_NR]);

    if (!original_mq) {
        pr_err("[mqhooking] original os_mq not found\n");
        return -EINVAL;
    }

    // Install the wrapper that records command usage.
    change_syscall((unsigned long)my_mq);

    pr_info("[mqhooking] os_mq hooked (nr=%d)\n",
            OS_MQ_NR);

    return 0;
}

// Remove the hook during module unloading.
static void __exit mqhooking_exit(void)
{
    // Restore the handler saved when this module was loaded.
    change_syscall((unsigned long)original_mq);

    pr_info("[mqhooking] os_mq restored\n");
    pr_info("[mqhooking] module unloaded\n");
}

module_init(mqhooking_init);
module_exit(mqhooking_exit);
