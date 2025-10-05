
/*
 * reAPFS.c
 * Minimal reAPFS implementation built on top of existing ATA functions.
 * - Uses ATA sector I/O (512B sectors) via reapfs_disk_{read,write} wrappers.
 * - Very small inode table and simple directory entries.
 *
 * This implementation aims to be compatible with the kernel project's ATA layer.
 * It's intentionally simple to be safe to integrate and easy to extend.
 */

#include "reAPFS.h"
#include "fs.h"
#include "ata.h"
#include "utils.h"
#include "screen.h"
#include <string.h>
#include <stdio.h>

/* Helpers to convert byte offsets to ATA LBAs (512-byte sectors) */
static int disk_read_bytes(void *buf, uint64_t offset, size_t len) {
    /* ATA API reads sectors (512B) by LBA. We'll round to sector boundaries. */
    const uint64_t SECTOR = 512;
    uint64_t start_sector = offset / SECTOR;
    uint64_t end_sector = (offset + len + SECTOR - 1) / SECTOR;
    uint32_t count = (uint32_t)(end_sector - start_sector);
    uint8_t *tmp = (uint8_t*)buf;
    /* If len not multiple of 512, we must read full sectors into a temp buffer.
       For simplicity allocate a temporary buffer for the full sectors. */
    if ((len % SECTOR) != 0) {
        uint64_t total = count * SECTOR;
        uint8_t *buf2 = malloc(total);
        if (!buf2) return -1;
        if (ata_read((uint32_t)start_sector, buf2, count) != 0) { free(buf2); return -1; }
        uint64_t off_in_first = offset - start_sector*SECTOR;
        memcpy(buf, buf2 + off_in_first, len);
        free(buf2);
        return 0;
    } else {
        /* direct read into buffer */
        if (ata_read((uint32_t)start_sector, tmp, count) != 0) return -1;
        return 0;
    }
}
static int disk_write_bytes(const void *buf, uint64_t offset, size_t len) {
    const uint64_t SECTOR = 512;
    uint64_t start_sector = offset / SECTOR;
    uint64_t end_sector = (offset + len + SECTOR - 1) / SECTOR;
    uint32_t count = (uint32_t)(end_sector - start_sector);
    if ((len % SECTOR) != 0) {
        uint64_t total = count * SECTOR;
        uint8_t *buf2 = malloc(total);
        if (!buf2) return -1;
        /* read-modify-write the sectors we will overwrite */
        if (ata_read((uint32_t)start_sector, buf2, count) != 0) { free(buf2); return -1; }
        uint64_t off_in_first = offset - start_sector*SECTOR;
        memcpy(buf2 + off_in_first, buf, len);
        if (ata_write((uint32_t)start_sector, buf2, count) != 0) { free(buf2); return -1; }
        free(buf2);
        return 0;
    } else {
        if (ata_write((uint32_t)start_sector, (const uint8_t*)buf, count) != 0) return -1;
        return 0;
    }
}

/* Implement reapfs_disk wrappers expected by header */
int reapfs_disk_read(void *buf, uint64_t offset, size_t len) { return disk_read_bytes(buf, offset, len) == 0 ? 0 : -1; }
int reapfs_disk_write(const void *buf, uint64_t offset, size_t len) { return disk_write_bytes(buf, offset, len) == 0 ? 0 : -1; }

/* On-disk placement:
 * LBA 0..0 : boot/superblock area
 * Next blocks: inode table (fixed REAPFS_MAX_INODES inodes)
 * Data area starts at block computed from superblock.
 */

/* Read/Write superblock */
static struct reapfs_super g_super;

/* Simple in-memory inode cache */
static struct reapfs_inode inode_cache[REAPFS_MAX_INODES];
static uint8_t inode_used[REAPFS_MAX_INODES];

/* Open file descriptor table */
struct fd_entry {
    int used;
    reapfs_inode_t inode;
    uint32_t pos;
    int write;
} fd_table[REAPFS_MAX_FD];

static uint64_t block_to_offset(uint32_t block) {
    return (uint64_t)block * REAPFS_BLOCK_SIZE;
}

