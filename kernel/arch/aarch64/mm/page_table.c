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

#include <common/util.h>
#include <common/vars.h>
#include <common/macro.h>
#include <common/types.h>
#include <common/errno.h>
#include <lib/printk.h>
#include <mm/kmalloc.h>
#include <mm/mm.h>
#include <arch/mmu.h>

#include <arch/mm/page_table.h>

extern void set_ttbr0_el1(paddr_t);
extern void set_ttbr1_el1(paddr_t);

void set_page_table(paddr_t pgtbl)
{
        set_ttbr0_el1(pgtbl);
}

#define USER_PTE 0
#define KERNEL_PTE 1

/*
 * Reconfigure the el1 page table after starting kernel.
 * Mapping with 4K pages.
 */

void reset_el1_page_table() {
/* Physical memory address space: 0-1G */
#define PHYSMEM_START   (0x0UL)
#define PERIPHERAL_BASE (0x3F000000UL)
#define PHYSMEM_END     (0x40000000UL)
#define PHYSMEM_MAX     (0xFFFFFFFFUL)

/* The number of entries in one page table page */
#define PTP_ENTRIES_NUM 512

        void* pgtbl = get_pages(0);
        memset(pgtbl, 0, sizeof(u64) * PTP_ENTRIES_NUM);
        map_range_in_pgtbl(pgtbl, KBASE, PHYSMEM_START, PERIPHERAL_BASE - PHYSMEM_START, 0);
        map_range_in_pgtbl(pgtbl, KBASE + PERIPHERAL_BASE, PERIPHERAL_BASE, PHYSMEM_END - PERIPHERAL_BASE, VMR_DEVICE);
        map_range_in_pgtbl(pgtbl, KBASE + PHYSMEM_END, PHYSMEM_END, PHYSMEM_MAX - PHYSMEM_END, VMR_DEVICE);
        set_ttbr1_el1(virt_to_phys(pgtbl));
}

/*
 * the 3rd arg means the kind of PTE.
 */
static int set_pte_flags(pte_t *entry, vmr_prop_t flags, int kind)
{
        // Only consider USER PTE now.
        // BUG_ON(kind != USER_PTE);

        if(kind == USER_PTE) {
                /*
                * Current access permission (AP) setting:
                * Mapped pages are always readable (No considering XOM).
                * EL1 can directly access EL0 (No restriction like SMAP
                * as ChCore is a microkernel).
                */
                if (flags & VMR_WRITE)
                        entry->l3_page.AP = AARCH64_MMU_ATTR_PAGE_AP_HIGH_RW_EL0_RW;
                else
                        entry->l3_page.AP = AARCH64_MMU_ATTR_PAGE_AP_HIGH_RO_EL0_RO;

                if (flags & VMR_EXEC)
                        entry->l3_page.UXN = AARCH64_MMU_ATTR_PAGE_UX;
                else
                        entry->l3_page.UXN = AARCH64_MMU_ATTR_PAGE_UXN;

                // EL1 cannot directly execute EL0 accessiable region.
                entry->l3_page.PXN = AARCH64_MMU_ATTR_PAGE_PXN;
                // Set AF (access flag) in advance.
                entry->l3_page.AF = AARCH64_MMU_ATTR_PAGE_AF_ACCESSED;
                // Mark the mapping as not global
                entry->l3_page.nG = 1;
                // Mark the mappint as inner sharable
                entry->l3_page.SH = INNER_SHAREABLE;
                // Set the memory type
                if (flags & VMR_DEVICE) {
                        entry->l3_page.attr_index = DEVICE_MEMORY;
                        entry->l3_page.SH = 0;
                } else if (flags & VMR_NOCACHE) {
                        entry->l3_page.attr_index = NORMAL_MEMORY_NOCACHE;
                } else {
                        entry->l3_page.attr_index = NORMAL_MEMORY;
                }
        } else if(kind == KERNEL_PTE) {
                // EL1 can r/w kernel region and EL0 can do nothing.
                entry->l3_page.AP = AARCH64_MMU_ATTR_PAGE_AP_HIGH_RW_EL0_NONE;
                // EL0 cannot directly execute EL1 accessiable region.
                entry->l3_page.UXN = AARCH64_MMU_ATTR_PAGE_UXN;
                // EL1 can directly execute EL1 accessiable region.
                entry->l3_page.PXN = AARCH64_MMU_ATTR_PAGE_PX;
                // Set AF (access flag) in advance.
                entry->l3_page.AF = AARCH64_MMU_ATTR_PAGE_AF_ACCESSED;
                // Mark the mapping as not global
                entry->l3_page.nG = 1;
                // Mark the mappint as inner sharable
                entry->l3_page.SH = INNER_SHAREABLE;
                // Set the memory type
                if (flags & VMR_DEVICE) {
                        entry->l3_page.attr_index = DEVICE_MEMORY;
                        entry->l3_page.SH = 0;
                } else if (flags & VMR_NOCACHE) {
                        entry->l3_page.attr_index = NORMAL_MEMORY_NOCACHE;
                } else {
                        entry->l3_page.attr_index = NORMAL_MEMORY;
                }
        } else {
                BUG("The PTE kind %d is not supported\n", kind);
        }

        return 0;
}

