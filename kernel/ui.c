#include "reapfs.h"
#include "screen.h"
#include "ui.h"
#include "utils.h"

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

void fs_draw_ls(void) {
    const char *cwd = fs_get_cwd();

    // Nettoyer le chemin courant pour éviter /test/../test/...
    char clean_path[MAX_PATH];
    if (normalize_path_abs(cwd, clean_path, sizeof(clean_path)) != 0)
        strncpy(clean_path, cwd, sizeof(clean_path) - 1);

    // Déterminer le nom du répertoire courant (dernier segment)
    const char *last_slash = strrchr(clean_path, '/');
    const char *dir_name = last_slash ? last_slash + 1 : clean_path;
    if (dir_name[0] == '\0') dir_name = "/";

    // Ouvrir le répertoire courant
    int ino = fs_open(cwd, 0);
    if (ino < 0) return;

    // Lire le contenu (fs_list_dir ignore déjà . et ..)
    fs_entry_t entries[MAX_DIR_ENTRIES];
    int count = fs_list_dir(entries, MAX_DIR_ENTRIES);
    if (count < 0) return;

    // Position d’affichage (coin supérieur droit)
    int screen_w = screen_get_width();
    int x = screen_w - 25;
    int y = 0;

    // Nettoyer la zone
    screen_fill_rect(x - 2, y, 25, 14, ' ');

    // En-tête lisible
    print_xy(x, y++, "[FS]");
    print_xy(x, y++, dir_name);
    print_xy(x, y++, "----------------");

    // Liste du contenu du répertoire
    if (count == 0) {
        print_xy(x, y++, "(vide)");
    } else {
        for (int i = 0; i < count; ++i) {
            char line[40];
            snprintf(line, sizeof(line), "%s%s",
                     entries[i].name,
                     entries[i].is_dir ? "/" : "");
            print_xy(x, y++, line);
        }
    }
}
