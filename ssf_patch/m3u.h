#ifndef SSF_PATCH_M3U_H
#define SSF_PATCH_M3U_H

#include <windows.h>

#define MAX_DISCS 10
#define MAX_PATH_LENGTH 512

typedef struct {
    char disc_paths[MAX_DISCS][MAX_PATH_LENGTH];
    int disc_count;
    int current_disc;
    char base_path[MAX_PATH_LENGTH];
} m3u_playlist_t;

extern m3u_playlist_t g_playlist;
extern int g_is_m3u;

int parse_m3u_file(const char* m3u_path);
void swap_to_next_disc();
void swap_to_previous_disc();
void reload_current_disc();

#endif //SSF_PATCH_M3U_H
