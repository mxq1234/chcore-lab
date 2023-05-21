### 思考题1
1. circle中还提供了SDHost的代码。SD卡，EMMC和SDHost三者之间的关系是怎么样的？
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;SD卡是真实的可移动存储设备，是一种闪存存储卡。EMMC，全称是“嵌入式多媒体控制器”，是指由闪存和集成在同一硅片上的闪存控制器组成的封装，有SD卡控制器的作用。SDHost是在主处理器或控制器芯片中实现的硬件或软件模块，负责管理与SD卡或eMMC之间的数据传输和通信协议。通过控制信号和命令与存储介质进行交互，处理读取和写入数据的操作。
  
2. 请详细描述Chcore是如何与SD卡进行交互的？即Chcore发出的指令是如何输送到SD卡上，又是如何得到SD卡的响应的。(提示: IO设备常使用MMIO的方式映射到内存空间当中)
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Chcore与SD卡进行交互的过程主要通过内存映射IO（Memory-Mapped IO，MMIO）的方式进行。SD控制器的寄存器被映射到了内存地址空间的特定位置，从而可以通过读写内存地址的方式与IO设备进行通信。
+ 在Chcore中，首先需要进行内存映射操作，将SD控制器的寄存器映射到内存地址空间中的特定地址范围。这样，Chcore就可以通过读写该地址范围的内存来与SD控制器进行交互。

+ 当Chcore需要发送指令给SD卡时，它会将指令写入到特定的内存地址中，这些内存地址对应着SD控制器的寄存器。通过写入指令到对应的寄存器，Chcore实际上是向SD控制器发出指令。这些指令包括初始化命令、读取命令、写入命令等，用于控制SD卡的操作。

+ SD控制器接收到Chcore写入的指令后，根据指令的内容和协议规范，将指令发送到SD卡上。SD卡执行相应的操作，并将响应数据写回到SD控制器的寄存器中。

+ Chcore通过读取对应的内存地址，即SD控制器的寄存器，来获取SD卡的响应数据。它可以从寄存器中读取响应状态、数据传输结果等信息，以了解SD卡的执行情况。

3. 请简要介绍一下SD卡驱动的初始化流程。

+ 硬件初始化：首先，需要对SD卡所连接的硬件进行初始化设置。这包括配置GPIO引脚、时钟和电源等，以确保SD卡接口能够正常工作。
+ 初始化SD控制器：SD卡与主机之间的通信是通过SD控制器进行的。在初始化流程中，需要设置SD控制器的寄存器，以使其能够与SD卡进行正确的通信。包括设置时钟频率、数据传输模式和通信协议等参数。
+ 发送初始化命令：在SD卡与SD控制器初始化完成后，需要向SD卡发送一系列的初始化命令，以建立正确的通信状态。这些命令包括复位命令、发送特定的SD卡命令和参数等。
+ 初始化协议：SD卡支持多种通信协议，例如SD卡协议和SPI协议。根据具体情况，需要选择并初始化适当的协议，其中涉及到设置时钟频率、数据传输模式和命令格式等。
+ 配置块大小和读取/写入参数：SD卡以块的形式进行数据读取和写入。在初始化流程中，需要设置块大小和读取/写入参数，以确保后续的数据传输操作可以正常进行。
+ 完成初始化：此时，可以进行读取和写入等操作，与SD卡进行数据交互。

4. 在驱动代码的初始化当中，设置时钟频率的意义是什么？为什么需要调用TimeoutWait进行等待?
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;SD卡和主机之间的通信是通过时钟信号进行同步的。时钟频率决定了数据传输的速度和稳定性。设置时钟频率，是为了确保SD卡与主机之间的数据传输能够在正确的时序下进行，否则可能导致通信错误、数据传输失败或不稳定等问题。
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;调用TimeoutWait，是为了等待某个特定的事件发生或某个操作完成。在SD卡初始化过程中，有些操作可能需要一定的时间才能完成，比如SD卡的复位过程或初始化命令的执行。TimeoutWait会使驱动程序等待一段时间，直到达到设定的超时时间或特定事件发生，以确保操作能够顺利进行或超时错误能够被捕获。

### 练习1