static int load_super(void) {
    if (reapfs_disk_read(&g_super, 0, sizeof(g_super)) != 0) return -1;
    if (g_super.magic != REAPFS_MAGIC) {
        /* Assume unformatted - create a default superblock */
        memset(&g_super,0,sizeof(g_super));
        g_super.magic = REAPFS_MAGIC;
        g_super.version = 1;
        g_super.block_size = REAPFS_BLOCK_SIZE;
        g_super.inode_table_blocks = 8; /* small */
        g_super.data_start_block = 1 + g_super.inode_table_blocks;
        /* disk_size_bytes unknown here; we don't need it for basic ops */
        if (reapfs_disk_write(&g_super, 0, sizeof(g_super)) != 0) return -1;
    }
    return 0;
}

static int load_inodes(void) {
    size_t ino_sz = sizeof(struct reapfs_inode) * REAPFS_MAX_INODES;
    uint64_t off = block_to_offset(1); /* inode table at block 1 */
    if (reapfs_disk_read(inode_cache, off, ino_sz) != 0) return -1;
    /* mark used by id */
    for (int i=0;i<REAPFS_MAX_INODES;i++){
        inode_used[i] = (inode_cache[i].id != 0);
    }
    return 0;
}

static int flush_inodes(void) {
    size_t ino_sz = sizeof(struct reapfs_inode) * REAPFS_MAX_INODES;
    uint64_t off = block_to_offset(1);
    if (reapfs_disk_write(inode_cache, off, ino_sz) != 0) return -1;
    return 0;
}

static reapfs_inode_t alloc_inode(void) {
    for (int i=0;i<REAPFS_MAX_INODES;i++){
        if (!inode_used[i]) {
            inode_used[i]=1;
            memset(&inode_cache[i],0,sizeof(inode_cache[i]));
            inode_cache[i].id = i+1;
            return i+1;
        }
    }
    return 0;
}

static struct reapfs_inode *get_inode(reapfs_inode_t id) {
    if (id==0 || id>REAPFS_MAX_INODES) return NULL;
    if (!inode_used[id-1]) return NULL;
    return &inode_cache[id-1];
}

/* Path helpers: only support simple absolute paths like /, /name, /dir/name */
static int split_path(const char *path, char *parent, char *name) {
    if (!path || path[0]!='/') return -1;
    if (strcmp(path,"/")==0) {
        strcpy(parent,"/");
        name[0]=0;
        return 0;
    }
    const char *p = strrchr(path,'/');
    if (!p) return -1;
    if (p==path) {
        strcpy(parent,"/");
    } else {
        size_t len = p - path;
        if (len >= 256) return -1;
        memcpy(parent, path, len);
        parent[len]=0;
    }
    strncpy(name, p+1, 255);
    return 0;
}

/* Find child in directory by name */
static reapfs_inode_t dir_lookup(reapfs_inode_t dir, const char *name) {
    struct reapfs_dir_entry de;
    if (!get_inode(dir) || !(get_inode(dir)->mode & 0x4000)) return 0;
    uint32_t db = get_inode(dir)->direct_blocks[0];
    if (db==0) return 0;
    uint64_t off = block_to_offset(g_super.data_start_block + db);
    /* read entries sequentially */
    size_t max_entries = REAPFS_BLOCK_SIZE / sizeof(de);
    for (size_t i=0;i<max_entries;i++){
        if (reapfs_disk_read(&de, off + i*sizeof(de), sizeof(de)) != 0) return 0;
        if (de.inode != 0 && strcmp(de.name, name)==0) return de.inode;
    }
    return 0;
}

/* Append dir entry to first direct block */
static int dir_add(reapfs_inode_t dir, reapfs_inode_t child, const char *name) {
    if (!get_inode(dir)) return -1;
    if (get_inode(dir)->direct_blocks[0]==0) {
        /* allocate first data block for dir */
        static uint32_t next_data_block = 1;
        get_inode(dir)->direct_blocks[0] = next_data_block++;
    }
    uint32_t db = get_inode(dir)->direct_blocks[0];
    uint64_t off = block_to_offset(g_super.data_start_block + db);
    struct reapfs_dir_entry de;
    size_t max_entries = REAPFS_BLOCK_SIZE / sizeof(de);
    for (size_t i=0;i<max_entries;i++){
        if (reapfs_disk_read(&de, off + i*sizeof(de), sizeof(de)) != 0) return -1;
        if (de.inode==0) {
            de.inode = child;
            strncpy(de.name, name, sizeof(de.name)-1);
            if (reapfs_disk_write(&de, off + i*sizeof(de), sizeof(de)) != 0) return -1;
            get_inode(dir)->size += sizeof(de);
            return 0;
        }
    }
    return -1;
}

