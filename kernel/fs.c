/*
 * fs.c - Implémentation du système de fichiers en mémoire pour TetraOS
 *
 * Ce module fournit un système de fichiers hiérarchique parent/enfant
 * (semblable à APFS mais simplifié) permettant de créer, lire, écrire,
 * et lister des fichiers et répertoires.
 *
 * Fonctions principales :
 *  - fs_init()        : Initialise le système de fichiers (racine "/")
 *  - fs_create()      : Crée un fichier dans un chemin donné
 *  - fs_mkdir()       : Crée un répertoire
 *  - fs_add()         : Alias de fs_create() (compatibilité avec l'ancien code)
 *  - fs_open()        : Ouvre un fichier et retourne un descripteur
 *  - fs_close()       : Ferme un fichier ouvert
 *  - fs_write()       : Écrit des données dans un fichier
 *  - fs_read()        : Lit des données depuis un fichier
 *  - fs_ls()          : Liste le contenu d’un répertoire
 *  - fs_remove()      : Supprime un fichier ou répertoire
 *  - fs_debug_print() : Affiche l'arborescence complète (debug)
 *
 * Auteur : adapté pour TetraOS
 */


#ifndef FS_H
#define FS_H

#include "utils.h"
#include "global.h"

// Codes d’erreur (valeurs négatives)
#define FS_OK            0    // Pas d’erreur
#define FS_ERR_NOTFOUND -1    // Fichier/dossier introuvable
#define FS_ERR_EXISTS   -2    // Déjà existant
#define FS_ERR_NOTDIR   -3    // Cible n’est pas un dossier
#define FS_ERR_ISDIR    -4    // Cible est un dossier
#define FS_ERR_NOMEM    -5    // Pas assez de mémoire
#define FS_ERR_BADARG   -6    // Argument invalide
#define FS_ERR_IO       -7    // Erreur I/O générique

// Descripteur de fichier (identifiant entier)
typedef int fs_fd_t;

// -------------------- API publique --------------------

// Initialisation du FS (appelé au boot)
void fs_init(void);

// Création d’un dossier
int fs_mkdir(const char *path);

// Création d’un fichier vide
int fs_create(const char *path);

// Alias de create (compatibilité avec ancienne commande `add`)
int fs_add(const char *path);

// Ouvrir un fichier (mode=0: lecture seule, mode=1: lecture/écriture)
fs_fd_t fs_open(const char *path, int mode);

// Fermer un descripteur de fichier
int fs_close(fs_fd_t fd);

// Écrire dans un fichier ouvert
int fs_write(fs_fd_t fd, const void *buf, size_t count);

// Lire depuis un fichier ouvert
int fs_read(fs_fd_t fd, void *buf, size_t count);

// Lister le contenu d’un dossier (stocke texte formaté dans `out`)
int fs_ls(const char *path, char *out, size_t out_size);

// Supprimer un fichier ou dossier (récursif pour dossier)
int fs_remove(const char *path);

// Debug : affiche l’arborescence complète
void fs_debug_print(void);

#endif // FS_H


/* ========================= fs.c ========================= */

#include "fs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// -------------------- Constantes --------------------
#define NAME_MAX 64            // Taille max d’un nom de fichier/dossier
#define INITIAL_CONTENT_CAP 256 // Taille initiale d’un fichier en octets
#define MAX_OPEN_FD 32          // Nombre max de fichiers ouverts en même temps

// -------------------- Structures --------------------

// Noeud d’arbre (fichier ou dossier)
struct fs_node {
    char name[NAME_MAX];          // Nom
    int is_dir;                   // 1 = dossier, 0 = fichier
    char *content;                // Données du fichier (NULL si dossier)
    size_t size;                  // Taille utilisée (fichier)
    size_t capacity;              // Capacité allouée (fichier)
    struct fs_node *parent;       // Parent
    struct fs_node *children;     // Premier enfant (si dossier)
    struct fs_node *next;         // Frère suivant
};