#define GET_PADDR_IN_PTE(entry) \
        (((u64)entry->table.next_table_addr) << PAGE_SHIFT)
#define GET_NEXT_PTP(entry) phys_to_virt(GET_PADDR_IN_PTE(entry))

#define NORMAL_PTP (0)
#define BLOCK_PTP  (1)

/*
 * Find next page table page for the "va".
 *
 * cur_ptp: current page table page
 * level:   current ptp level
 *
 * next_ptp: returns "next_ptp"
 * pte     : returns "pte" (points to next_ptp) in "cur_ptp"
 *
 * alloc: if true, allocate a ptp when missing
 *
 */
static int get_next_ptp(ptp_t *cur_ptp, u32 level, vaddr_t va, ptp_t **next_ptp,
                        pte_t **pte, bool alloc)
{
        u32 index = 0;
        pte_t *entry;

        if (cur_ptp == NULL)
                return -ENOMAPPING;

        switch (level) {
        case 0:
                index = GET_L0_INDEX(va);
                break;
        case 1:
                index = GET_L1_INDEX(va);
                break;
        case 2:
                index = GET_L2_INDEX(va);
                break;
        case 3:
                index = GET_L3_INDEX(va);
                break;
        default:
                BUG_ON(1);
        }

        entry = &(cur_ptp->ent[index]);
        if (IS_PTE_INVALID(entry->pte)) {
                if (alloc == false) {
                        return -ENOMAPPING;
                } else {
                        /* alloc a new page table page */
                        ptp_t *new_ptp;
                        paddr_t new_ptp_paddr;
                        pte_t new_pte_val;

                        /* alloc a single physical page as a new page table page  */
                        /* LAB 2 TODO 3 BEGIN 
                         * Hint: use get_pages to allocate a new page table page
                         *       set the attr `is_valid`, `is_table` and `next_table_addr` of new pte
                         */
                        new_ptp = get_pages(0);
                        new_pte_val.pte = 0;
                        if(new_ptp != NULL) {
                                memset(new_ptp, 0, sizeof(ptp_t));
                                new_ptp_paddr = virt_to_phys(new_ptp);
                                new_pte_val.table.is_valid = 1;
                                new_pte_val.table.is_table = 1;
                                new_pte_val.table.next_table_addr = (new_ptp_paddr >> PAGE_SHIFT);
                                *entry = new_pte_val;
                        } else {
                                return -ENOMEM;
                        }
                        /* LAB 2 TODO 3 END */
                }
        }

        *next_ptp = (ptp_t *)GET_NEXT_PTP(entry);
        *pte = entry;
        if (IS_PTE_TABLE(entry->pte))
                return NORMAL_PTP;
        else
                return BLOCK_PTP;
}

