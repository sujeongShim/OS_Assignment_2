/*
 * Linux x86-64 module that logs calls to the custom os_mq syscall.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>

#include <asm/special_insns.h>
#include <asm/processor-flags.h>

#define OS_MQ_NR 336

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Assignment 2-2");
MODULE_DESCRIPTION("os_mq system call hooking module");

// Address of the kernel system call table.
static unsigned long *sys_call_table;

// Saved os_mq handler using the x86-64 register-based syscall ABI.
static asmlinkage long (*original_mq)(
    const struct pt_regs *regs);

// Function pointer for resolving kernel symbols by name.
static unsigned long (*lookup_name)(const char *name);

// Write CR0 directly with inline assembly to control its write-protect bit.
static void write_cr0_direct(unsigned long value)
{
    asm volatile("mov %0, %%cr0"
                 :
                 : "r"(value)
                 : "memory");
}

// Replace the os_mq table entry with local interrupts and preemption disabled.
static void change_syscall(unsigned long new_addr)
{
    unsigned long cr0;
    unsigned long flags;

    preempt_disable();
    local_irq_save(flags);

    cr0 = read_cr0();

    // Temporarily clear write protection for the table update.
    write_cr0_direct(cr0 & ~X86_CR0_WP);

    // Install the replacement at the custom syscall number.
    WRITE_ONCE(sys_call_table[OS_MQ_NR], new_addr);

    // Restore the original CR0 value, including write protection.
    write_cr0_direct(cr0);

    local_irq_restore(flags);
    preempt_enable();
}

// Map a command number to a readable name for logging.
static const char *get_cmd_name(int cmd)
{
    switch (cmd) {
    case 0:
        return "OPEN";
    case 1:
        return "SEND";
    case 2:
        return "RECV";
    case 3:
        return "CLOSE";
    default:
        return "UNKNOWN";
    }
}

// Log the command and caller PID, then forward the syscall.
static asmlinkage long my_mq(const struct pt_regs *regs)
{
    int cmd = (int)regs->di;

    pr_info("[mqhook] os_mq called: cmd=%d (%s), pid=%d\n",
            cmd, get_cmd_name(cmd), current->pid);

    // Preserve the original arguments and return value.
    return original_mq(regs);
}

// Use a temporary kprobe to obtain the address of kallsyms_lookup_name.
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

// Resolve symbols, save the original handler, and install the hook.
static int __init mqhook_init(void)
{
    int ret;

    pr_info("[mqhook] module loading\n");

    ret = resolve_kallsyms();
    if (ret < 0) {
        pr_err("[mqhook] kallsyms lookup failed\n");
        return ret;
    }

    // Locate the system call table by its kernel symbol name.
    sys_call_table =
        (unsigned long *)lookup_name("sys_call_table");

    if (!sys_call_table) {
        pr_err("[mqhook] sys_call_table not found\n");
        return -ENOENT;
    }

    // Save the original handler for forwarding and restoration.
    original_mq =
        (void *)READ_ONCE(sys_call_table[OS_MQ_NR]);

    if (!original_mq) {
        pr_err("[mqhook] original os_mq not found\n");
        return -EINVAL;
    }

   
    change_syscall((unsigned long)my_mq);

    pr_info("[mqhook] os_mq hooked (nr=%d)\n", OS_MQ_NR);

    return 0;
}


// Restore the saved handler before the module is unloaded.
static void __exit mqhook_exit(void)
{
    change_syscall((unsigned long)original_mq);

    pr_info("[mqhook] os_mq restored\n");
    pr_info("[mqhook] module unloaded\n");
}

module_init(mqhook_init);
module_exit(mqhook_exit);