/* Simple FS API implementations */

int fs_init(void) {
    memset(inode_used,0,sizeof(inode_used));
    memset(inode_cache,0,sizeof(inode_cache));
    memset(fd_table,0,sizeof(fd_table));
    if (load_super() != 0) return FS_ERR;
    if (load_inodes() != 0) {
        /* if inode table empty, initialize root */
        for (int i=0;i<REAPFS_MAX_INODES;i++) inode_used[i]=0;
        reapfs_inode_t r = alloc_inode();
        struct reapfs_inode *ri = get_inode(r);
        ri->mode = 0x4000;
        ri->link_count = 1;
        ri->parent = r;
        strncpy(ri->name,"/", sizeof(ri->name)-1);
        /* flush */
        flush_inodes();
        /* write super too */
        reapfs_disk_write(&g_super, 0, sizeof(g_super));
    }
    return FS_OK;
}

int fs_mkdir(const char *path) {
    char parent[256], name[256];
    if (split_path(path,parent,name)!=0) return FS_ERR;
    reapfs_inode_t parent_inode = 1;
    if (strcmp(parent,"/")!=0) return FS_ERR; /* only root supported */
    /* check exists */
    if (dir_lookup(parent_inode, name) != 0) return FS_ERR;
    reapfs_inode_t nid = alloc_inode();
    if (nid==0) return FS_ERR;
    struct reapfs_inode *ni = get_inode(nid);
    ni->mode = 0x4000;
    ni->parent = parent_inode;
    strncpy(ni->name,name,sizeof(ni->name)-1);
    if (dir_add(parent_inode, nid, name) != 0) return FS_ERR;
    flush_inodes();
    return FS_OK;
}

int fs_create(const char *path) {
    char parent[256], name[256];
    if (split_path(path,parent,name)!=0) return FS_ERR;
    reapfs_inode_t parent_inode = 1;
    if (dir_lookup(parent_inode, name) != 0) return FS_OK; /* already exists => success */
    reapfs_inode_t nid = alloc_inode();
    if (nid==0) return FS_ERR;
    struct reapfs_inode *ni = get_inode(nid);
    ni->mode = 0x8000;
    ni->parent = parent_inode;
    strncpy(ni->name,name,sizeof(ni->name)-1);
    if (dir_add(parent_inode, nid, name) != 0) return FS_ERR;
    flush_inodes();
    return FS_OK;
}

reapfs_fd_t fs_open(const char *path, int write) {
    char parent[256], name[256];
    if (split_path(path,parent,name)!=0) return -1;
    reapfs_inode_t inode = dir_lookup(1, name);
    if (inode==0) return -1;
    for (int i=0;i<REAPFS_MAX_FD;i++){
        if (!fd_table[i].used) {
            fd_table[i].used=1;
            fd_table[i].inode=inode;
            fd_table[i].pos=0;
            fd_table[i].write=write;
            return i;
        }
    }
    return -1;
}

int fs_close(reapfs_fd_t fd) {
    if (fd<0 || fd>=REAPFS_MAX_FD) return FS_ERR;
    fd_table[fd].used=0;
    return FS_OK;
}

int fs_write(reapfs_fd_t fd, const void *buf, size_t len) {
    if (fd<0 || fd>=REAPFS_MAX_FD) return FS_ERR;
    if (!fd_table[fd].used) return FS_ERR;
    struct reapfs_inode *in = get_inode(fd_table[fd].inode);
    if (!in) return FS_ERR;
    /* allocate data block if none */
    if (in->direct_blocks[0]==0) {
        static uint32_t next_data_block = 1;
        in->direct_blocks[0] = next_data_block++;
    }
    uint32_t db = in->direct_blocks[0];
    uint64_t off = block_to_offset(g_super.data_start_block + db) + fd_table[fd].pos;
    if (reapfs_disk_write(buf, off, len) != 0) return FS_ERR;
    fd_table[fd].pos += len;
    if (fd_table[fd].pos > in->size) in->size = fd_table[fd].pos;
    flush_inodes();
    return (int)len;
}