// Entrée de la table des fichiers ouverts
struct fd_entry {
    struct fs_node *node;  // Pointeur vers le fichier
    size_t offset;         // Position courante (lecture/écriture)
    int mode;              // Mode (0=lecture, 1=rw)
    int used;              // Occupé ou non
};

// Racine du FS
static struct fs_node *root = NULL;
// Table des FD
static struct fd_entry fd_table[MAX_OPEN_FD];

// -------------------- Helpers internes --------------------

// Création d’un nouveau noeud (fichier ou dossier)
static struct fs_node *node_create(const char *name, int is_dir) {
    struct fs_node *n = malloc(sizeof(*n));
    if (!n) return NULL;

    strncpy(n->name, name, NAME_MAX-1);
    n->name[NAME_MAX-1] = '\0';

    n->is_dir = is_dir;
    n->parent = NULL;
    n->children = NULL;
    n->next = NULL;
    n->size = 0;
    n->capacity = 0;
    n->content = NULL;

    // Allocation pour les fichiers
    if (!is_dir) {
        n->capacity = INITIAL_CONTENT_CAP;
        n->content = malloc(n->capacity);
        if (!n->content) { free(n); return NULL; }
    }

    return n;
}

// Libération récursive d’un noeud
static void node_free_recursive(struct fs_node *n) {
    if (!n) return;
    // Libération enfants
    struct fs_node *c = n->children;
    while (c) {
        struct fs_node *next = c->next;
        node_free_recursive(c);
        c = next;
    }
    // Libération contenu
    if (n->content) free(n->content);
    free(n);
}

// Découpe un chemin en composants
static char **split_path(const char *path, int *count) {
    if (!path) return NULL;
    char *tmp = strdup(path);
    if (!tmp) return NULL;

    // Ignorer le premier '/'
    char *p = tmp;
    if (*p == '/') p++;

    // Compter et marquer les séparateurs
    for (char *q = p; *q; ++q) if (*q == '/') *q = '\0';

    // Compter le nombre de parties
    int cnt = 0; char *it = p;
    while (*it) { cnt++; it += strlen(it)+1; }

    if (cnt == 0) { free(tmp); *count = 0; return NULL; }

    char **arr = malloc(sizeof(char*) * cnt);
    if (!arr) { free(tmp); return NULL; }

    it = p; int i=0;
    while (*it) { arr[i++] = strdup(it); it += strlen(it)+1; }
    *count = cnt;
    free(tmp);
    return arr;
}

static void free_components(char **c, int n) {
    if (!c) return;
    for (int i=0;i<n;i++) free(c[i]);
    free(c);
}

// Recherche d’un noeud par chemin
// Si create_parent=1, retourne le parent si dernier composant absent
static struct fs_node *find_node_by_path(const char *path, struct fs_node **parent_out, int create_parent) {
    if (!path || !root) return NULL;

    if (strcmp(path, "/") == 0) { if (parent_out) *parent_out = NULL; return root; }

    int cnt=0; char **parts = split_path(path, &cnt);
    if (!parts) return NULL;

    struct fs_node *cur = root;
    struct fs_node *parent = NULL;

    for (int i=0;i<cnt;i++) {
        parent = cur;
        // Chercher enfant du même nom
        struct fs_node *c = cur->children;
        while (c) {
            if (strncmp(c->name, parts[i], NAME_MAX)==0) break;
            c = c->next;
        }
        if (!c) {
            if (create_parent && i==cnt-1) { cur = NULL; break; }
            cur = NULL; break;
        }
        cur = c;
        if (!cur->is_dir && i < cnt-1) { cur = NULL; break; }
    }

    free_components(parts, cnt);
    if (parent_out) *parent_out = parent;
    return cur;
}

// Attacher un enfant à un dossier
static int attach_child(struct fs_node *dir, struct fs_node *child) {
    if (!dir || !dir->is_dir) return FS_ERR_NOTDIR;
    child->next = dir->children;
    dir->children = child;
    child->parent = dir;
    return FS_OK;
}

// -------------------- API publique --------------------

