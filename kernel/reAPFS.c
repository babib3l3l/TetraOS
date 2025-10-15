/* reapfs.c  -- REAPFS hiérarchique (mise à jour)
 *
 * Modifications principales :
 * - chemins relatifs résolus par rapport au cwd interne au module.
 * - support complet de '.' et '..' (création et résolution).
 * - fonctions exportées : fs_chdir(path), fs_get_cwd()
 *
 * IMPORTANT:
 * - Ne définit PAS memcpy/memset/print_string/snprintf/ata_read/ata_write.
 * - Ce fichier remplace ton ancien reapfs.c
 */

#include <stdint.h>
#include <stddef.h>
#include "screen.h"
#include "input.h"
#include "reapfs.h"
#include "utils.h"
#include "io.h"
#include "ata.h"

/* Externs fournis par ton kernel : ne pas redéfinir */
extern void print_string(const char *s);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *ptr, int value, size_t num);
extern int snprintf(char *str, size_t size, const char *format, ...);

/* ---------- Configuration FS ---------- */
#define SECTOR_SIZE 512
#define MAX_INODES 256
#define INODE_TABLE_SECTORS 256
#define SUPERBLOCK_SECTOR 128
#define INODE_TABLE_START_SECTOR 129
#define MAX_FILENAME 32
#define MAX_DIR_ENTRIES 32
#define MAX_PATH 256

/* ---------- Structures ---------- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t inode_table_sectors;
    uint32_t inode_count;
    uint32_t data_start_sector;
    uint8_t reserved[SECTOR_SIZE - 20];
} reapfs_super_t;

typedef struct {
    uint32_t ino;
    uint32_t size; /* bytes: for file = file size, for dir = size of dirent table */
    uint32_t blocks[12];
    uint8_t used;
    uint8_t is_dir;
    char name[MAX_FILENAME];
} reapfs_inode_t;

typedef struct {
    char name[MAX_FILENAME];
    uint32_t ino;
} reapfs_dirent_t;

/* ---------- In-memory state ---------- */
static reapfs_super_t g_super;
static reapfs_inode_t g_inodes[MAX_INODES];
static uint8_t g_inode_used[MAX_INODES];

/* cwd interne au module : utilisé pour résoudre chemins relatifs */
static int g_cwd_ino = 0;
static char g_cwd_path[MAX_PATH] = "/";

/* ---------- Helpers disque (sans malloc) ---------- */

