### 思考题1

`_start` 函数先将 `mpidr_el1` 寄存器的值写入x8中，通过AND运算保留其低8位，而 `mpidr_el1` 寄存器值的低8位表示当前处理器的编号。接着利用 `cbz x8, primary` 指令，使得0号处理器作为主CPU，跳转到 `primary` 标签处执行后续的初始化指令。

其他处理器则执行到 `wait_for_bss_clear` 标签处，等待主CPU将 `clear_bss_flag` 置为0，否则会不断跳转回 `wait_for_bss_clear` 标签处检查 `clear_bss_flag` 的值。

接着，其他CPU降低自己的异常级别，配置自己的启动栈，执行到 `wait_until_smp_enabled` 标签处后，检查secondary_boot_flag[CPU_id]的值，如果为0，则跳转回 `wait_until_smp_enabled` 标签处，不断地检查secondary_boot_flag[CPU_id]的值，被阻塞在这里，直到secondary_boot_flag[CPU_id]的值被主CPU依次置为非零，才开始执行 `secondary_init_c`，初始化自己。

### 思考题2

对于主CPU，`secondary_boot_flag` 是虚拟地址，因为主CPU在 `init_c` 中使用 `secondary_boot_flag` 时，已经启用MMU，开启了虚拟内存机制，所以使用的是虚拟地址。但此时还处于boot目录下，主CPU仍然运行在低地址空间，而低地址空间的启动页表被配置为虚拟地址等于物理地址。

对于其他CPU，`secondary_boot_flag` 是物理地址。因为其他CPU在 `_start` 函数中使用 `secondary_boot_flag` 时，尚未启用MMU，开启虚拟内存机制，所以只能使用物理地址访问内存。

`secondary_boot_flag` 定义在init_c.c中。`init_c` 函数在调用 `start_kernel` 函数时，将其作为第一个参数放在 `x0` 寄存器中传递，此时 `x0` 中存放的是 `secondary_boot_flag` 的虚拟地址的值，但由于处在低地址空间，所以等于物理地址的值，可以看成传递的是物理地址。 

`start_kernel` 函数跳转到高地址空间，将 `x0` 寄存器压栈保存，之后从栈上恢复，接着调用 `main` 函数，此时 `secondary_boot_flag` 成为 `main` 函数的第一个参数，由于其存放了物理地址的值，所以类型为 `paddr_t`。`main` 函数又将它作为参数传递给 `enable_smp_cores` 函数。

`enable_smp_cores` 函数处于高地址空间，所以使用如下方式获取 `secondary_boot_flag` 的虚拟地址。这样，主CPU在内核中也可以访问 `secondary_boot_flag` 了。

``` c
void enable_smp_cores(paddr_t boot_flag) {
        long *secondary_boot_flag;
        secondary_boot_flag = (long *)phys_to_virt(boot_flag);
        ...
}
```

### 练习题3

`enable_smp_cores`: 以待激活的CPU的编号为索引，将 `secondary_boot_flag` 数组中对应的元素置为非零，相应的CPU检测到`secondary_boot_flag[CPU_id]` 的值非零后，跳出循环，执行`secondary_init_c` 函数。在该CPU状态变为run之前，用while循环阻塞住主CPU，使其等待该CPU完成初始化后，再激活下一个。

`secondary_start`：将CPU状态设为run。

### 练习题4

1. `unlock`：lock->owner++，将锁传递给下一个拿票的线程。
   `is_locked`：如果lock->owner < lock->next，那么值为lock->owner的票已经卖出，锁正在被拿到票的线程持有。

2. `kernel_lock_init`：调用 `lock_init` 函数初始化大内核锁。
   `lock_kernel`：调用 `lock` 函数获取大内核锁。
   `unlock_kernel`：调用 `unlock` 函数释放大内核锁。

3. 在文档所述的5个位置调用 `lock_kernel`。
   
4. 对于返回原线程的系统调用，`el0_syscall` 在 `exception exit` 前调用  `unlock_kernel`，释放大内核锁，返回用户态。其余情况则通过 `eret_to_thread` 函数返回用户态，在该函数前调用 `unlock_kernel` 函数，释放大内核锁，返回用户态。一个特例是 `handle_irq` 函数，只有中断来自用户态或者idle线程被中断，才会获取大内核锁，所以在放锁前需要判断当前线程是否持有锁。

### 思考题5

不需要。因为调用了 `unlock_kernel` 以后，就会开始 `exception_exit`，恢复处理器上下文并异常返回，所以即使调用 `unlock_kernel` 会覆盖寄存器的值，这些值也不再被使用了，也就无需保存恢复。

### 思考题6

如果将 `idle_threads` 加入到等待队列，即使等待队列中还有其他线程，也会时不时地调度到 `idle_threads`，做无意义的空转，浪费CPU资源。

### 练习题7