// Initialisation
void fs_init(void) {
    if (root) return;
    root = node_create("/", 1);
}

// Création d’un dossier
int fs_mkdir(const char *path) {
    if (!path) return FS_ERR_BADARG;
    if (strcmp(path, "/")==0) return FS_ERR_EXISTS;

    struct fs_node *found = find_node_by_path(path, NULL, 0);
    if (found) return FS_ERR_EXISTS;

    int cnt=0; char **parts = split_path(path, &cnt);
    if (!parts || cnt==0) return FS_ERR_BADARG;

    // Trouver parent
    struct fs_node *p = (cnt==1) ? root : find_node_by_path(path, NULL, 1);
    if (!p) { free_components(parts,cnt); return FS_ERR_NOTDIR; }

    // Vérifier doublons
    struct fs_node *c = p->children;
    while (c) { if (strcmp(c->name, parts[cnt-1])==0) { free_components(parts,cnt); return FS_ERR_EXISTS; } c = c->next; }

    // Créer et attacher
    struct fs_node *n = node_create(parts[cnt-1], 1);
    if (!n) { free_components(parts,cnt); return FS_ERR_NOMEM; }
    attach_child(p, n);

    free_components(parts,cnt);
    return FS_OK;
}

// Création fichier
int fs_create(const char *path) {
    if (!path) return FS_ERR_BADARG;
    int cnt=0; char **parts = split_path(path, &cnt);
    if (!parts || cnt==0) return FS_ERR_BADARG;

    // Parent
    struct fs_node *p = (cnt==1) ? root : find_node_by_path(path, NULL, 1);
    if (!p) { free_components(parts,cnt); return FS_ERR_NOTDIR; }

    // Vérifier doublons
    struct fs_node *c = p->children;
    while (c) { if (strcmp(c->name, parts[cnt-1])==0) { free_components(parts,cnt); return FS_ERR_EXISTS; } c = c->next; }

    // Créer et attacher
    struct fs_node *n = node_create(parts[cnt-1], 0);
    if (!n) { free_components(parts,cnt); return FS_ERR_NOMEM; }
    attach_child(p, n);

    free_components(parts,cnt);
    return FS_OK;
}

// Alias pour compatibilité
int fs_add(const char *path) { return fs_create(path); }

// Ouvrir un fichier
fs_fd_t fs_open(const char *path, int mode) {
    struct fs_node *n = find_node_by_path(path, NULL, 0);
    if (!n) return FS_ERR_NOTFOUND;
    if (n->is_dir) return FS_ERR_ISDIR;

    for (int i=0;i<MAX_OPEN_FD;i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].node = n;
            fd_table[i].offset = 0;
            fd_table[i].mode = mode;
            return i;
        }
    }
    return FS_ERR_NOMEM;
}

// Fermer un fichier
int fs_close(fs_fd_t fd) {
    if (fd < 0 || fd >= MAX_OPEN_FD || !fd_table[fd].used) return FS_ERR_BADARG;
    fd_table[fd].used = 0;
    fd_table[fd].node = NULL;
    fd_table[fd].offset = 0;
    fd_table[fd].mode = 0;
    return FS_OK;
}

// Vérifie capacité et réalloue si nécessaire
static int ensure_capacity(struct fs_node *n, size_t need) {
    if (!n || n->is_dir) return FS_ERR_ISDIR;
    if (need <= n->capacity) return FS_OK;

    size_t newcap = n->capacity * 2;
    while (newcap < need) newcap *= 2;

    char *p = realloc(n->content, newcap);
    if (!p) return FS_ERR_NOMEM;
    n->content = p;
    n->capacity = newcap;
    return FS_OK;
}

// Écrire
int fs_write(fs_fd_t fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FD || !fd_table[fd].used) return FS_ERR_BADARG;
    struct fs_node *n = fd_table[fd].node;
    if (!n || n->is_dir) return FS_ERR_ISDIR;

    size_t off = fd_table[fd].offset;
    int r = ensure_capacity(n, off+count);
    if (r < 0) return r;

    memcpy(n->content + off, buf, count);
    fd_table[fd].offset += count;
    if (n->size < fd_table[fd].offset) n->size = fd_table[fd].offset;

    return (int)count;
}