void free_page_table(void *pgtbl)
{
        ptp_t *l0_ptp, *l1_ptp, *l2_ptp, *l3_ptp;
        pte_t *l0_pte, *l1_pte, *l2_pte;
        int i, j, k;

        if (pgtbl == NULL) {
                kwarn("%s: input arg is NULL.\n", __func__);
                return;
        }

        /* L0 page table */
        l0_ptp = (ptp_t *)pgtbl;

        /* Interate each entry in the l0 page table*/
        for (i = 0; i < PTP_ENTRIES; ++i) {
                l0_pte = &l0_ptp->ent[i];
                if (IS_PTE_INVALID(l0_pte->pte) || !IS_PTE_TABLE(l0_pte->pte))
                        continue;
                l1_ptp = (ptp_t *)GET_NEXT_PTP(l0_pte);

                /* Interate each entry in the l1 page table*/
                for (j = 0; j < PTP_ENTRIES; ++j) {
                        l1_pte = &l1_ptp->ent[j];
                        if (IS_PTE_INVALID(l1_pte->pte)
                            || !IS_PTE_TABLE(l1_pte->pte))
                                continue;
                        l2_ptp = (ptp_t *)GET_NEXT_PTP(l1_pte);

                        /* Interate each entry in the l2 page table*/
                        for (k = 0; k < PTP_ENTRIES; ++k) {
                                l2_pte = &l2_ptp->ent[k];
                                if (IS_PTE_INVALID(l2_pte->pte)
                                    || !IS_PTE_TABLE(l2_pte->pte))
                                        continue;
                                l3_ptp = (ptp_t *)GET_NEXT_PTP(l2_pte);
                                /* Free the l3 page table page */
                                free_pages(l3_ptp);
                        }

                        /* Free the l2 page table page */
                        free_pages(l2_ptp);
                }

                /* Free the l1 page table page */
                free_pages(l1_ptp);
        }

        free_pages(l0_ptp);
}

/*
 * Translate a va to pa, and get its pte for the flags
 */
int query_in_pgtbl(void *pgtbl, vaddr_t va, paddr_t *pa, pte_t **entry)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * return the pa and pte until a L0/L1 block or page, return
         * `-ENOMAPPING` if the va is not mapped.
         */
        ptp_t *ptp_l1, *ptp_l2, *ptp_l3, *ptp;
        int retval;

        retval = get_next_ptp(pgtbl, 0, va, &ptp_l1, entry, false);
        if(retval < 0) {
                return retval;
        }

        retval = get_next_ptp(ptp_l1, 1, va, &ptp_l2, entry, false);
        if(retval < 0) {
                return retval;
        }

        if(retval == BLOCK_PTP) {
                *pa = virt_to_phys(ptp_l2) + GET_VA_OFFSET_L1(va);
                return 0;
        }

        retval = get_next_ptp(ptp_l2, 2, va, &ptp_l3, entry, false);
        if(retval < 0) {
                return retval;
        }

        if(retval == BLOCK_PTP) {
                *pa = virt_to_phys(ptp_l3) + GET_VA_OFFSET_L2(va);
                return 0;
        }

        retval = get_next_ptp(ptp_l3, 3, va, &ptp, entry, false);
        if(retval < 0) {
                return retval;
        }

        *pa = virt_to_phys((ptp)) + GET_VA_OFFSET_L3(va);
        return 0;
        /* LAB 2 TODO 3 END */
}

