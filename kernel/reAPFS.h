
#ifndef REAPFS_H
#define REAPFS_H

#include <stdint.h>
#include <stddef.h>

/* Simple reAPFS on-disk layout constants */
#define REAPFS_MAGIC 0x52415046u /* 'RAPF' */
#define REAPFS_BLOCK_SIZE 4096
#define REAPFS_MAX_INODES 1024
#define REAPFS_MAX_NAME 64
#define REAPFS_MAX_FD 16

typedef uint32_t reapfs_inode_t;
typedef int32_t reapfs_fd_t;

/* on-disk superblock */
struct reapfs_super {
    uint32_t magic;
    uint32_t version;
    uint64_t disk_size_bytes;
    uint32_t block_size;
    uint32_t inode_table_blocks;
    uint32_t data_start_block;
    uint8_t reserved[32];
};

/* simple inode on-disk */
struct reapfs_inode {
    reapfs_inode_t id;
    uint16_t mode; /* dir/file */
    uint16_t link_count;
    uint64_t size;
    uint32_t direct_blocks[6];
    reapfs_inode_t parent;
    char name[REAPFS_MAX_NAME];
    uint8_t reserved[32];
};

struct reapfs_dir_entry {
    reapfs_inode_t inode;
    char name[REAPFS_MAX_NAME];
};

/* Disk IO using ATA - provided by kernel */
int reapfs_disk_read(void *buf, uint64_t offset, size_t len);
int reapfs_disk_write(const void *buf, uint64_t offset, size_t len);

/* High level FS API used by the OS */
int fs_init(void);
int fs_mkdir(const char *path);
int fs_create(const char *path);
reapfs_fd_t fs_open(const char *path, int write);
int fs_close(reapfs_fd_t fd);
int fs_write(reapfs_fd_t fd, const void *buf, size_t len);
int fs_read(reapfs_fd_t fd, void *buf, size_t len);
int fs_ls(const char *path, char *out_buf, size_t out_sz);
int fs_remove(const char *path);
void fs_debug_print(void);

/* Status codes */
#define FS_OK 0
#define FS_ERR -1

#endif
