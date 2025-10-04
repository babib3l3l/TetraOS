#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

/* Codes d'erreur du système de fichiers */
#define FS_OK              0
#define FS_ERR_NOTFOUND   -1
#define FS_ERR_EXISTS     -2
#define FS_ERR_NOSPACE    -3
#define FS_ERR_IO         -4
#define FS_ERR_BADARG     -5
#define FS_ERR_ISDIR      -6
#define FS_ERR_NOTDIR     -7

/* Type d'identifiant de fichier (descripteur) */
typedef int fs_fd_t;

/* Structure de base d'un nœud du FS (fichier ou dossier) */
struct fs_node {
    char name[64];
    uint8_t is_dir;
    uint8_t *data;
    size_t size;
    struct fs_node *parent;
    struct fs_node *children;
    struct fs_node *next;
};

/* Initialisation du FS (en RAM pour le moment) */
int fs_init(void);

/* Création de dossier ou fichier */
int fs_mkdir(const char *path);
int fs_create(const char *path);

/* Ouverture, lecture, écriture et fermeture */
fs_fd_t fs_open(const char *path, int write_mode);
int fs_read(fs_fd_t fd, void *buf, size_t size);
int fs_write(fs_fd_t fd, const void *buf, size_t size);
int fs_close(fs_fd_t fd);

/* Suppression */
int fs_remove(const char *path);

/* Liste le contenu d’un dossier dans 'out' */
int fs_ls(const char *path, char *out, size_t out_size);

/* Fonction de debug : affiche l’arborescence complète */
void fs_debug_print(void);

/* Fonctions utilitaires pour introspection */
const char *fs_get_cwd(void);
int fs_set_cwd(const char *path);

#endif