int map_range_in_pgtbl(void *pgtbl, vaddr_t va, paddr_t pa, size_t len,
                       vmr_prop_t flags)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * create new page table page if necessary, fill in the final level
         * pte with the help of `set_pte_flags`. Iterate until all pages are
         * mapped.
         */
        ptp_t *ptpl1, *ptpl2, *ptpl3;
        pte_t *ptel0, *ptel1, *ptel2, *ptel3;
        int retval, cursor, index;

        retval = get_next_ptp(pgtbl, 0, va, &ptpl1, &ptel0, true);
        if(retval < 0) {
                return retval;
        }

        retval = get_next_ptp(ptpl1, 1, va, &ptpl2, &ptel1, true);
        if(retval < 0) {
                return retval;
        }

        retval = get_next_ptp(ptpl2, 2, va, &ptpl3, &ptel2, true);
        if(retval < 0) {
                return retval;
        }

        for (cursor = 0; cursor < len; cursor += PAGE_SIZE) {
                index = GET_L3_INDEX((va + cursor));
                ptel3 = &(ptpl3->ent[index]);

                if(!IS_PTE_INVALID(ptel3->pte)) {
                        // kwarn("The area %d between %d and %d has been mapped", va + cursor, va, va + len);
                }

                set_pte_flags(ptel3, flags, ((va + cursor < KBASE)? USER_PTE : KERNEL_PTE));
                ptel3->l3_page.is_valid = 1;
                ptel3->l3_page.is_page = 1;
                ptel3->l3_page.pfn = ((pa + cursor) >> L3_INDEX_SHIFT);

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L2_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L1_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        index = GET_L0_INDEX((va + cursor));
                                        if(index == PTP_ENTRIES - 1) {
                                                // kinfo("The mapping exceeds the address space!");
                                                return -ENOMEM;
                                        }
                                        retval = get_next_ptp(pgtbl, 0, va + cursor + PAGE_SIZE, &ptpl1, &ptel0, true);
                                        if(retval < 0) {
                                                return retval;
                                        }
                                }
                                retval = get_next_ptp(ptpl1, 1, va + cursor + PAGE_SIZE, &ptpl2, &ptel1, true);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl2, 2, va + cursor + PAGE_SIZE, &ptpl3, &ptel2, true);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        return 0;
        /* LAB 2 TODO 3 END */
}

int unmap_range_in_pgtbl(void *pgtbl, vaddr_t va, size_t len)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * mark the final level pte as invalid. Iterate until all pages are
         * unmapped.
         */
        ptp_t *ptpl1, *ptpl2, *ptpl3;
        pte_t *ptel0, *ptel1, *ptel2, *ptel3;
        int retval, cursor, index;

        retval = get_next_ptp(pgtbl, 0, va, &ptpl1, &ptel0, false);
        if(retval < 0) {
                return retval;
        }

        retval = get_next_ptp(ptpl1, 1, va, &ptpl2, &ptel1, false);
        if(retval < 0) {
                return retval;
        }

        retval = get_next_ptp(ptpl2, 2, va, &ptpl3, &ptel2, false);
        if(retval < 0) {
                return retval;
        }

        for (cursor = 0; cursor < len; cursor += PAGE_SIZE) {
                index = GET_L3_INDEX((va + cursor));
                ptel3 = &(ptpl3->ent[index]);
                ptel3->l3_page.is_valid = 0;

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L2_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L1_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        index = GET_L0_INDEX((va + cursor));
                                        if(index == PTP_ENTRIES - 1) {
                                                kinfo("The unmapping exceeds the address space!");
                                                return 0;
                                        }
                                        retval = get_next_ptp(pgtbl, 0, va + cursor + PAGE_SIZE, &ptpl1, &ptel0, false);
                                        if(retval < 0) {
                                                return retval;
                                        }
                                }
                                retval = get_next_ptp(ptpl1, 1, va + cursor + PAGE_SIZE, &ptpl2, &ptel1, false);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl2, 2, va + cursor + PAGE_SIZE, &ptpl3, &ptel2, false);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        return 0;
        /* LAB 2 TODO 3 END */
}

