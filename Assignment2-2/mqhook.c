
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

/* 시스템 콜 테이블 주소 */
static unsigned long *sys_call_table;

/* 원본 시스템 콜 함수 포인터 */
static asmlinkage long (*original_mq)(
    const struct pt_regs *regs);

/* kallsyms_lookup_name 함수 포인터 */
static unsigned long (*lookup_name)(const char *name);

/*
 * CR0 값을 직접 기록하는 함수
 * Linux 5.4의 WP 보호 동작을 고려한 실습용 코드
 */
static void write_cr0_direct(unsigned long value)
{
    asm volatile("mov %0, %%cr0"
                 :
                 : "r"(value)
                 : "memory");
}

/* 시스템 콜 테이블 항목 변경 */
static void change_syscall(unsigned long new_addr)
{
    unsigned long cr0;
    unsigned long flags;

    preempt_disable();
    local_irq_save(flags);

    cr0 = read_cr0();

    /* WP 비트 해제 */
    write_cr0_direct(cr0 & ~X86_CR0_WP);

    /* 336번 시스템 콜 주소 변경 */
    WRITE_ONCE(sys_call_table[OS_MQ_NR], new_addr);

    /* WP 비트 복원 */
    write_cr0_direct(cr0);

    local_irq_restore(flags);
    preempt_enable();
}

/* cmd 번호를 문자열로 변환 */
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

/* 후킹 함수 */
static asmlinkage long my_mq(const struct pt_regs *regs)
{
    int cmd = (int)regs->di;

    pr_info("[mqhook] os_mq called: cmd=%d (%s), pid=%d\n",
            cmd, get_cmd_name(cmd), current->pid);

    /* 원본 os_mq 실행 */
    return original_mq(regs);
}

/*
 * kallsyms_lookup_name 주소 탐색
 * 직접 참조가 불가능한 환경을 위해 kprobe 활용
 */
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

/* 모듈 적재 */
static int __init mqhook_init(void)
{
    int ret;

    pr_info("[mqhook] module loading\n");

    ret = resolve_kallsyms();
    if (ret < 0) {
        pr_err("[mqhook] kallsyms lookup failed\n");
        return ret;
    }

    /* sys_call_table 주소 검색 */
    sys_call_table =
        (unsigned long *)lookup_name("sys_call_table");

    if (!sys_call_table) {
        pr_err("[mqhook] sys_call_table not found\n");
        return -ENOENT;
    }

    /* 원본 시스템 콜 주소 저장 */
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


static void __exit mqhook_exit(void)
{
    change_syscall((unsigned long)original_mq);

    pr_info("[mqhook] os_mq restored\n");
    pr_info("[mqhook] module unloaded\n");
}

module_init(mqhook_init);
module_exit(mqhook_exit);