`rr_sched_enqueue`：对于thread为空、thread_ctx为空、thread状态为TS_READY(即已经在等待队列中的)、thread的亲和性不在合法范围的，返回-EINVAL。如果thread是空闲线程，则直接返回0。如果thread亲和性为NO_AFF，将其加入当前核心的等待队列，否则将其加入编号为aff的核心的等待队列，将thread的cpu_id设为其所在队列的cpu_id，将thread状态设为TS_READY。

`rr_sched_dequeue`：对于thread为空、thread_ctx为空、thread状态不为TS_READY(即不在等待队列中的)、thread为空闲线程、thread不在当前核心等待队列的，返回-EINVAL。将thread从当前核心的等待队列中移除，并将其状态设为TS_INTER。

`rr_sched_choose_thread`：如果当前核心的等待队列为空，则返回当前核心的空闲线程，否则用 `list_entry` 取出等待队列的队首线程，并调用 `rr_sched_dequeue` 将其出队，返回该线程。

`rr_sched`：如果当前线程及其上下文不为空，且其退出状态为TE_EXITING，则将其退出状态设为TE_EXITED，状态设为TE_EXIT，表示线程已经退出。如果当前线程状态为TS_RUNNING，则将来还会被调度到，调用 `rr_sched_enqueue` 将其入队。之后调用 `rr_sched_choose_thread` 选择一个线程，调用 `switch_to_thread` 切换到该线程的上下文和地址空间。

### 思考题8

当空闲线程收到中断，会经由 `irq_el1h` 调用 `handle_irq` 函数，该函数最终会通过 `eret_to_thread` 返回。假设某一时刻锁的owner为1，如果在 `eret_to_thread` 中调用了 `unlock_kernel`，且之前没有获取大内核锁，则放锁会使owner变为2，此时那个持锁的线程(票号为1)也放锁，那么锁的owner会变为3。等调度到持有2号票的线程时，owner已经大于2，则该线程永远不可能拿到锁，被阻塞在内核态。

在我的实现中，没有将 `unlock_kernel` 写在 `eret_to_thread`中，而是每次调用 `eret_to_thread` 前小心地调用 `unlock_kernel` 函数，释放大内核锁，因此 `handle_irq` 中是这样放锁的：

``` c
void handle_irq(int type)
{
        bool is_locked = false;
        if (type >= SYNC_EL0_64
            || current_thread->thread_ctx->type == TYPE_IDLE) {
                lock_kernel();
                is_locked = true;
        }

        plat_handle_irq();
        sched();

        if (is_locked)
                unlock_kernel();   
        eret_to_thread(switch_context());
}
```

这样即使空闲线程不拿锁，也不会出现阻塞内核的情况。但是空闲线程被中断还是应当先拿大内核锁，因为这里调用了 `sched` 函数，会出现多个核心访问共享数据结构 `rr_ready_queue_meta` 中同一个元素的情况(enqueue)，所以需要并发的保护。

### 练习题9

`sys_yield`：调用 `sched` 函数，挂起当前线程并切换到新调度到的线程，释放大内核锁后通过 `eret_to_thread` 返回新调度到的线程。

`sys_get_cpu_id`：调用 `smp_get_cpu_id` 函数获取当前核心的编号。

### 练习题10

在 `main` 函数和 `secondary_start` 函数的相应位置调用 `timer_init` 函数。

### 练习题11

`sched_handle_timer_irq`：如果当前线程不为空、线程上下文不为空、sc不为空且budget大于0(防止underflow)，则将线程的budget减1，表示线程消耗了1单位的时间片。

`rr_sched`：如果当前线程及其上下文不为空，sc不为空且budget大于0，说明其时间片还未用完，则不予调度，直接返回。否则，如果当前线程状态为 TS_RUNNING，则将其budget恢复为DEFAULT_BUDGET，放回调度队列挂起，等待下一次调度。

`sys_yield`：调用 `sched` 前将当前线程的budget置为0，使得 `sched` 挂起当前线程并重新选择线程执行。

`sys_thread_exit`：同上。

### 练习题12

`sys_set_affinity`：直接将线程的affinity设为给定的aff。

`sys_get_affinity`：返回线程的affinity。

`rr_sched_enqueue`：练习题7中已实现。

`rr_sched`：调用 `rr_sched_choose_thread` 函数选出下一个线程后，如果该线程不是空闲线程或NO_AFF的线程，则检查该线程的affinity，如果不等于当前核心的编号，说明该线程的affinity被设置过，不应该在当前核心上运行。调用 `rr_sched_enqueue` 将其放到其他核心的等待队列中，再用 `rr_sched_choose_thread` 函数获取下一个进程，直到得到一个可以在当前核心上运行的线程。

### 练习题13

