#include <string.h>
#include <stdio.h>

#include "file_ops.h"
#include "block_layer.h"

#define FILE_BLOCK_NUM (((BLOCK_SIZE) / sizeof(int)) - 1)
#define SUPER_BLOCK 0
#define BITMAP_BLOCK 1
#define ROOT_INODE_BLOCK 2
#define FIRST_INODE_BLOCK 3
#define FIRST_DATA_BLOCK 256
#define BLOCK_NUM 4096
#define SET_BIT(map, i) ((map)[(i) / 8] |= (1 << ((i) % 8)))
#define CLEAR_BIT(map, i) ((map)[(i) / 8] &= ~(1 << ((i) % 8)))
#define GET_BIT(map, i) ((map)[(i) / 8] & (1 << ((i) % 8)))
#define FIND_NEXT_ZERO(map, i, limit) \
    do { \
        while ((i) < (limit) && GET_BIT(map, i)) { \
            i++; \
        } \
    } while (0)
#define FILENAME_LEN 28
#define ENTRY_NUM_PER_BLOCK (BLOCK_SIZE / sizeof(struct direntry))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct superblock {
    char magic[8];
    int block_num;
    int inode_num;
    int block_size;
    int inode_size;
    int inode_table_block;
    int root_inode_block;
    int bitmap_block;
    int data_block;
};

struct bitmap {
    char map[BLOCK_SIZE];
};

struct inode {
    int size;
    int blocks[FILE_BLOCK_NUM];
};

struct direntry {
    char name[FILENAME_LEN];
    int inode_block;
};

int naive_fs_init() {
    char buf[BLOCK_SIZE];

    struct superblock *sb = (struct superblock *)buf;
    memset(buf, 0, BLOCK_SIZE);
    strcpy(sb->magic, "naivefs");
    sb->block_num = BLOCK_NUM;
    sb->inode_num = FIRST_DATA_BLOCK - FIRST_INODE_BLOCK;
    sb->block_size = BLOCK_SIZE;
    sb->inode_size = sizeof(struct inode);
    sb->inode_table_block = FIRST_INODE_BLOCK;
    sb->root_inode_block = ROOT_INODE_BLOCK;
    sb->bitmap_block = BITMAP_BLOCK;
    sb->data_block = FIRST_DATA_BLOCK;
    if(sd_bwrite(SUPER_BLOCK, buf) != 0) {
        printf("Write superblock failed\n");
        return -1;
    }

    struct bitmap *bitmap = (struct bitmap *)buf;
    memset(buf, 0, BLOCK_SIZE);
    SET_BIT(bitmap->map, SUPER_BLOCK);
    SET_BIT(bitmap->map, BITMAP_BLOCK);
    SET_BIT(bitmap->map, ROOT_INODE_BLOCK);
    if(sd_bwrite(BITMAP_BLOCK, buf) != 0) {
        printf("Write bitmap failed\n");
        return -1;
    }
    
    struct inode *inode = (struct inode *)buf;
    memset(buf, 0, BLOCK_SIZE);
    inode->size = 0;
    if(sd_bwrite(ROOT_INODE_BLOCK, buf) != 0) {
        printf("Write root inode failed\n");
        return -1;
    }
    return 0;
}

int naive_fs_check_init() {
    char buf[BLOCK_SIZE];
    struct superblock *sb = (struct superblock *)buf;
    if(sd_bread(SUPER_BLOCK, buf) != 0) {
        printf("Read superblock failed\n");
        return -1;
    }
    if (strcmp(sb->magic, "naivefs") != 0) {
        return -1;
    }
    return 0;
}