int map_range_in_pgtbl_huge(void *pgtbl, vaddr_t va, paddr_t pa, size_t len,
                            vmr_prop_t flags)
{
        /* LAB 2 TODO 4 BEGIN */
#define L1_PAGESIZE (1 << (L1_INDEX_SHIFT))
#define L2_PAGESIZE (1 << (L2_INDEX_SHIFT))

        ptp_t *ptpl1, *ptpl2, *ptpl3;
        pte_t *ptel0, *ptel1, *ptel2, *ptel3;
        int retval, cursor, index;

        retval = get_next_ptp(pgtbl, 0, va, &ptpl1, &ptel0, true);
        if(retval < 0) {
                return retval;
        }

        for(cursor = 0; cursor + L1_PAGESIZE <= len; cursor += L1_PAGESIZE) {
                index = GET_L1_INDEX((va + cursor));
                ptel1 = &(ptpl1->ent[index]);

                if(!IS_PTE_INVALID(ptel1->pte)) {
                        // kwarn("The area %d between %d and %d has been mapped", va + cursor, va, va + len);
                }

                set_pte_flags(ptel1, flags, ((va + cursor < KBASE)? USER_PTE : KERNEL_PTE));
                ptel1->l1_block.is_valid = 1;
                ptel1->l1_block.is_table = 0;
                ptel1->l1_block.pfn = ((pa + cursor) >> L1_INDEX_SHIFT);

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L0_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                // kinfo("The mapping exceeds the address space!");
                                return -ENOMEM;
                        }
                        retval = get_next_ptp(pgtbl, 0, va + cursor + L1_PAGESIZE, &ptpl1, &ptel0, true);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        retval = get_next_ptp(ptpl1, 1, va + cursor, &ptpl2, &ptel1, true);
        if(retval < 0) {
                return retval;
        }

        for(; cursor + L2_PAGESIZE <= len; cursor += L2_PAGESIZE) {
                index = GET_L2_INDEX((va + cursor));
                ptel2 = &(ptpl2->ent[index]);

                if(!IS_PTE_INVALID(ptel2->pte)) {
                        // kwarn("The area %d between %d and %d has been mapped", va + cursor, va, va + len);
                }

                set_pte_flags(ptel2, flags, ((va + cursor < KBASE)? USER_PTE : KERNEL_PTE));
                ptel2->l2_block.is_valid = 1;
                ptel2->l2_block.is_table = 0;
                ptel2->l2_block.pfn = ((pa + cursor) >> L2_INDEX_SHIFT);

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L1_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L0_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        // kinfo("The mapping exceeds the address space!");
                                        return -ENOMEM;
                                }
                                retval = get_next_ptp(pgtbl, 0, va + cursor + L2_PAGESIZE, &ptpl1, &ptel0, true);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl1, 1, va + cursor + L2_PAGESIZE, &ptpl2, &ptel1, true);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        retval = get_next_ptp(ptpl2, 2, va + cursor, &ptpl3, &ptel2, true);
        if(retval < 0) {
                return retval;
        }

        for(; cursor < len; cursor += PAGE_SIZE) {
                index = GET_L3_INDEX((va + cursor));
                ptel3 = &(ptpl3->ent[index]);

                if(!IS_PTE_INVALID(ptel3->pte)) {
                        // kwarn("The area %d between %d and %d has been mapped", va + cursor, va, va + len);
                }

                set_pte_flags(ptel3, flags, ((va + cursor < KBASE)? USER_PTE : KERNEL_PTE));
                ptel3->l3_page.is_valid = 1;
                ptel3->l3_page.is_page = 1;
                ptel3->l3_page.pfn = ((pa + cursor) >> L3_INDEX_SHIFT);

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L2_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L1_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        index = GET_L0_INDEX((va + cursor));
                                        if(index == PTP_ENTRIES - 1) {
                                                // kinfo("The mapping exceeds the address space!");
                                                return -ENOMEM;
                                        }
                                        retval = get_next_ptp(pgtbl, 0, va + cursor + PAGE_SIZE, &ptpl1, &ptel0, true);
                                        if(retval < 0) {
                                                return retval;
                                        }
                                }
                                retval = get_next_ptp(ptpl1, 1, va + cursor + PAGE_SIZE, &ptpl2, &ptel1, true);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl2, 2, va + cursor + PAGE_SIZE, &ptpl3, &ptel2, true);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        return 0;
        /* LAB 2 TODO 4 END */
}

