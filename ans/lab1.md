### 思考题1：  

`_start`函数先将`mpidr_el1`寄存器的值写入x8中，通过AND运算保留其低8位，而`mpidr_el1`寄存器值的低8位用于表示当前处理器的编号。接着利用`cbz x8, primary`指令，使得0号处理器跳转到primary标签处执行后续的初始化指令，而其他处理器则执行`b .`指令，该指令跳转到当前位置，被阻塞在这里，不会执行后续的初始化指令。

### 练习题2：

用 `mrs x9, CurrentEL` 将当前异常级别从系统寄存器CurrentEL加载到通用寄存器x9中，此处x9的值将被设置为12，即为CURRENTEL_EL3。

### 练习题3：

先用 `adr x9, .LTarget` 和 `msr elr_el3, x9` 将ret的内存地址(即.LTarget标签的地址)写入异常链接寄存器elr_el3，使得异常返回时跳转到ret语句，进而返回到_start函数。再用 `mov x9, SPSR_ELX_DAIF | SPSR_ELX_EL1H` 
和 `msr spsr_el3, x9` 两条语句，将SPSR_EL3中控制中断和异常屏蔽(DAIF)的位设为1，在跳转到 EL1 时暂时屏蔽所有中断，将SPSR_EL3中指定栈指针的位设为1，使用SP_EL1作为栈指针。

### 思考题4：

之所以要在进入 C 函数之前设置启动栈，是因为函数调用时，caller的FP存放在x29中，返回地址保存在x30中。由于callee也有自己的帧指针，会覆盖x29的值。callee也会调用其他函数，会覆盖x30的值。因此在生成ARM64汇编时，需要在callee入口处将帧指针寄存器x29和返回地址寄存器x30压栈保存。如果没有设置启动栈，则无法将x29和x30的值压栈保存，在callee返回前将无法恢复x29和x30的值，因而无法获得caller的帧指针和返回地址，会导致程序无法正常返回到caller。另外，栈也被用于存放溢出的局部变量和临时变量、传递参数等，C 语言的指针、结构也可能会用到栈，所以在进入 C 函数前应当设置启动栈。

### 思考题5：

内核镜像加载时，未初始化的或初始化为0的全局变量和静态变量存储在`.bss`段，默认被初始化为0。如果没有将`.bss`段清零，后续访问未初始化的或初始化为0的全局变量和静态变量，它们的值是不确定的，这种不确定值可能引发程序异常或错误，导致内核无法正常工作。

### 练习题6：

for循环遍历str数组的每一个字符，调用 `early_uart_send` 函数发送单个字符，直到遇到`'\0'`为止。

### 练习题7：

用 `orr x8, x8, #SCTLR_EL1_M` 将M位置为1，启用MMU。按下Ctrl-C后，gdb输出如下，说明执行流确实在0x200处：

```
(gdb) continue
Continuing.

Thread 1 received signal SIGINT, Interrupt.
0x0000000000000200 in ?? ()
Cannot access memory at address 0x200
```