int naive_fs_alloc_inode() {
    int i = FIRST_INODE_BLOCK;
    struct bitmap bitmap;
    if(sd_bread(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Read bitmap failed\n");
        return -1;
    }
    FIND_NEXT_ZERO(bitmap.map, i, FIRST_DATA_BLOCK);
    if (i >= FIRST_DATA_BLOCK) {
        printf("No inode for new file\n");
        return -1;
    }
    SET_BIT(bitmap.map, i);
    if(sd_bwrite(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Write bitmap failed\n");
        return -1;
    }
    return i;
}

int naive_fs_free_inode(int inode_block) {
    struct bitmap bitmap;
    if(sd_bread(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Read bitmap failed\n");
        return -1;
    }
    CLEAR_BIT(bitmap.map, inode_block);
    if(sd_bwrite(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Write bitmap failed\n");
        return -1;
    }
    return 0;
}

int naive_fs_alloc_data_block() {
    int i = FIRST_DATA_BLOCK;
    struct bitmap bitmap;
    if(sd_bread(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Read bitmap failed\n");
        return -1;
    }
    FIND_NEXT_ZERO(bitmap.map, i, BLOCK_NUM);
    if (i >= BLOCK_NUM) {
        printf("No data block for new file\n");
        return -1;
    }
    SET_BIT(bitmap.map, i);
    if(sd_bwrite(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Write bitmap failed\n");
        return -1;
    }
    return i;
}

int naive_fs_free_data_block(int block_index) {
    struct bitmap bitmap;
    if(sd_bread(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Read bitmap failed\n");
        return -1;
    }
    if (block_index < FIRST_DATA_BLOCK || block_index >= BLOCK_NUM) {
        printf("Invalid block index\n");
        return -1;
    }
    CLEAR_BIT(bitmap.map, block_index);
    if(sd_bwrite(BITMAP_BLOCK, (char*)&bitmap) != 0) {
        printf("Write bitmap failed\n");
        return -1;
    }
    return 0;
}

int naive_fs_append_direntry(const char *name, int inode_block)
{
    struct inode root_inode;
    if(sd_bread(ROOT_INODE_BLOCK, (char*)&root_inode) != 0) {
        printf("Read root inode failed\n");
        return -1;
    }

    struct direntry direntry;
    memset(&direntry, 0, sizeof(struct direntry));
    strcpy(direntry.name, name);
    direntry.inode_block = inode_block;

    int direntry_num = root_inode.size / sizeof(struct direntry);
    int block_index = direntry_num / ENTRY_NUM_PER_BLOCK;
    int entry_offset = direntry_num % ENTRY_NUM_PER_BLOCK;

    if(entry_offset == 0) {
        int new_block = naive_fs_alloc_data_block();
        if(new_block < 0) {
            printf("Alloc new block failed\n");
            return -1;
        }
        root_inode.blocks[block_index] = new_block;
    }

    char buf[BLOCK_SIZE];
    if(sd_bread(root_inode.blocks[block_index], buf) != 0) {
        printf("Read root inode block %d failed\n", block_index);
        return -1;
    }
    memcpy(buf + entry_offset * sizeof(struct direntry), (char*)&direntry, sizeof(struct direntry));
    root_inode.size += sizeof(struct direntry);
    if(sd_bwrite(root_inode.blocks[block_index], buf) != 0) {
        printf("Write root inode block %d failed\n", block_index);
        return -1;
    }
    if(sd_bwrite(ROOT_INODE_BLOCK, (char*)&root_inode) != 0) {
        printf("Write root inode failed\n");
        return -1;
    }
    return 0;
}

int naive_fs_remove_direntry(const char *name)
{
    struct inode root_inode;
    if(sd_bread(ROOT_INODE_BLOCK, (char*)&root_inode) != 0) {
        printf("Read root inode failed\n");
        return -1;
    }

    char buf[BLOCK_SIZE];
    int direntry_num = root_inode.size / sizeof(struct direntry);
    for(int i = 0; i < direntry_num; i++) {
        int block_index = i / ENTRY_NUM_PER_BLOCK;
        int entry_offset = i % ENTRY_NUM_PER_BLOCK;
        
        if(entry_offset == 0) {
            if(sd_bread(root_inode.blocks[block_index], buf) != 0) {
                printf("Read root inode block %d failed\n", block_index);
                return -1;
            }
        }
        struct direntry *direntry = (struct direntry *)(buf + entry_offset * sizeof(struct direntry));
        if(strcmp(direntry->name, name) == 0) {
            int last_direntry_index = (root_inode.size - sizeof(struct direntry)) / sizeof(struct direntry);
            int last_block_index = last_direntry_index / ENTRY_NUM_PER_BLOCK;
            int last_entry_offset = last_direntry_index % ENTRY_NUM_PER_BLOCK;

            char tmp[BLOCK_SIZE];
            if(sd_bread(root_inode.blocks[last_block_index], tmp) != 0) {
                printf("Read root inode block %d failed\n", last_block_index);
                return -1;
            }
            struct direntry *last_direntry = (struct direntry *)(tmp + last_entry_offset * sizeof(struct direntry));
            memcpy((char*)direntry, (char*)last_direntry, sizeof(struct direntry));
            
            root_inode.size -= sizeof(struct direntry);
            if(root_inode.size % BLOCK_SIZE == 0) {
                if(naive_fs_free_data_block(root_inode.blocks[last_block_index]) != 0) {
                    printf("Free root inode block %d failed\n", last_block_index);
                    return -1;
                }
                root_inode.blocks[last_block_index] = 0;
            }

            if(sd_bwrite(root_inode.blocks[block_index], buf) != 0) {
                printf("Write root inode block %d failed\n", block_index);
                return -1;
            }
            if(sd_bwrite(ROOT_INODE_BLOCK, (char*)&root_inode) != 0) {
                printf("Write root inode failed\n");
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

int naive_fs_lookup(const char* name) {
    struct inode root_inode;
    if(sd_bread(ROOT_INODE_BLOCK, (char*)&root_inode) != 0) {
        printf("Read root inode failed\n");
        return -1;
    }

    char buf[BLOCK_SIZE];
    int direntry_num = root_inode.size / sizeof(struct direntry);
    for(int i = 0; i < direntry_num; i++) {
        int block_index = i / ENTRY_NUM_PER_BLOCK;
        int entry_offset = i % ENTRY_NUM_PER_BLOCK;
        
        if(entry_offset == 0) {
            if(sd_bread(root_inode.blocks[block_index], buf) != 0) {
                printf("Read root inode block %d failed\n", block_index);
                return -1;
            }
        }
        struct direntry *direntry = (struct direntry *)(buf + entry_offset * sizeof(struct direntry));
        if(strcmp(direntry->name, name) == 0) {
            return direntry->inode_block;
        }
    }
    return -1;
}

int naive_fs_access(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(naive_fs_check_init() != 0) {
        if(naive_fs_init() != 0) {
            printf("Init file system failed\n");
            return -1;
        }
    }

    if(strlen(name) > FILENAME_LEN - 1) {
        printf("File name too long\n");
        return -1;
    }

    int inode_block = naive_fs_lookup(name);
    if(inode_block < 0) {
        printf("File not found\n");
        return -1;
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return 0;
}

int naive_fs_creat(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(strlen(name) > FILENAME_LEN - 1) {
        printf("File name too long\n");
        return -1;
    }

    int new_inode = naive_fs_alloc_inode();
    if(new_inode < 0) {
        printf("Alloc inode failed\n");
        return -1;
    }

    if(naive_fs_lookup(name) >= 0) {
        printf("File already exists\n");
        return -1;
    }
    
    if(naive_fs_append_direntry(name, new_inode) != 0) {
        printf("Append direntry failed\n");
        return -1;
    }

    struct inode inode;
    memset(&inode, 0, sizeof(struct inode));
    inode.size = 0;
    if(sd_bwrite(new_inode, (char*)&inode) != 0) {
        printf("Write inode failed\n");
        return -1;
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return 0;
}

int naive_fs_pread(const char *name, int offset, int size, char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(strlen(name) > FILENAME_LEN - 1) {
        printf("File name too long\n");
        return -1;
    }

    if(offset < 0 || size < 0) {
        printf("Read offset or size less than 0\n");
        return -1;
    }

    int inode_block = naive_fs_lookup(name);
    if(inode_block < 0) {
        printf("File not found\n");
        return -1;
    }

    struct inode inode;
    if(sd_bread(inode_block, (char*)&inode) != 0) {
        printf("Read inode failed\n");
        return -1;
    }

    if(offset > inode.size) {
        printf("Read offset too large\n");
        return 0;
    }

    size = MIN(size, inode.size - offset);
    int block_index = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    int read_size = 0;
    char buf[BLOCK_SIZE];
    while(read_size < size) {
        if(sd_bread(inode.blocks[block_index], buf) != 0) {
            printf("Read inode block %d failed\n", block_index);
            return read_size;
        }
        int copy_size = MIN(size - read_size, BLOCK_SIZE - block_offset);
        memcpy(buffer + read_size, buf + block_offset, copy_size);
        read_size += copy_size;
        block_index++;
        block_offset = 0;
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return read_size;
}

int naive_fs_pwrite(const char *name, int offset, int size, const char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(strlen(name) > FILENAME_LEN - 1) {
        printf("File name too long\n");
        return -1;
    }

    if(offset < 0 || size < 0) {
        printf("Write offset or size less than 0\n");
        return -1;
    }

    int inode_block = naive_fs_lookup(name);
    if(inode_block < 0) {
        printf("File not found\n");
        return -1;
    }

    struct inode inode;
    if(sd_bread(inode_block, (char*)&inode) != 0) {
        printf("Read inode failed\n");
        return -1;
    }

    if(offset > inode.size) {
        printf("Write offset too large\n");
        return 0;
    }

    int block_index = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    int write_size = 0;
    char buf[BLOCK_SIZE];
    while(write_size < size) {
        if(block_index > inode.size / BLOCK_SIZE) {
            int new_data_block = naive_fs_alloc_data_block();
            if(new_data_block < 0) {
                printf("Alloc data block failed\n");
                return write_size;
            }
            inode.blocks[block_index] = new_data_block;
        }

        if(sd_bread(inode.blocks[block_index], buf) != 0) {
            printf("Read inode block %d failed\n", block_index);
            return write_size;
        }
        int copy_size = MIN(size - write_size, BLOCK_SIZE - block_offset);
        memcpy(buf + block_offset, buffer + write_size, copy_size);
        if(sd_bwrite(inode.blocks[block_index], buf) != 0) {
            printf("Write inode block %d failed\n", block_index);
            return write_size;
        }
        write_size += copy_size;
        block_index++;
        block_offset = 0;
    }

    inode.size = MAX(inode.size, offset + size);
    if(sd_bwrite(inode_block, (char*)&inode) != 0) {
        printf("Write inode failed\n");
        return -1;
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return write_size;
}

int naive_fs_unlink(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(strlen(name) > FILENAME_LEN - 1) {
        printf("File name too long\n");
        return -1;
    }

    int inode_block = naive_fs_lookup(name);
    if(inode_block < 0) {
        printf("File not found\n");
        return -1;
    }

    struct inode inode;
    if(sd_bread(inode_block, (char*)&inode) != 0) {
        printf("Read inode failed\n");
        return -1;
    }

    for(int i = 0; i < inode.size / BLOCK_SIZE; i++) {
        if(naive_fs_free_data_block(inode.blocks[i]) != 0) {
            printf("Free block %d failed\n", inode.blocks[i]);
            return -1;
        }
    }

    if(naive_fs_free_inode(inode_block) != 0) {
        printf("Free inode %d failed\n", inode_block);
        return -1;
    }

    if(naive_fs_remove_direntry(name) != 0) {
        printf("Remove direntry %s failed\n", name);
        return -1;
    }

    /* BLANK END */
    /* LAB 6 TODO END */
    return 0;
}