int unmap_range_in_pgtbl_huge(void *pgtbl, vaddr_t va, size_t len)
{
        /* LAB 2 TODO 4 BEGIN */
#define L1_PAGESIZE (1 << (L1_INDEX_SHIFT))
#define L2_PAGESIZE (1 << (L2_INDEX_SHIFT))

        ptp_t *ptpl1, *ptpl2, *ptpl3;
        pte_t *ptel0, *ptel1, *ptel2, *ptel3;
        int retval, cursor, index;

        retval = get_next_ptp(pgtbl, 0, va, &ptpl1, &ptel0, false);
        if(retval < 0) {
                return retval;
        }

        for(cursor = 0; cursor + L1_PAGESIZE <= len; cursor += L1_PAGESIZE) {
                index = GET_L1_INDEX((va + cursor));
                ptel1 = &(ptpl1->ent[index]);

                ptel1->l1_block.is_valid = 0;

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L0_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                kinfo("The mapping exceeds the address space!");
                                return -ENOMEM;
                        }
                        retval = get_next_ptp(pgtbl, 0, va + cursor + L1_PAGESIZE, &ptpl1, &ptel0, false);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        retval = get_next_ptp(ptpl1, 1, va + cursor, &ptpl2, &ptel1, false);
        if(retval < 0) {
                return retval;
        }

        for(; cursor + L2_PAGESIZE <= len; cursor += L2_PAGESIZE) {
                index = GET_L2_INDEX((va + cursor));
                ptel2 = &(ptpl2->ent[index]);

                ptel2->l2_block.is_valid = 0;

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L1_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L0_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        kinfo("The mapping exceeds the address space!");
                                        return -ENOMEM;
                                }
                                retval = get_next_ptp(pgtbl, 0, va + cursor + L2_PAGESIZE, &ptpl1, &ptel0, false);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl1, 1, va + cursor + L2_PAGESIZE, &ptpl2, &ptel1, false);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        retval = get_next_ptp(ptpl2, 2, va + cursor, &ptpl3, &ptel2, false);
        if(retval < 0) {
                return retval;
        }

        for(; cursor < len; cursor += PAGE_SIZE) {
                index = GET_L3_INDEX((va + cursor));
                ptel3 = &(ptpl3->ent[index]);

                ptel3->l3_page.is_valid = 0;

                if(index == PTP_ENTRIES - 1 && cursor + PAGE_SIZE < len) {
                        index = GET_L2_INDEX((va + cursor));
                        if(index == PTP_ENTRIES - 1) {
                                index = GET_L1_INDEX((va + cursor));
                                if(index == PTP_ENTRIES - 1) {
                                        index = GET_L0_INDEX((va + cursor));
                                        if(index == PTP_ENTRIES - 1) {
                                                kinfo("The mapping exceeds the address space!");
                                                return -ENOMEM;
                                        }
                                        retval = get_next_ptp(pgtbl, 0, va + cursor + PAGE_SIZE, &ptpl1, &ptel0, false);
                                        if(retval < 0) {
                                                return retval;
                                        }
                                }
                                retval = get_next_ptp(ptpl1, 1, va + cursor + PAGE_SIZE, &ptpl2, &ptel1, false);
                                if(retval < 0) {
                                        return retval;
                                }
                        }
                        retval = get_next_ptp(ptpl2, 2, va + cursor + PAGE_SIZE, &ptpl3, &ptel2, false);
                        if(retval < 0) {
                                return retval;
                        }
                }
        }

        return 0;
        /* LAB 2 TODO 4 END */
}