// Lire
int fs_read(fs_fd_t fd, void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FD || !fd_table[fd].used) return FS_ERR_BADARG;
    struct fs_node *n = fd_table[fd].node;
    if (!n || n->is_dir) return FS_ERR_ISDIR;

    size_t off = fd_table[fd].offset;
    if (off >= n->size) return 0; // EOF

    size_t can = n->size - off;
    size_t toread = (count < can) ? count : can;
    memcpy(buf, n->content + off, toread);
    fd_table[fd].offset += toread;
    return (int)toread;
}

// Liste contenu dossier
int fs_ls(const char *path, char *out, size_t out_size) {
    struct fs_node *n = (!path || strcmp(path, "")==0) ? root : find_node_by_path(path, NULL, 0);
    if (!n) return FS_ERR_NOTFOUND;
    if (!n->is_dir) return FS_ERR_NOTDIR;

    size_t used = 0;
    struct fs_node *c = n->children;
    while (c) {
        int nw = snprintf(out + used, (out_size > used) ? (out_size - used) : 0,
                         "%s\t%s\t%zu\n", c->name, c->is_dir?"<DIR>":"<FILE>", c->size);
        if (nw < 0) return FS_ERR_IO;
        used += nw;
        if (used >= out_size) break;
        c = c->next;
    }
    return FS_OK;
}

// Supprimer fichier/dossier
int fs_remove(const char *path) {
    if (!path || strcmp(path, "/")==0) return FS_ERR_BADARG;
    struct fs_node *parent = NULL;
    struct fs_node *target = find_node_by_path(path, &parent, 0);
    if (!target || !parent) return FS_ERR_NOTFOUND;

    // Retirer de la liste chaînée des enfants
    struct fs_node *prev = NULL; struct fs_node *it = parent->children;
    while (it) {
        if (it == target) break;
        prev = it; it = it->next;
    }
    if (!it) return FS_ERR_NOTFOUND;

    if (prev) prev->next = it->next; else parent->children = it->next;
    node_free_recursive(it);
    return FS_OK;
}

// Debug : affichage arborescence
void fs_debug_print(void) {
    if (!root) return;
    // Pile manuelle
    struct stack_item { struct fs_node *n; int depth; struct stack_item *next; } *stack = NULL;
    stack = malloc(sizeof(*stack)); stack->n = root; stack->depth = 0; stack->next = NULL;

    while (stack) {
        struct stack_item *si = stack; stack = stack->next;

        for (int i=0;i<si->depth;i++) putchar(' ');
        printf("%s %s size=%zu\n", si->n->is_dir?"DIR":"FILE", si->n->name, si->n->size);

        // Empiler enfants (ordre inverse pour parcours naturel)
        struct fs_node *arr[64]; int ac=0;
        struct fs_node *c = si->n->children;
        while (c && ac<64) { arr[ac++] = c; c=c->next; }
        for (int i=ac-1;i>=0;i--) {
            struct stack_item *new = malloc(sizeof(*new));
            new->n = arr[i];
            new->depth = si->depth + 1;
            new->next = stack;
            stack = new;
        }

        free(si);
    }
}

 // Affiche récursivement l'arborescence du FS (pour debug).
static void fs_debug_recursive(fs_node_t *node, int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
    printf("- %s (%s, %d bytes)\n",
           node->name,
           node->type == FS_NODE_DIR ? "DIR" : "FILE",
           node->size);

    if (node->type == FS_NODE_DIR) {
        fs_node_t *child = node->children;
        while (child) {
            fs_debug_recursive(child, depth + 1);
            child = child->next;
        }
    }
}


//API publique : affiche tout le FS depuis la racine
void fs_debug_print(void) {
    if (!fs_root) {
        printf("[FS] Non initialisé !\n");
        return;
    }
    fs_debug_recursive(fs_root, 0);
}
