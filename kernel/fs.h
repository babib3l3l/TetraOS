/*
 * fs.h - Interface du système de fichiers en mémoire pour TetraOS
 *
 * Fournit une API simple pour gérer un système de fichiers hiérarchique
 * en RAM, avec des dossiers et des fichiers (inspiré d’APFS, version simplifiée).
 *
 * Auteur : adapté pour TetraOS
 */

#ifndef FS_H
#define FS_H

#include <stddef.h>

/* ------------------------------------------------------------------
 * Constantes
 * ------------------------------------------------------------------ */
#define FS_MAX_NAME     64      // Longueur max d'un nom de fichier/dossier
#define FS_MAX_OPEN_FD  32      // Nombre max de fichiers ouverts simultanément

/* ------------------------------------------------------------------
 * Codes d'erreur
 * ------------------------------------------------------------------ */
#define FS_OK            0
#define FS_ERR_NOMEM    -1   // Mémoire insuffisante
#define FS_ERR_NOTFOUND -2   // Fichier/dossier introuvable
#define FS_ERR_EXISTS   -3   // Fichier/dossier déjà existant
#define FS_ERR_NOTDIR   -4   // Attendu un dossier mais trouvé un fichier
#define FS_ERR_ISDIR    -5   // Attendu un fichier mais trouvé un dossier
#define FS_ERR_BADFD    -6   // Descripteur invalide
#define FS_ERR_IO       -7   // Erreur d'entrée/sortie
#define FS_ERR_NOFD     -8   // Trop de fichiers ouverts
#define FS_ERR_BADARG   -9   // Argument invalide

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */
typedef enum {
    FS_NODE_FILE,
    FS_NODE_DIR
} fs_node_type_t;

struct fs_node {
    char name[FS_MAX_NAME];     // Nom du fichier/dossier
    fs_node_type_t type;        // Type (fichier ou dossier)
    struct fs_node *parent;     // Dossier parent
    struct fs_node *children;   // Liste chaînée des enfants (si dossier)
    struct fs_node *next;       // Sibling suivant
    char *data;                 // Contenu (si fichier)
    size_t size;                // Taille du contenu
};

typedef struct {
    struct fs_node *node;       // Pointeur vers le fichier ouvert
    size_t pos;                 // Position courante (pour read/write)
    int used;                   // 0 = libre, 1 = utilisé
} fs_fd_t;

/* ------------------------------------------------------------------
 * API publique
 * ------------------------------------------------------------------ */

// Initialisation
void fs_init(void);

// Création
int fs_create(const char *path);
int fs_mkdir(const char *path);

// Compatibilité : alias de fs_create()
#define fs_add(path) fs_create(path)

// Gestion fichiers
int fs_open(const char *path);
int fs_close(int fd);
int fs_write(int fd, const char *buf, size_t len);
int fs_read(int fd, char *buf, size_t len);

// Suppression
int fs_remove(const char *path);

// Liste d’un répertoire (remplit buffer avec les noms)
int fs_ls(const char *path, char *out, size_t out_size);

// Debug
void fs_debug_print(void);

#endif // FS_H