#ifdef CHCORE_KERNEL_TEST
#include <mm/buddy.h>
#include <lab.h>
void lab2_test_page_table(void)
{
        vmr_prop_t flags = VMR_READ | VMR_WRITE;
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;

                ret = map_range_in_pgtbl(
                        pgtbl, 0x1001000, 0x1000, PAGE_SIZE, flags);
                lab_assert(ret == 0);

                ret = query_in_pgtbl(pgtbl, 0x1001000, &pa, &pte);
                lab_assert(ret == 0 && pa == 0x1000);
                lab_assert(pte && pte->l3_page.is_valid && pte->l3_page.is_page
                           && pte->l3_page.SH == INNER_SHAREABLE);
                ret = query_in_pgtbl(pgtbl, 0x1001050, &pa, &pte);
                lab_assert(ret == 0 && pa == 0x1050);

                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000, PAGE_SIZE);
                lab_assert(ret == 0);
                ret = query_in_pgtbl(pgtbl, 0x1001000, &pa, &pte);
                lab_assert(ret == -ENOMAPPING);

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap one page");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                size_t nr_pages = 10;
                size_t len = PAGE_SIZE * nr_pages;

                ret = map_range_in_pgtbl(pgtbl, 0x1001000, 0x1000, len, flags);
                lab_assert(ret == 0);
                ret = map_range_in_pgtbl(
                        pgtbl, 0x1001000 + len, 0x1000 + len, len, flags);
                lab_assert(ret == 0);

                for (int i = 0; i < nr_pages * 2; i++) {
                        ret = query_in_pgtbl(
                                pgtbl, 0x1001050 + i * PAGE_SIZE, &pa, &pte);
                        lab_assert(ret == 0 && pa == 0x1050 + i * PAGE_SIZE);
                        lab_assert(pte && pte->l3_page.is_valid
                                   && pte->l3_page.is_page);
                }

                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000, len);
                lab_assert(ret == 0);
                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000 + len, len);
                lab_assert(ret == 0);

                for (int i = 0; i < nr_pages * 2; i++) {
                        ret = query_in_pgtbl(
                                pgtbl, 0x1001050 + i * PAGE_SIZE, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap multiple pages");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                /* 1GB + 4MB + 40KB */
                size_t len = (1 << 30) + (4 << 20) + 10 * PAGE_SIZE;

                ret = map_range_in_pgtbl(
                        pgtbl, 0x100000000, 0x100000000, len, flags);
                lab_assert(ret == 0);
                ret = map_range_in_pgtbl(pgtbl,
                                         0x100000000 + len,
                                         0x100000000 + len,
                                         len,
                                         flags);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len * 2;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == 0 && pa == va);
                }

                ret = unmap_range_in_pgtbl(pgtbl, 0x100000000, len);
                lab_assert(ret == 0);
                ret = unmap_range_in_pgtbl(pgtbl, 0x100000000 + len, len);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap huge range");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                /* 1GB + 4MB + 40KB */
                size_t len = (1 << 30) + (4 << 20) + 10 * PAGE_SIZE;
                size_t free_mem, used_mem;

                free_mem = get_free_mem_size_from_buddy(&global_mem[0]);
                ret = map_range_in_pgtbl_huge(
                        pgtbl, 0x100000000, 0x100000000, len, flags);
                lab_assert(ret == 0);
                used_mem =
                        free_mem - get_free_mem_size_from_buddy(&global_mem[0]);
                lab_assert(used_mem < PAGE_SIZE * 8);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == 0 && pa == va);
                }

                ret = unmap_range_in_pgtbl_huge(pgtbl, 0x100000000, len);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap with huge page support");
        }
        printk("[TEST] Page table tests finished\n");
}
#endif /* CHCORE_KERNEL_TEST */
