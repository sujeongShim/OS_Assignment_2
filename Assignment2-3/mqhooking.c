
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

/* 시스템 콜 테이블 */
static unsigned long *sys_call_table;

/* 원본 os_mq 시스템 콜 */
static asmlinkage long (*original_mq)(
    const struct pt_regs *regs);

/* kallsyms_lookup_name 함수 포인터 */
static unsigned long (*lookup_name)(const char *name);

/* 명령별 호출 횟수 */
static atomic64_t mq_counts[OS_MQ_CMD_COUNT];

/* 통계 조회 함수 */
unsigned long long mq_get_count(int cmd)
{
    if (cmd < 0 || cmd >= OS_MQ_CMD_COUNT)
        return 0;

    return (unsigned long long)
        atomic64_read(&mq_counts[cmd]);
}

/* 다른 모듈에서 mq_get_count 사용 가능 */
EXPORT_SYMBOL(mq_get_count);

/* 명령어 이름 반환 */
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

/* CR0 레지스터 직접 수정 */
static void write_cr0_direct(unsigned long value)
{
    asm volatile("mov %0, %%cr0"
                 :
                 : "r"(value)
                 : "memory");
}

/* 시스템 콜 테이블 항목 교체 */
static void change_syscall(unsigned long new_addr)
{
    unsigned long cr0;
    unsigned long flags;

    preempt_disable();
    local_irq_save(flags);

    cr0 = read_cr0();

    /* 쓰기 보호(WP) 일시 해제 */
    write_cr0_direct(cr0 & ~X86_CR0_WP);

    WRITE_ONCE(sys_call_table[OS_MQ_NR], new_addr);

    /* WP 원상복구 */
    write_cr0_direct(cr0);

    local_irq_restore(flags);
    preempt_enable();
}

/* 후킹 함수 */
static asmlinkage long my_mq(const struct pt_regs *regs)
{
    int cmd = (int)regs->di;

    /* 유효한 명령어의 호출 횟수 증가 */
    if (cmd >= 0 && cmd < OS_MQ_CMD_COUNT)
        atomic64_inc(&mq_counts[cmd]);

    /* 호출 정보 기록 */
    pr_info("[mqhooking] os_mq called: cmd=%d (%s), pid=%d\n",
            cmd, get_cmd_name(cmd), current->pid);

    /* 원본 시스템 콜 실행 */
    return original_mq(regs);
}

/* kallsyms_lookup_name 주소 탐색 */
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

/* 모듈 초기화 */
static int __init mqhooking_init(void)
{
    int ret;
    int i;

    pr_info("[mqhooking] module loading\n");

    /* 호출 카운터 초기화 */
    for (i = 0; i < OS_MQ_CMD_COUNT; i++)
        atomic64_set(&mq_counts[i], 0);

    /* kallsyms 주소 탐색 */
    ret = resolve_kallsyms();
    if (ret < 0) {
        pr_err("[mqhooking] kallsyms lookup failed\n");
        return ret;
    }

    /* 시스템 콜 테이블 검색 */
    sys_call_table =
        (unsigned long *)lookup_name("sys_call_table");

    if (!sys_call_table) {
        pr_err("[mqhooking] sys_call_table not found\n");
        return -ENOENT;
    }

    /* 원본 시스템 콜 백업 */
    original_mq =
        (void *)READ_ONCE(sys_call_table[OS_MQ_NR]);

    if (!original_mq) {
        pr_err("[mqhooking] original os_mq not found\n");
        return -EINVAL;
    }

    /* 후킹 함수로 교체 */
    change_syscall((unsigned long)my_mq);

    pr_info("[mqhooking] os_mq hooked (nr=%d)\n",
            OS_MQ_NR);

    return 0;
}

/* 모듈 제거 */
static void __exit mqhooking_exit(void)
{
    /* 원본 시스템 콜 복원 */
    change_syscall((unsigned long)original_mq);

    pr_info("[mqhooking] os_mq restored\n");
    pr_info("[mqhooking] module unloaded\n");
}

module_init(mqhooking_init);
module_exit(mqhooking_exit);