static int disk_read_bytes(void *buf, uint64_t offset, size_t len) {
    if (len == 0) return 0;
    uint64_t start = offset / SECTOR_SIZE;
    uint64_t end = (offset + (uint64_t)len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t count = (uint32_t)(end - start);
    uint8_t *out = (uint8_t*)buf;

    if ((offset % SECTOR_SIZE) == 0 && (len % SECTOR_SIZE) == 0) {
        if (ata_read((uint32_t)start, out, count) != 0) {
            print_string("ATA read failed (bulk)\n");
            return -1;
        }
        return 0;
    }

    uint8_t tmp[SECTOR_SIZE];
    uint64_t cur_off = offset;
    size_t remaining = len;

    for (uint32_t s = 0; s < count; ++s) {
        if (ata_read((uint32_t)(start + s), tmp, 1) != 0) {
            print_string("ATA read failed (partial)\n");
            return -1;
        }
        uint64_t sector_base = (uint64_t)(start + s) * SECTOR_SIZE;
        uint64_t copy_start = (cur_off > sector_base) ? (cur_off - sector_base) : 0;
        size_t copy_len = SECTOR_SIZE - copy_start;
        if (copy_len > remaining) copy_len = remaining;
        memcpy(out, tmp + copy_start, copy_len);
        out += copy_len;
        cur_off += copy_len;
        remaining -= copy_len;
        if (remaining == 0) break;
    }
    return 0;
}

static int disk_write_bytes(const void *buf, uint64_t offset, size_t len) {
    if (len == 0) return 0;
    uint64_t start = offset / SECTOR_SIZE;
    uint64_t end = (offset + (uint64_t)len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t count = (uint32_t)(end - start);
    const uint8_t *in = (const uint8_t*)buf;

    if ((offset % SECTOR_SIZE) == 0 && (len % SECTOR_SIZE) == 0) {
        if (ata_write((uint32_t)start, in, count) != 0) {
            print_string("ATA write failed (bulk)\n");
            return -1;
        }
        return 0;
    }

    uint8_t tmp[SECTOR_SIZE];
    uint64_t cur_off = offset;
    size_t remaining = len;

    for (uint32_t s = 0; s < count; ++s) {
        if (ata_read((uint32_t)(start + s), tmp, 1) != 0) {
            print_string("ATA read before write failed\n");
            return -1;
        }
        uint64_t sector_base = (uint64_t)(start + s) * SECTOR_SIZE;
        uint64_t copy_start = (cur_off > sector_base) ? (cur_off - sector_base) : 0;
        size_t copy_len = SECTOR_SIZE - copy_start;
        if (copy_len > remaining) copy_len = remaining;
        memcpy(tmp + copy_start, in, copy_len);
        if (ata_write((uint32_t)(start + s), tmp, 1) != 0) {
            print_string("ATA write failed (partial)\n");
            return -1;
        }
        in += copy_len;
        cur_off += copy_len;
        remaining -= copy_len;
        if (remaining == 0) break;
    }
    return 0;
}

/* ---------- Super / inode persistence ---------- */

static int reapfs_disk_read_wrapper(void *buf, uint64_t offset, size_t len) {
    return disk_read_bytes(buf, offset, len) == 0 ? 0 : -1;
}
static int reapfs_disk_write_wrapper(const void *buf, uint64_t offset, size_t len) {
    return disk_write_bytes(buf, offset, len) == 0 ? 0 : -1;
}

static int load_super(void) {
    uint8_t buf[SECTOR_SIZE];
    if (reapfs_disk_read_wrapper(buf, (uint64_t)SUPERBLOCK_SECTOR * SECTOR_SIZE, SECTOR_SIZE) != 0) {
        print_string("FS: super read failed\n");
        return -1;
    }
    memcpy(&g_super, buf, sizeof(reapfs_super_t));
    if (g_super.magic != 0x52455046) {
        print_string("FS: invalid magic\n");
        return -1;
    }
    if (g_super.inode_count > MAX_INODES) {
        print_string("FS: inode_count too large\n");
        return -1;
    }
    size_t it_size = (size_t)g_super.inode_count * sizeof(reapfs_inode_t);
    size_t it_sectors = (it_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (reapfs_disk_read_wrapper((uint8_t*)g_inodes, (uint64_t)INODE_TABLE_START_SECTOR * SECTOR_SIZE, it_sectors * SECTOR_SIZE) != 0) {
        print_string("FS: inode table read failed\n");
        return -1;
    }
    for (uint32_t i = 0; i < g_super.inode_count && i < MAX_INODES; ++i) {
        g_inode_used[i] = g_inodes[i].used ? 1 : 0;
    }
    return 0;
}

static int save_super(void) {
    uint8_t buf[SECTOR_SIZE];
    memset(buf, 0, SECTOR_SIZE);
    memcpy(buf, &g_super, sizeof(reapfs_super_t));
    if (reapfs_disk_write_wrapper(buf, (uint64_t)SUPERBLOCK_SECTOR * SECTOR_SIZE, SECTOR_SIZE) != 0) {
        print_string("FS: super write failed\n");
        return -1;
    }
    size_t it_size = (size_t)g_super.inode_count * sizeof(reapfs_inode_t);
    size_t it_sectors = (it_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (reapfs_disk_write_wrapper((uint8_t*)g_inodes, (uint64_t)INODE_TABLE_START_SECTOR * SECTOR_SIZE, it_sectors * SECTOR_SIZE) != 0) {
        print_string("FS: inode table write failed\n");
        return -1;
    }
    return 0;
}



/* ---------- Inode management ---------- */

static int alloc_inode(void) {
    for (int i = 0; i < (int)g_super.inode_count && i < MAX_INODES; ++i) {
        if (!g_inode_used[i]) {
            g_inode_used[i] = 1;
            memset(&g_inodes[i], 0, sizeof(reapfs_inode_t));
            g_inodes[i].ino = (uint32_t)i;
            g_inodes[i].used = 1;
            save_super();
            return i;
        }
    }
    return -1;
}

static void free_inode(uint32_t ino) {
    if (ino >= g_super.inode_count || ino >= MAX_INODES) return;
    g_inode_used[ino] = 0;
    memset(&g_inodes[ino], 0, sizeof(reapfs_inode_t));
    save_super();
}

/* ---------- File data IO (simple direct blocks) ---------- */

static int write_file_data(reapfs_inode_t *inode, const void *buf, uint32_t size) {
    if (!inode) return -1;
    uint32_t sectors_needed = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (sectors_needed > (uint32_t)(sizeof(inode->blocks)/sizeof(inode->blocks[0]))) {
        print_string("FS: file too large\n");
        return -1;
    }
    uint8_t sector_buf[SECTOR_SIZE];
    const uint8_t *in = (const uint8_t*)buf;
    for (uint32_t s = 0; s < sectors_needed; ++s) {
        uint32_t lba = g_super.data_start_sector + (inode->ino * 100) + s;
        inode->blocks[s] = lba;
        memset(sector_buf, 0, SECTOR_SIZE);
        size_t copy_len = SECTOR_SIZE;
        if (s == sectors_needed - 1) {
            uint32_t remain = size - (s * SECTOR_SIZE);
            if (remain < copy_len) copy_len = remain;
        }
        memcpy(sector_buf, in + s * SECTOR_SIZE, copy_len);
        if (ata_write(lba, sector_buf, 1) != 0) {
            print_string("FS: ata_write failed\n");
            return -1;
        }
    }
    /* if size == 0, we won't write any block but still set size and save */
    inode->size = size;
    save_super();
    return 0;
}
/* Inode après init de write file data */
static int format_super(uint32_t inode_count) {
    memset(&g_super, 0, sizeof(g_super));
    g_super.magic = 0x52455046;
    g_super.version = 1;
    g_super.inode_table_sectors = INODE_TABLE_SECTORS;
    g_super.inode_count = inode_count;
    g_super.data_start_sector = INODE_TABLE_START_SECTOR + g_super.inode_table_sectors;
    memset(g_inodes, 0, sizeof(g_inodes));
    memset(g_inode_used, 0, sizeof(g_inode_used));

    /* créer inode racine */
    g_inode_used[0] = 1;
    g_inodes[0].used = 1;
    g_inodes[0].is_dir = 1;
    g_inodes[0].ino = 0;
    g_inodes[0].size = 0;
    strncpy(g_inodes[0].name, "/", MAX_FILENAME - 1);
    g_inodes[0].name[MAX_FILENAME - 1] = '\0';

    /* Initialiser . et .. pour la racine (pointent vers lui-même) */
    {
        reapfs_dirent_t root_entries[2];
        strncpy(root_entries[0].name, ".", MAX_FILENAME - 1);
        root_entries[0].name[MAX_FILENAME - 1] = '\0';
        root_entries[0].ino = 0;
        strncpy(root_entries[1].name, "..", MAX_FILENAME - 1);
        root_entries[1].name[MAX_FILENAME - 1] = '\0';
        root_entries[1].ino = 0;
        write_file_data(&g_inodes[0], root_entries, (uint32_t)sizeof(root_entries));
        g_inodes[0].size = (uint32_t)sizeof(root_entries);
    }

    /* initial cwd */
    g_cwd_ino = 0;
    strncpy(g_cwd_path, "/", MAX_PATH - 1);
    g_cwd_path[MAX_PATH - 1] = '\0';

    if (save_super() != 0) return -1;
    print_string("FS: formatted new super\n");
    return 0;
}
static int read_file_data(reapfs_inode_t *inode, void *buf, uint32_t buf_size) {
    if (!inode) return -1;
    uint32_t to_read = inode->size;
    if (to_read == 0) return 0;
    if (buf_size < to_read) to_read = buf_size;
    uint32_t sectors = (to_read + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint8_t sector_buf[SECTOR_SIZE];
    for (uint32_t s = 0; s < sectors; ++s) {
        uint32_t lba = inode->blocks[s];
        if (ata_read(lba, sector_buf, 1) != 0) {
            print_string("FS: ata_read failed\n");
            return -1;
        }
        size_t copy_len = SECTOR_SIZE;
        if (s == sectors - 1) {
            uint32_t remain = to_read - (s * SECTOR_SIZE);
            if (remain < copy_len) copy_len = remain;
        }
        memcpy((uint8_t*)buf + s * SECTOR_SIZE, sector_buf, copy_len);
    }
    return (int)to_read;
}

/* ---------- Utility path helpers ---------- */

/* normalize_path_abs:
 * - convertit 'path_in' en chemin absolu nettoyé dans out[] (résolution de '.' et '..')
 * - si path_in commence par '/', on traite comme absolu ; sinon on l'interprète relatif à g_cwd_path.
 * - out buffer doit avoir taille out_sz (>= MAX_PATH).
 * - retourne 0 ou -1 si erreur.
 */
static int normalize_path_abs(const char *path_in, char *out, size_t out_sz) {
    if (!path_in || !out) return -1;

    char tmp[MAX_PATH * 2];
    tmp[0] = '\0';

    /* Déterminer base : cwd si relatif, sinon vide pour absolu */
    if (path_in[0] == '/') {
        /* start from root */
        strncpy(tmp, path_in, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';
    } else {
        /* prepend cwd (sans trailing slash sauf root) */
        if (strcmp(g_cwd_path, "/") == 0) {
            snprintf(tmp, sizeof(tmp), "/%s", path_in);
        } else {
            snprintf(tmp, sizeof(tmp), "%s/%s", g_cwd_path, path_in);
        }
    }

    /* collapse multiple slashes */
    char cleaned[MAX_PATH * 2];
    size_t ci = 0;
    for (size_t i = 0; tmp[i] != '\0' && ci + 1 < sizeof(cleaned); ++i) {
        if (i > 0 && tmp[i] == '/' && tmp[i-1] == '/') continue;
        cleaned[ci++] = tmp[i];
    }
    if (ci == 0) return -1;
    cleaned[ci] = '\0';

    /* split by '/' and process components, using a stack of parts */
    char parts[64][MAX_FILENAME];
    int top = 0;
    size_t i = 0;
    /* skip leading slash */
    if (cleaned[0] == '/') i = 1;
    while (cleaned[i]) {
        char seg[MAX_FILENAME];
        size_t si = 0;
        while (cleaned[i] && cleaned[i] != '/' && si + 1 < sizeof(seg)) seg[si++] = cleaned[i++];
        seg[si] = '\0';
        if (si == 0) { if (cleaned[i] == '/') ++i; continue; }
        if (strcmp(seg, ".") == 0) {
            /* skip */
        } else if (strcmp(seg, "..") == 0) {
            if (top > 0) --top;
            /* if top==0, we're at root, stay there */
        } else {
            if (top < (int)sizeof(parts)/sizeof(parts[0])) {
                strncpy(parts[top], seg, MAX_FILENAME-1);
                parts[top][MAX_FILENAME-1] = '\0';
                ++top;
            } else {
                return -1; /* chemin trop long */
            }
        }
        if (cleaned[i] == '/') ++i;
    }

    /* build output */
    if (top == 0) {
        /* root */
        if (out_sz < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    size_t pos = 0;
    for (int p = 0; p < top; ++p) {
        size_t need = strlen(parts[p]) + 1; /* '/' + part */
        if (pos + need + 1 > out_sz) return -1;
        out[pos++] = '/';
        size_t l = strlen(parts[p]);
        memcpy(out + pos, parts[p], l);
        pos += l;
    }
    out[pos] = '\0';
    return 0;
}

/* split_path: sépare path en parent (output) et name (output)
 * parent_out doit avoir taille >= MAX_PATH
 */
static int split_path(const char *path_in, char *parent_out, char *name_out) {
    if (!path_in || !parent_out || !name_out) return -1;
    char abs[MAX_PATH];
    if (normalize_path_abs(path_in, abs, sizeof(abs)) != 0) return -1;
    size_t len = strlen(abs);
    if (strcmp(abs, "/") == 0) {
        /* root has no parent/name */
        return -1;
    }
    const char *last = strrchr(abs, '/');
    if (!last) return -1;
    if (last == abs) {
        /* parent = "/", name = rest */
        strcpy(parent_out, "/");
        strncpy(name_out, last + 1, MAX_FILENAME - 1);
        name_out[MAX_FILENAME - 1] = '\0';
        return 0;
    }
    size_t plen = (size_t)(last - abs);
    if (plen >= MAX_PATH) return -1;
    strncpy(parent_out, abs, plen);
    parent_out[plen] = '\0';
    strncpy(name_out, last + 1, MAX_FILENAME - 1);
    name_out[MAX_FILENAME - 1] = '\0';
    return 0;
}

/* find_inode_by_path: parcours hiérarchique à partir de la racine
 * accepte chemins absolus ou relatifs (relatifs résolus via normalize_path_abs)
 */
static int find_inode_by_path(const char *path) {
    if (!path) return -1;
    char abs[MAX_PATH];
    if (normalize_path_abs(path, abs, sizeof(abs)) != 0) return -1;
    if (strcmp(abs, "/") == 0) return 0;

    const char *p = abs;
    if (p[0] == '/') p++;
    int current = 0; /* start at root */
    char part[MAX_FILENAME];

    while (*p) {
        size_t lenp = 0;
        while (*p && *p != '/' && lenp + 1 < sizeof(part)) part[lenp++] = *p++;
        part[lenp] = '\0';
        if (*p == '/') p++;

        if (!g_inodes[current].is_dir) return -1;

        reapfs_dirent_t entries[MAX_DIR_ENTRIES];
        int bytes = read_file_data(&g_inodes[current], entries, sizeof(entries));
        int count = bytes > 0 ? bytes / sizeof(reapfs_dirent_t) : 0;

        int found = 0;
        for (int i = 0; i < count; ++i) {
            if (strncmp(entries[i].name, part, MAX_FILENAME) == 0) {
                current = (int)entries[i].ino;
                found = 1;
                break;
            }
        }
        if (!found) return -1;
    }
    return current;
}

/* Ajoute une entrée dans un répertoire (parent_ino). Retourne 0 ou -1 */
static int dir_add_entry(uint32_t parent_ino, const char *name, uint32_t child_ino) {
    if (parent_ino >= MAX_INODES) return -1;
    if (!g_inodes[parent_ino].is_dir) return -1;

    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[parent_ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;
    if (count >= MAX_DIR_ENTRIES) return -1;

    strncpy(entries[count].name, name, MAX_FILENAME - 1);
    entries[count].name[MAX_FILENAME - 1] = '\0';
    entries[count].ino = child_ino;
    int nw = write_file_data(&g_inodes[parent_ino], entries, (uint32_t)((count + 1) * sizeof(reapfs_dirent_t)));
    return nw == 0 ? 0 : -1;
}

/* Retire une entrée d'un répertoire, retourne 0 ou -1 */
static int dir_remove_entry(uint32_t parent_ino, const char *name) {
    if (parent_ino >= MAX_INODES) return -1;
    if (!g_inodes[parent_ino].is_dir) return -1;

    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[parent_ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;
    int idx = -1;
    for (int i = 0; i < count; ++i) {
        if (strncmp(entries[i].name, name, MAX_FILENAME) == 0) { idx = i; break; }
    }
    if (idx < 0) return -1;
    for (int i = idx; i < count - 1; ++i) entries[i] = entries[i+1];
    if (count - 1 == 0) {
        if (write_file_data(&g_inodes[parent_ino], NULL, 0) != 0) return -1;
    } else {
        if (write_file_data(&g_inodes[parent_ino], entries, (uint32_t)((count - 1) * sizeof(reapfs_dirent_t))) != 0) return -1;
    }
    return 0;
}

/* ---------- API exposée attendue par main.c (adaptée pour chemins) ---------- */

int fs_init(void) {
    print_string("FS: start\n");
    memset(g_inode_used, 0, sizeof(g_inode_used));
    memset(g_inodes, 0, sizeof(g_inodes));
    if (load_super() == 0) {
        print_string("FS: load_super ok\n");
        /* ensure cwd valid */
        g_cwd_ino = 0;
        strncpy(g_cwd_path, "/", MAX_PATH-1);
        g_cwd_path[MAX_PATH-1] = '\0';
        return 0;
    }
    if (format_super(MAX_INODES) != 0) {
        print_string("FS: format failed\n");
        return -1;
    }
    print_string("FS: formatted and initialized\n");
    return 0;
}

/* Create file given a path (creates inode + ajoute au parent) */
int fs_create(const char *path) {
    char parent[256];
    char name[MAX_FILENAME];
    if (split_path(path, parent, name) != 0) return -1;

    int parent_ino = find_inode_by_path(parent);
    if (parent_ino < 0 || !g_inodes[parent_ino].is_dir) return -1;

    /* vérifier qu'il n'existe pas déjà */
    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[parent_ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;
    for (int i = 0; i < count; ++i) {
        if (strncmp(entries[i].name, name, MAX_FILENAME) == 0) return -1; /* exists */
    }

    int ino = alloc_inode();
    if (ino < 0) return -1;

    reapfs_inode_t *node = &g_inodes[ino];
    node->is_dir = 0;
    strncpy(node->name, name, MAX_FILENAME - 1);
    node->name[MAX_FILENAME - 1] = '\0';
    node->size = 0;

    if (dir_add_entry((uint32_t)parent_ino, name, (uint32_t)ino) != 0) {
        free_inode((uint32_t)ino);
        return -1;
    }

    save_super();
    return ino;
}

/* Open file by path -> returns inode number as "fd" or -1 */
reapfs_fd_t fs_open(const char *path, int write) {
    (void)write;
    if (!path) return -1;
    return find_inode_by_path(path);
}

/* Write to file by "fd" (inode number). Returns bytes written or -1 */
int fs_write(int fd, const void *buf, uint32_t size) {
    if (fd < 0 || (uint32_t)fd >= g_super.inode_count) return -1;
    if (!g_inode_used[fd]) return -1;
    return write_file_data(&g_inodes[fd], buf, size) == 0 ? (int)size : -1;
}

/* Read from file by "fd" (inode number). Returns bytes read or -1 */
int fs_read(int fd, void *buf, uint32_t buf_size) {
    if (fd < 0 || (uint32_t)fd >= g_super.inode_count) return -1;
    if (!g_inode_used[fd]) return -1;
    return read_file_data(&g_inodes[fd], buf, buf_size);
}

/* Close (noop) */
void fs_close(reapfs_fd_t fd) {
    (void)fd;
}

/* Remove file or empty directory by path */
int fs_remove(const char *path) {
    char parent[256];
    char name[MAX_FILENAME];
    if (split_path(path, parent, name) != 0) return -1;
    int parent_ino = find_inode_by_path(parent);
    if (parent_ino < 0) return -1;

    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[parent_ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;
    int target = -1;
    for (int i = 0; i < count; ++i) if (strncmp(entries[i].name, name, MAX_FILENAME) == 0) { target = (int)entries[i].ino; break; }
    if (target < 0) return -1;

    /* si c'est un répertoire, vérifier qu'il est vide (seulement . and .. allowed) */
    if (g_inodes[target].is_dir) {
        reapfs_dirent_t tmp[MAX_DIR_ENTRIES];
        int b = read_file_data(&g_inodes[target], tmp, sizeof(tmp));
        int c = b > 0 ? b / (int)sizeof(reapfs_dirent_t) : 0;
        /* if only . and .. remain, consider empty */
        if (c > 2) return -1; /* non vide */
    }

    if (dir_remove_entry((uint32_t)parent_ino, name) != 0) return -1;
    free_inode((uint32_t)target);
    save_super();
    return 0;
}

/* List files in path -> prints to console */
int fs_ls(const char *path, char *out, size_t out_sz) {
    int ino = find_inode_by_path(path);
    if (ino < 0) return -1;
    if (!g_inodes[ino].is_dir) return -1;

    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;

    print_string("FS: listing ");
    print_string(path ? path : g_cwd_path);
    print_string("\n");

    for (int i = 0; i < count; ++i) {
        print_string(" - ");
        print_string(entries[i].name);
        if (g_inodes[entries[i].ino].is_dir) print_string("/\n"); else print_string("\n");
    }
    return 0;
}

/* mkdir : crée un répertoire hiérarchique (avec . et ..) */
int fs_mkdir(const char *path) {
    char parent[256];
    char name[MAX_FILENAME];
    if (split_path(path, parent, name) != 0) return -1;

    int parent_ino = find_inode_by_path(parent);
    if (parent_ino < 0 || !g_inodes[parent_ino].is_dir) return -1;

    /* vérifier non existant */
    reapfs_dirent_t entries[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[parent_ino], entries, sizeof(entries));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;
    for (int i = 0; i < count; ++i) if (strncmp(entries[i].name, name, MAX_FILENAME) == 0) return -1;

    int ino = alloc_inode();
    if (ino < 0) return -1;

    reapfs_inode_t *node = &g_inodes[ino];
    node->is_dir = 1;
    strncpy(node->name, name, MAX_FILENAME - 1);
    node->name[MAX_FILENAME - 1] = '\0';
    node->size = 0; /* empty dir initially */

    /* Ajout automatique des entrées . et .. (écrire d'abord la table de dirent pour ce répertoire) */
    {
        reapfs_dirent_t init_entries[2];
        strncpy(init_entries[0].name, ".", MAX_FILENAME - 1);
        init_entries[0].name[MAX_FILENAME - 1] = '\0';
        init_entries[0].ino = (uint32_t)ino;
        strncpy(init_entries[1].name, "..", MAX_FILENAME - 1);
        init_entries[1].name[MAX_FILENAME - 1] = '\0';
        init_entries[1].ino = (uint32_t)parent_ino;
        if (write_file_data(node, init_entries, (uint32_t)sizeof(init_entries)) != 0) {
            free_inode((uint32_t)ino);
            return -1;
        }
        node->size = (uint32_t)sizeof(init_entries);
    }

    /* maintenant ajouter l'entrée dans le parent */
    if (dir_add_entry((uint32_t)parent_ino, name, (uint32_t)ino) != 0) {
        /* rollback : supprimer inode créé */
        free_inode((uint32_t)ino);
        return -1;
    }

    save_super();
    print_string("FS: mkdir ok\n");
    return ino;
}

/* debug print */
void fs_debug_print(void) {
    print_string("FS: debug\n");
    for (uint32_t i = 0; i < g_super.inode_count && i < MAX_INODES; ++i) {
        if (g_inode_used[i]) {
            print_string(" ino=");
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%u", i);
            print_string(tmp);
            print_string(" name=");
            print_string(g_inodes[i].name);
            print_string(g_inodes[i].is_dir ? " (dir)\n" : "\n");
        }
    }
}

/* utility to create file with content in one call */
int fs_create_with_data(const char *path, const void *data, uint32_t size) {
    int ino = fs_create(path);
    if (ino < 0) return -1;
    if (fs_write(ino, data, size) < 0) {
        fs_remove(path);
        return -1;
    }
    return ino;
}

/* ---------- Fonctions utilitaires exposées pour le shell ---------- */

/* change cwd : path peut être relatif ou absolu */
int fs_chdir(const char *path) {
    if (!path) return -1;
    int ino = find_inode_by_path(path);
    if (ino < 0 || !g_inodes[ino].is_dir) return -1;
    g_cwd_ino = ino;
    /* mettre à jour g_cwd_path */
    if (normalize_path_abs(path, g_cwd_path, sizeof(g_cwd_path)) != 0) return -1;
    return 0;
}

/* retourne pointeur sur chaîne interne (read-only) */
const char *fs_get_cwd(void) {
    return g_cwd_path;
}

/* Renvoie 1 si l’inode est un répertoire, sinon 0 */
int fs_is_dir(uint32_t ino) {
    if (ino >= MAX_INODES) return 0;
    return g_inodes[ino].is_dir ? 1 : 0;
}

/* Liste le contenu du répertoire courant dans un tableau fs_entry_t */
int fs_list_dir(fs_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return -1;

    // Utiliser le répertoire courant (et non /)
    int ino = g_cwd_ino;
    if (ino < 0 || ino >= MAX_INODES) return -1;
    if (!g_inodes[ino].is_dir) return -1;

    reapfs_dirent_t raw[MAX_DIR_ENTRIES];
    int bytes = read_file_data(&g_inodes[ino], raw, sizeof(raw));
    int count = bytes > 0 ? bytes / (int)sizeof(reapfs_dirent_t) : 0;

    int j = 0;
    for (int i = 0; i < count && j < max_entries; ++i) {
        if (strcmp(raw[i].name, ".") == 0 || strcmp(raw[i].name, "..") == 0)
            continue;
        strncpy(entries[j].name, raw[i].name, MAX_FILENAME - 1);
        entries[j].name[MAX_FILENAME - 1] = '\0';
        entries[j].ino = raw[i].ino;
        entries[j].is_dir = g_inodes[raw[i].ino].is_dir ? 1 : 0;
        j++;
    }

    return j; // nombre d'entrées trouvées
}



/* fin du fichier */
