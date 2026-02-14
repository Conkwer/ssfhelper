#include "main.h"
#include "global.h"
#include "fs.h"
#include "sptd.h"
#include <windows.h>
#include <stdio.h>

extern void stop_key_polling();

void patch_binary(){
    init_sptd();
    patch_fs();
}

// Entry-Point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved){
    if (fdwReason == DLL_PROCESS_ATTACH ) { 
        patch_binary(); 
    }
    if (fdwReason == DLL_PROCESS_DETACH ) { 
        stop_key_polling(); 
    }
    return TRUE;
}
