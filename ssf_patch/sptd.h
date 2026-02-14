#ifndef SSF_PATCH_SPTD_H
#define SSF_PATCH_SPTD_H

#include <windows.h>
#include "chd_helper.h"

void init_sptd();
void get_toc_data();
void load_chd_file(unsigned char* chd_path);  // Takes WCHAR* (wide string)
void load_chd_file_from_path(const char* chd_path_char);  // New: Takes char* and converts to WCHAR*

#endif //SSF_PATCH_SPTD_H
