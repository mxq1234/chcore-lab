### 思考题1

Chcore内核启动时，执行 `_start` 函数，让一个核进入初始化流程。调用 `arm64_elX_to_el1` 函数，将异常等级降到EL1。调用 `init_c` 函数，完成一些初始化工作，包括：

* 调用 `early_uart_init` 函数，初始化输入输出串口
* 调用 `init_boot_pt` 函数，配置启动页表
* 调用 `el1_mmu_activate` 函数，开启MMU，启用虚拟内存机制
* 调用 `start_kernel` 函数，跳转到高地址，开始真正执行内核代码。

内核镜像开始运行后：

* 调用 `mm_init` 函数，初始化物理内存池，创建伙伴系统和slab分配器来管理物理内存
* 调用 `arch_interrupt_init` 函数，配置异常向量表，启用异常机制
* 调用 `create_root_thread` 函数，创建第一个用户进程和线程，完成相应的初始化，包括内核对象和数据结构的创建管理、栈空间的映射、代码段和数据段的加载等，最后将当前线程设置为新创建的线程
* 调用 `eret_to_thread` 函数，从内核态返回用户态，开始执行第一个用户线程

### 练习题2

`cap_group_init` ：调用 `slot_table_init` 函数，初始化 `slot_table`，为slot表和bitmap分配空间。初始化 `thread_list`，将线程数设置为0，将进程id设置为给定的id。

`sys_create_cap_group` ：调用 `obj_alloc` 函数，创建新的 `cap_group` 和 `vmspace` 对象，分别用 `cap_group_init` 和 `vmspace_init` 函数进行初始化。

`create_root_cap_group` ：调用 `obj_alloc` 函数，创建新的 `cap_group` 和 `vmspace` 对象，分别用 `cap_group_init` 和 `vmspace_init` 函数进行初始化，将`cap_group` 的pid设置为 `ROOT_PID`，将 `vmspace` 的pcid设置为 `ROOT_PCID`。调用 `cap_alloc` 函数，将新创建的 `cap_group` 和 `vmspace` 对象加入 `cap_group` 的 slot表，其中 `cap_group` 的 `slot_id` 为0(第一个cap)，`vmspace` 的 `slot_id` 为1(第二个cap)。

### 练习题3

如果 `p_align` 字段不是0或1，那么对于 `PT_LOAD` 的段，需要以 `PAGE_SIZE` 对齐，其中 `p_vaddr` 向下对齐，`p_vaddr + p_memsz` 向上对齐，二者的差值即为实际需要映射的页面大小 `seg_map_sz`。

调用 `create_pmo` 函数，创建一个物理内存对象。由于加载到内存的段主要是代码段和数据段，在用户程序开始运行不久后就会访问，并且应当被缓存，所以采用 `PMO_DATA` 类型，立即分配物理页，分配的物理内存大小为 `seg_map_sz`。

创建好 `pmo` 后，物理地址 `pmo->start` 指向了分配好的 `pmo->size` 大小的物理内存，先将这块物理内存清零(比如.bss段的情况)，然后将实际的代码和数据通过 `memcpy` 加载到相应的物理内存位置。

最后调用 `vmspace_map_range` 函数，创建VMR，将虚拟地址空间 `p_vaddr_aligned` 到`p_vaddr_aligned + seg_map_sz` 的区域映射到 `pmo` 的物理内存空间。通过 `p_flags` 字段，确定VMR的权限。由于采用立即映射，所以该函数也会配置好页表。

### 练习题4

`el1_vector`：使用汇编宏 `.macro exception_entry label`，按照ARMv8异常向量表的结构，为每一个entry添加跳转到相应处理程序label处的指令。

使用 `bl` 指令跳转到具体的处理函数。

### 练习题5
``` c
ret = handle_trans_fault(current_thread->vmspace, fault_addr);
```

### 练习题6

先用 `get_page_from_pmo` 函数查看radix树中有无该虚拟页对应的物理页。

如果物理页还没有被分配，调用 `get_pages` 函数分配一块 `PAGE_SIZE` 大小的物理页。如果分配失败，则物理内存耗尽，需要换页。调用 `commit_page_to_pmo` 函数，将新分配的物理页加入radix树，用对应虚拟页的index进行索引。最后使用 `map_range_in_pgtbl` 函数更新页表。

如果物理页已经被分配，则直接使用 `map_range_in_pgtbl` 函数更新页表。

### 练习题7

`exception_enter`：将31个通用寄存器和 `sp_el0`，`elr_el1`，`spsr_el1` 寄存器压栈保存，防止值被覆盖。

`exception_exit`：将上述寄存器的值从栈中恢复。

### 思考题8

假设有一个运行在AArch64的用户程序，执行了 `svc` 指令。此时会跳转到异常向量表 +0x400处，执行 `exception_entry sync_el0_64` 指令，跳转到 `sync_el0_64` 处。这段汇编程序在保存寄存器后，先检查 `esr_el1` 的值，如果异常症状是 `svc`，则调用 `el0_syscall` 函数处理系统调用。

`el0_syscall` 函数通过 `adr` 指令获得 `syscall_table` 的地址，该表是一个函数指针数组，表的条目是系统调用处理函数的入口地址。`svc` 指令后面跟着的系统调用号此时存放在 `w8` 寄存器中，用系统调用号作为索引，从 `syscall_table` 中取出相应的handler的地址，跳转过去。

### 练习题9

`sys_putc`：使用 `uart_send` 函数打印字符。

`sys_getc`：使用 `uart_recv` 函数接收字符。

`sys_thread_exit`：将当前线程的退出状态设置为 `ET_EXITING`，将当前线程的状态设置为 `ES_EXIT`，将当前线程置空。

`__chcore_sys_putc`：系统调用 `sys_putc` 函数有一个参数，因此选择 `__chcore_syscall1` 函数，该函数的第一个参数是 `sys_putc` 的系统调用号 `__CHCORE_SYS_putc`，第二个参数是 `sys_putc` 函数的第一个参数 ch。

`__chcore_sys_getc`：系统调用 `sys_getc` 函数没有参数，因此选择 `__chcore_syscall0` 函数，该函数的第一个参数是 `sys_getc` 的系统调用号 `__CHCORE_SYS_getc`。返回值为接收到的字符。

`__chcore_sys_thread_exit`：系统调用 `sys_thread_exit` 函数没有参数，因此选择 `__chcore_syscall0` 函数，该函数的第一个参数是 `sys_thread_exit` 的系统调用号 `__CHCORE_SYS_thread_exit`。