`sdcard_readblock`：调用 `DoRead` 函数，读取 `BLOCK_SIZE` 大小的数据，`DoRead` 函数将调用 `DoDataCommand` 函数

`sdcard_writeblock`：调用 `DoWrite` 函数，将 `BLOCK_SIZE` 大小的数据写入，`DoWrite` 函数将调用 `DoDataCommand` 函数

`DoDataCommand`：如果 `is_write` 为true，则将command设置为 `WRITE_BLOCK`，否则设置为 `READ_SINGLE_BLOCK`，该函数后面的写法似乎不支持R/W MULTIPLE_BLOCK，这里也只需要读一个BLOCK即可

`Seek`：将 `m_ullOffset` 设置为 `ullOffset`，要求 `ullOffset` 必须是 `SD_BLOCK_SIZE` 对齐的

`sd_Read`：从 `m_ullOffset` 对应的 `lba` 开始，循环调用 `DoRead` 函数，逐块读取数据，直到读到 `nCount` 大小的数据为止，更新 `m_ullOffset`，要求 `nCount` 必须是 `SD_BLOCK_SIZE` 对齐的

`sd_write`：类似于 `sd_Read`

### 练习2

`block_layer.h` 中定义的两个函数提供了 `block` 层的读写操作，因此可以在 `block` 层上实现 `inode` 文件系统：

+ `block 0` 是 `superblock`，其最开始是一个8字节的魔数，存放着 `"naivefs"` 字符串，用来表示inode文件系统已经初始化。如果在进行文件操作时发现没有这个魔数，则先调用 `naive_fs_init` 函数初始化文件系统的 `superblock`、`bitmap`、`root_inode`。`superblock` 还存储了文件系统的其他规格数据，比如 `block_num`、`block_size` 等等

+ `block 1` 是 `bitmap`，大小为 `BLOCK_SIZE`(即512字节)，可以支持2G的SD卡。提供了 `SET_BIT`、`CLEAR_BIT`、`GET_BIT`、`FIND_NEXT_ZERO` 等一系列宏来操作 `bitmap`

+ `block 2` 是 `root_inode`，代表根目录的 `inode`，这里为了方便起见将一个inode的大小设置为一个block的大小。这里的 `inode` 只包含size和blocks两个字段，分别指明根目录或文件的大小，以及数据存储的块号

+ `block 3~255` 是文件的 `inode` 表，`block 256~4096` 用来作为根目录或文件的数据块

+ `direntry` 是根目录数据块中存储的目录项，包含name和inode_block两个字段，分别代表文件名和文件inode的块号，其中文件名不超过28字节，因此每个 `direntry` 的大小固定为32字节

在此基础上，实现了 `naive_fs_alloc_inode`、`naive_fs_free_inode`、`naive_fs_alloc_data_block`、`naive_fs_free_data_block` 等工具函数，这几个函数比较简单，下面阐述其他几个工具函数：

+ `naive_fs_append_direntry`：被create函数调用，在根目录中添加新文件的目录项。接收文件名和文件块号，组成一个 `direntry` 后，添加到根目录数据区的尾部，如果现有的数据块不够，则分配新的数据块，并更新根目录inode

+ `naive_fs_remove_direntry`：被unlink函数调用，在根目录中删除文件的目录项。接收文件名，查找到相应的 `direntry`，为了避免形成“空洞”，将根目录的最后一个目录项拷贝到此处，覆盖要被删除的目录项(因为目录项的大小是固定的)，如果根目录的最后一个目录项恰好独自存储在一个数据块，则删除该数据块，并更新根目录inode

+ `naive_fs_lookup`：从根目录的目录项中逐个查找，匹配文件名，返回inode块号，如果不存在，返回-1

利用以上的结构和函数，实现外部接口：

+ `naive_fs_access`：调用 `naive_fs_check_init`，检查文件系统是否被初始化，如果没有，则调用 `naive_fs_init` 进行初始化。调用 `naive_fs_lookup` 函数查找目录项，根据返回结果确定文件是否存在

+ `naive_fs_creat`：调用 `naive_fs_lookup` 函数，检查文件是否存在，若不存在，调用 `naive_fs_alloc_inode` 为新文件分配inode，调用 `naive_fs_append_direntry` 将新文件的文件名和inode块号以目录项的形式添加到根目录中。最后使用 `sd_bwrite` 将新文件的inode内容清零，完成初始化

