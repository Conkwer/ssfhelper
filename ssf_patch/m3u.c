#include "m3u.h"
#include "global.h"
#include "sptd.h"
#include <stdio.h>
#include <string.h>

m3u_playlist_t g_playlist = {0};
int g_is_m3u = 0;

// Parse M3U file and extract CHD paths
int parse_m3u_file(const char* m3u_path) {
    FILE* fp;
    char line[MAX_PATH_LENGTH];
    char base_dir[MAX_PATH_LENGTH];

    memset(&g_playlist, 0, sizeof(m3u_playlist_t));

    fp = fopen(m3u_path, "r");
    if (!fp) {
        OutputDebugStringA("Failed to open M3U file");
        return 0;
    }

    // Extract base directory from M3U path
    strncpy(base_dir, m3u_path, MAX_PATH_LENGTH);
    char* last_slash = strrchr(base_dir, '\\');
    if (!last_slash) {
        last_slash = strrchr(base_dir, '/');
    }
    if (last_slash) {
        *(last_slash + 1) = '\0';
    } else {
        base_dir[0] = '\0';
    }
    strncpy(g_playlist.base_path, base_dir, MAX_PATH_LENGTH);

    // Read disc paths from M3U
    while (fgets(line, sizeof(line), fp) && g_playlist.disc_count < MAX_DISCS) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            len--;
        }
        if (len > 1 && (line[len-2] == '\r')) {
            line[len-2] = '\0';
            len--;
        }

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // Build full path
        char full_path[MAX_PATH_LENGTH];
        if (line[0] == '\\' || (len > 1 && line[1] == ':')) {
            // Absolute path
            strncpy(full_path, line, MAX_PATH_LENGTH);
        } else {
            // Relative path
            snprintf(full_path, MAX_PATH_LENGTH, "%s%s", base_dir, line);
        }

        strncpy(g_playlist.disc_paths[g_playlist.disc_count], full_path, MAX_PATH_LENGTH);
        g_playlist.disc_count++;
    }

    fclose(fp);

    if (g_playlist.disc_count > 0) {
        g_playlist.current_disc = 0;
        g_is_m3u = 1;

        char msg[256];
        sprintf(msg, "M3U: Loaded %d discs", g_playlist.disc_count);
        OutputDebugStringA(msg);

        return 1;
    }

    return 0;
}

// Swap to next disc in playlist
void swap_to_next_disc() {
    if (!g_is_m3u || g_playlist.disc_count <= 1) {
        return;
    }

    g_playlist.current_disc = (g_playlist.current_disc + 1) % g_playlist.disc_count;
    reload_current_disc();
}

// Swap to previous disc in playlist
void swap_to_previous_disc() {
    if (!g_is_m3u || g_playlist.disc_count <= 1) {
        return;
    }

    g_playlist.current_disc--;
    if (g_playlist.current_disc < 0) {
        g_playlist.current_disc = g_playlist.disc_count - 1;
    }
    reload_current_disc();
}

// Reload current disc from playlist
void reload_current_disc() {
    if (!g_is_m3u) {
        return;
    }

    char msg[512];
    sprintf(msg, "Swapping to disc %d/%d: %s", 
            g_playlist.current_disc + 1, 
            g_playlist.disc_count,
            g_playlist.disc_paths[g_playlist.current_disc]);
    OutputDebugStringA(msg);

    // Call the CHD loading function with new disc path
    // Use the new function that converts char* to WCHAR*
    load_chd_file_from_path(g_playlist.disc_paths[g_playlist.current_disc]);
    get_toc_data();

    sprintf(msg, "Disc swap complete - now on disc %d", g_playlist.current_disc + 1);
    OutputDebugStringA(msg);
}
