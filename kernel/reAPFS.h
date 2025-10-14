#ifndef REAPFS_H
#define REAPFS_H

#include <stdint.h>
#include <stddef.h>

/*
 * reapfs.h — header complet pour le système de fichiers REAPFS (bare-metal)
 * Version freestanding (aucune dépendance libc)
 *
 * Fournit les prototypes publics utilisés par le kernel et le shell.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* === Constantes globales === */
#define REAPFS_MAGIC        0x52454150  /* "REAP" */
#define REAPFS_VERSION      1
#define REAPFS_MAX_INODES   128
#define REAPFS_MAX_FILENAME 32
#define FS_OK               0
#define FS_ERR             -1

/* === Types internes === */

/**
 * Superblock du système de fichiers
 */
struct reapfs_super {
    uint32_t magic;        /* Identifiant du FS */
    uint32_t version;      /* Version du format */
    uint32_t inode_count;  /* Nombre d'inodes utilisés */
    uint32_t block_count;  /* Nombre total de blocs */
};

/**
 * Inode simplifié (fichier ou répertoire)
 */
struct reapfs_inode {
    char name[REAPFS_MAX_FILENAME];  /* Nom du fichier */
    uint8_t parent;                  /* Index du répertoire parent */
    uint8_t is_dir;                  /* 1 = dossier, 0 = fichier */
    uint32_t size;                   /* Taille en octets */
    uint32_t first_block;            /* Premier bloc de données */
};

/**
 * Descripteur de fichier (index d'inode ou handle interne)
 */
typedef int reapfs_fd_t;

/* === API publique === */

/**
 * Initialise le FS. Charge le superblock si existant, sinon formate le disque.
 * Retourne FS_OK si succès, FS_ERR sinon.
 */
int fs_init(void);

/**
 * Crée un fichier vide avec le nom donné.
 * Retourne l’inode index (>=0) ou FS_ERR.
 */
int fs_create(const char *path);

/**
 * Supprime un fichier du FS (libère son inode).
 * Retourne FS_OK si succès, FS_ERR sinon.
 */
int fs_remove(const char *path);

/**
 * Crée un répertoire (si non existant).
 * Retourne FS_OK si succès, FS_ERR sinon.
 */
int fs_mkdir(const char *path);

/**
 * Ouvre un fichier (lecture/écriture selon `write`).
 * Retourne un descripteur (>=0) ou FS_ERR si échec.
 */
reapfs_fd_t fs_open(const char *name, int write);

/**
 * Lit `sz` octets du fichier référencé par `fd` dans `buf`.
 * Retourne le nombre d’octets lus ou FS_ERR.
 */
int fs_read(reapfs_fd_t fd, void *buf, size_t sz);

/**
 * Écrit `sz` octets du buffer `buf` dans le fichier `fd`.
 * Retourne le nombre d’octets écrits ou FS_ERR.
 */
int fs_write(reapfs_fd_t fd, const void *buf, size_t sz);

/**
 * Ferme un fichier (actuellement no-op).
 * Toujours retourne FS_OK.
 */
void fs_close(reapfs_fd_t fd);

/**
 * Liste le contenu du répertoire donné dans `out` (chaîne texte).
 * Ex. fs_ls("/", buf, 4096);
 * Retourne FS_OK si succès, FS_ERR sinon.
 */
int fs_ls(const char *path, char *out, size_t out_sz);

/**
 * Affiche les métadonnées du FS (debug).
 */
void fs_debug_print(void);

#ifdef __cplusplus
}
#endif

#endif /* REAPFS_H */