+ `naive_fs_pread`：调用 `naive_fs_lookup` 函数，检查文件是否存在，若存在，则已经得到了文件的inode块号，调用 `sd_bread` 函数读取inode内容。接着调用 `sd_bread` 函数从 `offset` 所在的块开始(offset如果不在正常范围内，返回0，代表没读到)，逐块读取文件内容，直到读到size大小的数据，或者读到文件的末尾，返回实际读到的字节数

+ `naive_fs_pwrite`：调用 `naive_fs_lookup` 函数，检查文件是否存在，若不存在则创建文件。通过 `naive_fs_lookup` 获得文件的inode块号后，调用 `sd_bread` 函数读取inode内容。如果offset超过文件长度，返回错误(也可以选择将中间的“空洞”以0填充)。从 `offset` 所在的块开始，读取块的内容，写入数据，将块写回SD卡，如果数据块不够用，则调用
`naive_fs_alloc_data_block` 分配新的数据块，更新inode，最后需要将inode写回。函数返回实际写入的字节数

+ `naive_fs_unlink`：调用 `naive_fs_lookup` 函数，检查文件是否存在，若存在，则已经得到了文件的inode块号，调用 `sd_bread` 函数读取inode内容。对于每一个数据块，调用 `naive_fs_free_data_block` 函数释放数据块。接着调用 `naive_fs_free_inode` 函数释放inode，最后调用 `naive_fs_remove_direntry` 函数从根目录中移除目录项

### 思考题

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;SD卡可以采用Master Boot Record (MBR)或GUID Partition Table (GPT)的分区方式。在MBR分区中，主引导记录（Master Boot Record）的分区表记录了分区的起始位置、大小和分区类型。GPT分区表则使用分区表头（Partition Table Header）和分区表项（Partition Entry）来描述分区信息。文件系统通常具有特定的标识符或特征，用于识别分区中使用的文件系统类型。而SD卡的分区表中存储着文件系统标识符，用于识别分区所采用的文件系统类型，比如FAT32、NTFS、EXT4等等。

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;要为ChCore提供多分区的SD卡驱动支持，需要修改SD卡驱动程序，使其可以解析SD卡的分区表，并挂载每个分区使其可用。以MBR的分区方式为例，详细的设计方案如下：

1. 解析

+ 启动时读取SD卡的主引导记录(Master Boot Record, MBR)，该记录位于SD卡的第一个扇区。主引导扇区包含了分区表，其中记录了SD卡上各个分区的起始位置和大小。解析MBR，获取分区表的信息，进而获取SD卡每个分区的起始位置和大小等信息。

+ 对于每个分区，读取该分区的引导扇区(Boot Sector)。引导扇区包含了分区的文件系统类型信息和文件系统的元数据信息。通过解析引导扇区，可以获取要挂载的文件系统的类型信息和元数据。

2. 挂载

+ Chcore要创建一个虚拟文件系统VFS，用于统一管理不同类型文件系统的访问。VFS提供了抽象的接口，使得用户和应用程序可以通过统一的文件系统接口访问不同的文件系统。VFS维护着一个统一的文件系统树，内核启动时会挂载根文件系统，其他文件系统可以挂载在文件系统树的目录下。

+ 根据解析得到的文件系统类型信息，加载相应的文件系统驱动，比如FAT32、NTFS、EXT4等文件系统的驱动。
  
+ 根据文件系统的元数据，将文件系统挂载到指定的挂载点，即VFS的文件系统树的某个目录下。

### 可能的改进

在 `naive_fs_pwrite` 函数中，是从 `offset` 所在的块开始，读取块的内容，写入数据，再将块写回SD卡。这里可以对读取做出优化，对于那些会被整块写，或者因文件变大而刚刚被分配的块，不需要读出来，直接覆盖即可，可以省去一次读取操作。

还可以针对inode的大小进行优化，现在的设计为了方便，将inode的大小设置为一个块的大小，这样可以支持大文件，但如果都是小文件，会造成空间的浪费。小文件很多的时候，也可能会不够用。可以设计不同的inode尺寸，根据实际需求动态调整和分配，在支持的文件大小和空间利用率上做出权衡。