通过 `__chcore_sys_create_pmo` 系统调用创建主线程栈的pmo，这里仿照root_thread的栈，将pmo类型设为PMO_ANONYM，按需分配。

`stack_top` 为主线程栈的基地址加上主线程栈的大小，栈底1K的页被用来存放环境变量、命令行参数，所以栈指针相对于栈基地址的 `offset` 为主线程栈的大小减去1K。

将主线程栈的pmo映射请求写入 `pmo_map_requests[0]`，其中addr为主线程栈基地址，perm为r/w。

args.stack中的值最终会作为栈指针的初始值，所以是栈基地址加上offset。

### 练习题14

`ipc_register_server`：将vm_config的各字段设置为定义好的宏，栈基地址和共享内存基地址都是server地址空间的虚拟地址。调用该函数的server线程将作为listener线程，为建立连接的client提供被调函数、server配置以及唯一的conn_idx。

`ipc_register_client`：将vm_config中的共享内存基地址设为CLIENT_BUF_BASE + client_id * CLIENT_BUF_SIZE，使用唯一的client_id可以使不同的线程使用不同的虚拟内存区域映射共享内存，不至于发生冲突。共享内存基地址是client地址空间的虚拟地址，共享内存大小设为CLIENT_BUF_SIZE。调用该函数的client将与server创建连接，并创建一个worker线程(server的线程)服务于该连接的call请求。

`create_connection`：将worker线程的栈基地址设为vm_config->stack_base_addr + conn_idx * vm_config->stack_size，使用唯一的conn_idx可以使不同的worker线程使用不同的虚拟内存区域当做自己的栈，不至于发生冲突。worker线程的共享内存基地址同理，client的共享内存基地址则直接使用client_vm_config中设置好的。分别调用 `vmspace_map_range` 函数将client和worker线程的虚拟内存区域映射到buf_pmo的物理内存区域，完成共享内存的建立。

`ipc_set_msg_data`：共享内存的头部作为ipc_msg的元数据区域，调用`ipc_get_msg_data` 获得数据区域的首地址，调用 `memcpy` 将数据拷贝到首地址+offset处。

`sys_ipc_call`：如果 `cap_num` 大于0，则调用 `ipc_send_cap` 向server传送相应的cap。arg要作为被调函数的第一个参数，而被调函数的第一个参数是ipc_msg，所以arg需要指向server地址空间共享内存的基地址。

`ipc_send_cap`：调用 `cap_copy` 函数将当前cap_group中第cap_buf[i]个cap拷贝到server的cap_group中，返回的dest_cap存放在cap_buf[i]，待会儿会传递给server的worker线程使用，使其可以访问这些对象。

`thread_migrate_to_server`：目标线程的栈指针设为conn->server_stack_top，PC设为server注册的被调函数，arg0设为上述的arg，arg1设为source线程所在进程的pid。在 `eret_to_thread` 前释放大内核锁。

`sys_ipc_return`：将source线程的状态由TS_WAITING重新设置为TS_RUNNING，source线程将继续使用当前线程(target线程)的budget。

`thread_migrate_to_client`：设置source线程的返回值。在 `eret_to_thread` 前释放大内核锁。

### 练习题15

`wait_sem`：如果sem_count还大于0，则将其减1，代表消耗了一单位的资源。否则，如果is_block，则将当前线程添加到sem的等待队列中，将线程状态设为TS_WAITING并将budget清零，调用 `obj_put` 函数“放弃”对sem的引用。调用 `sched` 函数挂起当前线程，等待资源的产生。释放大内核锁，通过 `eret_to_thread` 返回到新调度的线程。如果is_block为false，直接返回-EAGAIN。

`signal_sem`：如果sem的等待队列为空，说明暂时没有人消耗资源，那么将sem_count加1。否则使用 `list_entry` 取出sem等待队列队首的线程，将其出队后，调用 `sched_enqueue` 将其放入调度队列，该线程直接得到这1单位的资源。

### 练习题16

`producer`：需要消耗empty_slot，因此通过 `sys_wait_sem` 系统调用等待在empty_slot上。获得empty_slot后，产生filled_slot，通过 `sys_signal_sem` 系统调用通知等在filled_slot上的consumer。

`consumer`：需要消耗filled_slot，因此通过 `sys_wait_sem` 系统调用等待在filled_slot上。获得filled_slot后，产生empty_slot，通过 `sys_signal_sem` 系统调用通知等在empty_slot上的producer。

### 练习题17

`lock_init`：通过 `sys_create_sem` 系统调用创建sem，由于刚开始的时候lock资源必须要有(还没有人拿锁)，所以通过 `sys_signal_sem` 系统调用“生产”lock资源，将sem_count的初始值变为1。

`lock`：通过 `sys_wait_sem` 系统调用获得锁，"消耗"锁资源。

`unlock`：通过 `sys_signal_sem` 系统调用释放锁，"释放"锁资源。