int fs_read(reapfs_fd_t fd, void *buf, size_t len) {
    if (fd<0 || fd>=REAPFS_MAX_FD) return FS_ERR;
    if (!fd_table[fd].used) return FS_ERR;
    struct reapfs_inode *in = get_inode(fd_table[fd].inode);
    if (!in) return FS_ERR;
    if (in->direct_blocks[0]==0) return 0;
    uint32_t db = in->direct_blocks[0];
    uint64_t off = block_to_offset(g_super.data_start_block + db) + fd_table[fd].pos;
    size_t toread = len;
    if (fd_table[fd].pos + toread > in->size) toread = (size_t)(in->size - fd_table[fd].pos);
    if (toread==0) return 0;
    if (reapfs_disk_read(buf, off, toread) != 0) return FS_ERR;
    fd_table[fd].pos += toread;
    return (int)toread;
}

int fs_ls(const char *path, char *out_buf, size_t out_sz) {
    (void)path;
    size_t used = 0;
    struct reapfs_dir_entry de;
    uint32_t db;
    struct reapfs_inode *root = get_inode(1);
    if (!root) return FS_ERR;
    db = root->direct_blocks[0];
    if (db==0) {
        snprintf(out_buf, out_sz, "/\n");
        return FS_OK;
    }
    uint64_t off = block_to_offset(g_super.data_start_block + db);
    size_t max_entries = REAPFS_BLOCK_SIZE / sizeof(de);
    used += snprintf(out_buf+used, out_sz-used, "/\n");
    for (size_t i=0;i<max_entries;i++){
        if (reapfs_disk_read(&de, off + i*sizeof(de), sizeof(de)) != 0) return FS_ERR;
        if (de.inode!=0) {
            struct reapfs_inode *ni = get_inode(de.inode);
            if (!ni) continue;
            used += snprintf(out_buf+used, out_sz-used, " %s\n", de.name);
        }
        if (used >= out_sz) break;
    }
    return FS_OK;
}

int fs_remove(const char *path) {
    char parent[256], name[256];
    if (split_path(path,parent,name)!=0) return FS_ERR;
    /* find in dir and zero entry */
    struct reapfs_dir_entry de;
    struct reapfs_inode *root = get_inode(1);
    if (!root) return FS_ERR;
    uint32_t db = root->direct_blocks[0];
    if (db==0) return FS_ERR;
    uint64_t off = block_to_offset(g_super.data_start_block + db);
    size_t max_entries = REAPFS_BLOCK_SIZE / sizeof(de);
    for (size_t i=0;i<max_entries;i++){
        if (reapfs_disk_read(&de, off + i*sizeof(de), sizeof(de)) != 0) return FS_ERR;
        if (de.inode!=0 && strcmp(de.name, name)==0) {
            memset(&de,0,sizeof(de));
            if (reapfs_disk_write(&de, off + i*sizeof(de), sizeof(de)) != 0) return FS_ERR;
            /* mark inode unused */
            struct reapfs_inode *ni = get_inode(de.inode);
            if (ni) {
                inode_used[de.inode-1]=0;
                memset(ni,0,sizeof(*ni));
                flush_inodes();
            }
            return FS_OK;
        }
    }
    return FS_ERR;
}

void fs_debug_print(void) {
    print_string("reAPFS debug:\n");
    print_string("Inodes:\n");
    for (int i=0;i<REAPFS_MAX_INODES;i++){
        if (inode_used[i]) {
            char buf[128];
            snprintf(buf,sizeof(buf)," id=%d name=%s size=%llu\n", inode_cache[i].id, inode_cache[i].name, (unsigned long long)inode_cache[i].size);
            print_string(buf);
        }
    }
}

