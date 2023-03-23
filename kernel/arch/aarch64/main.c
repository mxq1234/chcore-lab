/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v1. You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v1 for more details.
 */

#include <sched/sched.h>
#include <common/kprint.h>
#include <common/vars.h>
#include <common/macro.h>
#include <common/types.h>
#include <common/lock.h>
#include <arch/machine/smp.h>
#include <arch/mm/page_table.h>
#include <mm/mm.h>
#include <io/uart.h>
#include <machine.h>
#include <irq/irq.h>
#include <object/thread.h>
#include <arch/mmu.h>

ALIGN(STACK_ALIGNMENT)
char kernel_stack[PLAT_CPU_NUM][KERNEL_STACK_SIZE];

#ifdef CHCORE_KERNEL_TEST
#include <lab.h>
static void lab2_test_kernel_vaddr(void)
{
        u64 pc;
        asm volatile("adr %0, ." : "=r"(pc));
        lab_check(pc >= KBASE, "Jump to kernel high memory");
}

#include <mm/kmalloc.h>
#include <arch/mm/page_table.h>
#endif /* CHCORE_KERNEL_TEST */

/*
 * @boot_flag is the physical address of boot flag;
 */
void main(paddr_t boot_flag)
{
        u32 ret = 0;

        /* Init uart: no need to init the uart again */
        uart_init();
        kinfo("[ChCore] uart init finished\n");

#ifdef CHCORE_KERNEL_TEST
        lab2_test_kernel_vaddr();
#endif /* CHCORE_KERNEL_TEST */

        /* Init mm */
        mm_init();
        kinfo("[ChCore] mm init finished\n");

        reset_el1_page_table();

#ifdef CHCORE_KERNEL_TEST
        void lab2_test_kmalloc(void);
        lab2_test_kmalloc();
        void lab2_test_page_table(void);
        lab2_test_page_table();
#endif /* CHCORE_KERNEL_TEST */

        /* Init exception vector */
        arch_interrupt_init();
        kinfo("[ChCore] interrupt init finished\n");

        create_root_thread();
        kinfo("[ChCore] create initial thread done on %d\n", smp_get_cpu_id());

        /* Context switch to the picked thread */
        eret_to_thread(switch_context());

        /* Should provide panic and use here */
        BUG("[FATAL] Should never be here!\n");
}
