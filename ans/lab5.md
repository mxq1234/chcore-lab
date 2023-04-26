### 思考题1

好处：(1) 相比于数组，使用radix树组织文件的数据块可以节省空间。当文件大小发生变化时，数组需要预留足够的空间存放数据块的块号(内存页地址)；而radix树可以动态伸缩，适应数据块数量的变化。(2) 相比于链表，使用radix树可以支持快速的随机访问。链表无法通过索引快速找到想要随机访问的数据块，只能顺序遍历；而radix树可以通过数据块的顺序编号，利用索引在常数时间内找到相应的数据块。

其他的存储方式：数组、链表、基于比较的查找树、哈希表

### 练习题2

`tfs_mknod`：利用 `tfs_lookup` 函数查找要创建的文件或目录是否已经存在，如果不存在，调用 `new_dir` 或 `new_reg` 函数创建目录或者普通文件的inode，再调用 `new_dent` 函数创建相应的目录项，将目录项加入父目录的哈希表中。

`tfs_namex`：以'/'分割文件路径，通过循环遍历逐级目录，调用 `tfs_lookup` 函数在当前目录下查找下一级目录，如果目录不存在，且mkdir_p为true，则创建该目录，继续向下查找，否则返回-ENOENT。

### 练习题3

`tfs_file_read`：通过offset可以计算出初始的page_no和page_off，循环调用 `radix_get` 函数，通过page_no获取文件的数据块，将数据块中的内容通过 `memcpy` 附加到buf尾部，直到size bytes的数据被读取，或者读到文件的末尾。

`tfs_file_write`：同上，但是 `radix_get` 函数可能返回NULL，说明数据块不存在，要写入的数据超过了文件大小。调用 `calloc` 函数分配新的数据块，并通过 `radix_add` 函数添加到inode的radix树中。如果写入的数据范围超过文件大小，则更新文件大小。

### 练习题4

`tfs_load_image`：调用 `tfs_namex` 和 `tfs_mknod` 函数在tmpfs中创建新文件，通过 `tfs_lookup` 得到新文件的目录项，调用 `tfs_file_write` 函数向新文件中写入ELF文件的二进制数据。

### 练习题5

`fs_creat`：调用 `tfs_namex` 找到要创建的文件所在的目录，如果中间目录不存在，则应当创建。调用 `tfs_creat` 函数在目录下创建文件。

`tmpfs_unlink`：调用 `tfs_namex` 找到要创建的文件所在的目录，无需创建中间的缺失目录。调用 `tfs_remove` 函数删除对应的文件。

`tmpfs_mkdir`：同 `fs_creat`。调用 `tfs_mkdir` 函数在目录下创建目录。

### 练习题6

`fopen`：如果mode为"r"，则flags为O_RDONLY; 如果mode为"w"，则flags为O_WRONLY。接着调用 `open(filename, flags)`，返回新创建文件的fd。用calloc分配FILE结构体，填入fd和flags，返回FILE*。`open` 函数分配好fd，通过 `ipc_call` 向文件服务发起打开文件的请求。修改 `tmpfs_open` 函数，对于O_RDONLY的请求，如果文件不存在，返回-ENOENT。对于O_WDONLY的请求，如果文件不存在，则创建文件，如果文件存在，清空文件。

`fwrite`：如果文件是只读的，直接返回0。新建ipc_msg，数据区的大小为fs_request的大小加上size * nmemb，将buf中要写入的数据拷贝到共享内存，通过 `ipc_call` 向文件服务发起写入文件的请求。

`fread`：如果文件是只写的，直接返回0。新建ipc_msg，数据区的大小为fs_request的大小加上size * nmemb，通过 `ipc_call` 向文件服务发起读文件的请求，将共享内存中读到的数据拷贝到buf。

`fclose`：调用 `close(f->fd)` 关闭文件。`close` 函数通过 `ipc_call` 向文件服务发起关闭文件的请求。

`fscanf`：调用 `fread` 函数将字符串读入buf，同时遍历fmt和buf字符串，如果fmt中出现%s，将buf中当前位置往后的字符串写入相应的char数组参数中，直到遇到空格为止。如果出现%d，将buf中当前位置往后的字符串通过 `atoi` 函数转化为整数，写入相应的int参数中。如果是普通的字符，则要求fmt和buf的元素一一对应。

`fprintf`：同理，使用 `itoa` 函数将整数转成字符串，最后调用 `fwrite` 函数将buf中的字符串写入文件。

### 练习题7

`getch`：调用 `__chcore_sys_getc` 函数。

`readline`：对于普通的可打印字符，用 `printf("%c)` 将其打印到屏幕上，并附加到buf尾部。对于换行符'\r'和'\n'，打印出'\n'，并在buf尾部附加'\0'，终止buf字符串。

### 练习题8

`ls [dir]`：`fs_scan` 函数调用 `open` 函数打开文件，得到文件的fd，调用 `getdents` 通过 `ipc_call` 获取到目录下所有文件和子目录的目录项，循环调用 `get_dent_name` 函数，获取文件名或子目录名，如果不是"."或".."，输出文件名。最后调用 `close` 函数关闭文件。

`echo [string]`：通过 `printf` 输出。

`cat [filename]`：`print_file_content` 函数调用 `fopen` 函数打开文件，调用 `fread` 函数读取文件的所有内容，通过 `printf` 输出。最后调用 `fclose` 函数关闭文件。

### 练习题9

`run_cmd`：通过 `chcore_procm_spawn(cmdline, &cap)` 创建新进程，运行cmdline指定的二进制文件。

`do_complement`：类似于 `fs_scan` 函数，只不过每次多提示一个，所以只有第 `complement_time` 个文件名或者目录名才会被输出。

`readline`：对于'\t'，增加 `complement_time`，调用 `do_complement` 函数打印出下一个提示的文件名。

### 练习题10

`FS_REQ_OPEN`：调用 `get_mount_point` 函数，通过文件路径识别出具体的文件系统，得到相应文件系统的mpinfo。调用 `strip_path` 函数，将路径转化成具体文件系统中的路径。在FSM和具体文件服务之间创建新的IPC，复制原来IPC的消息内容，打开文件。最后调用 `fsm_set_mount_info_withfd` 将mpinfo加入链表，以便之后用fd请求时可以正确找到具体的文件系统。

`FS_REQ_READ`：调用 `fsm_get_mount_info_withfd` 函数得到打开文件时放入链表中的mpinfo，在FSM和具体文件服务之间创建新的IPC，复制原来IPC的消息内容，读取文件。将新的IPC的msg中读到的数据写入旧的IPC的msg中。

`FS_REQ_WRITE`：与 `FS_REQ_READ` 类似。只是需要将旧的IPC msg的数据写入新的IPC msg的数据区。

其他的都与上述的类似。