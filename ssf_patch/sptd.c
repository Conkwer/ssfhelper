#include "sptd.h"
#include "global.h"
#include "chd_helper.h"
#include "m3u.h"
#include <string.h>
#include <stdlib.h>

static HANDLE ldll;
static int(*libchd_cdrom_open)(int);
static int(*libchd_cdrom_get_toc)(int,unsigned int*);
static int(*libchd_cdrom_read_data)(struct _cdrom_file*,unsigned int,unsigned char*,unsigned int,unsigned char);
static unsigned char* disc_toc = NULL;
static unsigned int disc_toc_size = NULL;

static HANDLE key_polling_thread = NULL;
static int keep_polling = 1;

#define CONFIG_FILE "ssfhelper.ini"
#define DEFAULT_STARTUP_DELAY 7000
#define DEFAULT_SWAP_PROTECTION 5000

static char g_m3u_filename[512] = {0};

// Configurable key virtual codes (loaded from ini)
static int vk_next_disc  = VK_NEXT;   // Page Down
static int vk_prev_disc  = VK_PRIOR;  // Page Up
static int vk_auto_next  = VK_END;    // End (auto F1/F2)
static int vk_auto_prev  = VK_HOME;   // Home (auto F1/F2)

void get_config_path(char* path_out, size_t out_size) {
    char current_dir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, current_dir);
    snprintf(path_out, out_size, "%s\\%s", current_dir, CONFIG_FILE);
}

int config_exists() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    DWORD attrib = GetFileAttributesA(config_path);
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// Convert key name string from ini to Windows VK code
// Supports: PageDown, PageUp, Home, End, F1-F12,
//           OBracket (='['), CBracket (=']'),
//           Comma, Period, Minus, Equals, Backslash,
//           A-Z, F1-F12, and numeric VK (e.g. "0x21")
int parse_vk_key(const char* key_name) {
    if (!key_name || key_name[0] == '\0') return 0;

    // Numeric hex value e.g. "0x22"
    if (key_name[0] == '0' && (key_name[1] == 'x' || key_name[1] == 'X')) {
        return (int)strtol(key_name, NULL, 16);
    }

    // Named keys
    if (_stricmp(key_name, "PageDown")   == 0) return VK_NEXT;
    if (_stricmp(key_name, "PageUp")     == 0) return VK_PRIOR;
    if (_stricmp(key_name, "Home")       == 0) return VK_HOME;
    if (_stricmp(key_name, "End")        == 0) return VK_END;
    if (_stricmp(key_name, "Insert")     == 0) return VK_INSERT;
    if (_stricmp(key_name, "Delete")     == 0) return VK_DELETE;
    if (_stricmp(key_name, "OBracket")   == 0) return 0xDB;  // [ key
    if (_stricmp(key_name, "CBracket")   == 0) return 0xDD;  // ] key
    if (_stricmp(key_name, "Comma")      == 0) return VK_OEM_COMMA;
    if (_stricmp(key_name, "Period")     == 0) return VK_OEM_PERIOD;
    if (_stricmp(key_name, "Minus")      == 0) return VK_OEM_MINUS;
    if (_stricmp(key_name, "Equals")     == 0) return VK_OEM_PLUS;
    if (_stricmp(key_name, "Backslash")  == 0) return VK_OEM_5;
    if (_stricmp(key_name, "None")       == 0) return 0;     // Disabled
    if (_stricmp(key_name, "Disabled")   == 0) return 0;     // Disabled

    // F1-F12
    if ((key_name[0] == 'f' || key_name[0] == 'F') && key_name[1] != '\0') {
        int n = atoi(key_name + 1);
        if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
    }

    // Single letter A-Z
    if (key_name[1] == '\0' && ((key_name[0] >= 'A' && key_name[0] <= 'Z') ||
                                  (key_name[0] >= 'a' && key_name[0] <= 'z'))) {
        return toupper((unsigned char)key_name[0]);
    }

    return 0;  // Unrecognised = disabled
}

int create_default_config() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    OutputDebugStringA("[CONFIG] Creating default config...");
    FILE* f = fopen(config_path, "w");
    if (f) {
        fprintf(f, "; SSFHelper Configuration\n\n");
        fprintf(f, "[Settings]\n");
        fprintf(f, "; Startup delay before disc swap keys become active (ms)\n");
        fprintf(f, "StartupDelay=7000\n\n");
        fprintf(f, "; Protection delay after Home/End disc swap (ms)\n");
        fprintf(f, "SwapProtection=5000\n\n");
        fprintf(f, "; SSF R4 mode - auto F1+F2 for Home/End keys (1=ON, 0=OFF)\n");
        fprintf(f, "; ON: For SSF_012_beta_R4 (keybd_event works perfectly)\n");
        fprintf(f, "; OFF: For SSF R16+ - swap disc only, press F1+F2 manually\n");
        fprintf(f, "EnableSSF_R4_Mode=0\n\n");
        fprintf(f, "; === Key Mappings ===\n");
        fprintf(f, "; Supported values: PageDown, PageUp, Home, End,\n");
        fprintf(f, ";   OBracket ([), CBracket (]), Comma, Period, Minus,\n");
        fprintf(f, ";   Equals, Backslash, F1-F12, A-Z, 0x## (hex VK code),\n");
        fprintf(f, ";   None (disabled)\n\n");
        fprintf(f, "; Simple disc swap - no F1/F2, just switches the CHD\n");
        fprintf(f, "Key_NextDisc=PageDown\n");
        fprintf(f, "Key_PrevDisc=PageUp\n\n");
        fprintf(f, "; Disc swap + auto F1/F2 (SSF R4) or notify only (SSF R16+)\n");
        fprintf(f, "Key_AutoNext=End\n");
        fprintf(f, "Key_AutoPrev=Home\n\n");
        fprintf(f, "[LastDisc]\n");
        fprintf(f, "; Shining Force III Collection.m3u = 0\n");
        fclose(f);
        OutputDebugStringA("[CONFIG] Created successfully");
        return 1;
    }
    char debug_msg[256];
    sprintf(debug_msg, "[CONFIG] Create FAILED - Error: %lu", GetLastError());
    OutputDebugStringA(debug_msg);
    return 0;
}

int read_startup_delay() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char buffer[32];
    GetPrivateProfileStringA("Settings", "StartupDelay", "7000", buffer, sizeof(buffer), config_path);
    int delay = atoi(buffer);
    if (delay < 0 || delay > 30000) delay = DEFAULT_STARTUP_DELAY;
    char msg[128];
    sprintf(msg, "[CONFIG] StartupDelay = %d ms", delay);
    OutputDebugStringA(msg);
    return delay;
}

int read_swap_protection() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char buffer[32];
    GetPrivateProfileStringA("Settings", "SwapProtection", "5000", buffer, sizeof(buffer), config_path);
    int delay = atoi(buffer);
    if (delay < 0 || delay > 30000) delay = DEFAULT_SWAP_PROTECTION;
    char msg[128];
    sprintf(msg, "[CONFIG] SwapProtection = %d ms", delay);
    OutputDebugStringA(msg);
    return delay;
}

int read_enable_ssf_r4_mode() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char buffer[32];
    GetPrivateProfileStringA("Settings", "EnableSSF_R4_Mode", "0", buffer, sizeof(buffer), config_path);
    int enabled = atoi(buffer);
    if (enabled != 0 && enabled != 1) enabled = 0;
    char msg[128];
    sprintf(msg, "[CONFIG] EnableSSF_R4_Mode = %d (%s)", enabled,
            enabled ? "ON - SSF R4 auto F1/F2" : "OFF - SSF R16+ manual F1/F2");
    OutputDebugStringA(msg);
    return enabled;
}

void read_key_mappings() {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char buffer[64];
    char msg[256];

    GetPrivateProfileStringA("Settings", "Key_NextDisc", "PageDown", buffer, sizeof(buffer), config_path);
    vk_next_disc = parse_vk_key(buffer);
    sprintf(msg, "[CONFIG] Key_NextDisc = %s (VK 0x%02X)", buffer, vk_next_disc);
    OutputDebugStringA(msg);

    GetPrivateProfileStringA("Settings", "Key_PrevDisc", "PageUp", buffer, sizeof(buffer), config_path);
    vk_prev_disc = parse_vk_key(buffer);
    sprintf(msg, "[CONFIG] Key_PrevDisc = %s (VK 0x%02X)", buffer, vk_prev_disc);
    OutputDebugStringA(msg);

    GetPrivateProfileStringA("Settings", "Key_AutoNext", "End", buffer, sizeof(buffer), config_path);
    vk_auto_next = parse_vk_key(buffer);
    sprintf(msg, "[CONFIG] Key_AutoNext  = %s (VK 0x%02X)", buffer, vk_auto_next);
    OutputDebugStringA(msg);

    GetPrivateProfileStringA("Settings", "Key_AutoPrev", "Home", buffer, sizeof(buffer), config_path);
    vk_auto_prev = parse_vk_key(buffer);
    sprintf(msg, "[CONFIG] Key_AutoPrev  = %s (VK 0x%02X)", buffer, vk_auto_prev);
    OutputDebugStringA(msg);
}

int read_last_disc(const char* m3u_filename) {
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char buffer[32];
    GetPrivateProfileStringA("LastDisc", m3u_filename, "0", buffer, sizeof(buffer), config_path);
    int disc_num = atoi(buffer);
    if (disc_num < 0 || disc_num > 50) disc_num = 0;
    char msg[256];
    sprintf(msg, "[CONFIG] Read '%s' = %d", m3u_filename, disc_num);
    OutputDebugStringA(msg);
    return disc_num;
}

int write_last_disc(const char* m3u_filename, int disc_number) {
    if (disc_number < 0 || disc_number > 50) disc_number = 0;
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path));
    char value[32];
    sprintf(value, "%d", disc_number);
    BOOL result = WritePrivateProfileStringA("LastDisc", m3u_filename, value, config_path);
    if (result) {
        char msg[256];
        sprintf(msg, "[CONFIG] Write '%s' = %d SUCCESS", m3u_filename, disc_number);
        OutputDebugStringA(msg);
        return 1;
    }
    return 0;
}

void extract_filename(const char* full_path, char* filename_out, size_t out_size) {
    const char* last_slash  = strrchr(full_path, '\\');
    const char* last_fslash = strrchr(full_path, '/');
    const char* filename = full_path;
    if (last_slash && (!last_fslash || last_slash > last_fslash))
        filename = last_slash + 1;
    else if (last_fslash)
        filename = last_fslash + 1;
    strncpy(filename_out, filename, out_size - 1);
    filename_out[out_size - 1] = '\0';
}

void update_config_on_disc_change() {
    if (!g_is_m3u || strlen(g_m3u_filename) == 0) return;
    if (!config_exists()) create_default_config();
    write_last_disc(g_m3u_filename, g_playlist.current_disc);
}


bswap32(unsigned int x) {
    return ((x << 24) & 0xff000000) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00) | ((x >> 24) & 0x000000ff);
}

unsigned short bswap16(unsigned short x) {
    return ((x << 8) & 0xff00) | ((x >> 8) & 0x00ff);
}

void get_toc_data(){
    if(disc_toc != NULL) { free(disc_toc); disc_toc = NULL; }
    disc_toc = (unsigned char*)malloc(12+(cdrf.cdtoc.numtrks*8));
    memset(disc_toc,0x00,12+(cdrf.cdtoc.numtrks*8));
    unsigned short toc_sz = 10+(cdrf.cdtoc.numtrks*8);
    toc_sz = bswap16(toc_sz);
    memcpy(disc_toc,&toc_sz,2);
    memset(disc_toc+2,0x01,1);
    memset(disc_toc+3,cdrf.cdtoc.numtrks&0xFF,1);
    unsigned int toc_buffer_track_offset = 4;
    unsigned int leadout_start = 0;
    for(int i=0;i<cdrf.cdtoc.numtrks;i++){
        unsigned char track_info[8] = {0x00};
        if(cdrf.cdtoc.tracks[i].subtype == 0x01) track_info[1] = 0x14;
        else track_info[1] = 0x10;
        track_info[2] = i+1;
        unsigned int tis = bswap32(cdrf.cdtoc.tracks[i].physframeofs);
        memcpy(track_info+4,&tis,4);
        memcpy(disc_toc+toc_buffer_track_offset,track_info,8);
        toc_buffer_track_offset+=8;
        leadout_start = cdrf.cdtoc.tracks[i].physframeofs + cdrf.cdtoc.tracks[i].extraframes;
    }
    unsigned char leadout_track[8] = {0x00, 0x10, 0xAA, 0x00,0x00, 0x00, 0x00, 0x00};
    unsigned int tis = bswap32(leadout_start);
    memcpy(leadout_track+4,&tis,4);
    memset(disc_toc+3,cdrf.cdtoc.numtrks&0xFF,1);
    memcpy(disc_toc+toc_buffer_track_offset,leadout_track,8);
    disc_toc_size = 12+(cdrf.cdtoc.numtrks*8);
}

void load_chd_file(unsigned char* chd_path){
    int addr = libchd_cdrom_open(chd_path);
    if(addr == 1 || addr == 0) OutputDebugStringA("Opening CHD Failed!");
    else OutputDebugStringA("Opened CHD");
    memcpy(&cdrf,addr,sizeof(cdrf));
}

void load_chd_file_from_path(const char* chd_path_char){
    WCHAR chd_path_wide[512];
    MultiByteToWideChar(CP_ACP, 0, chd_path_char, -1, chd_path_wide, 512);
    load_chd_file((unsigned char*)chd_path_wide);
}

void init_chd_library(){
    ldll = LoadLibrary("libchd.dll");
    libchd_cdrom_open  = GetProcAddress(ldll, "libchd_cdrom_open");
    libchd_cdrom_get_toc  = GetProcAddress(ldll, "libchd_cdrom_get_toc");
    libchd_cdrom_read_data = GetProcAddress(ldll, "libchd_cdrom_read_data");
}

void bswap_buffer(unsigned char* buffer, unsigned int num_bytes){
    for(int i=0;i<num_bytes;i+=2){
        buffer[i]   ^= buffer[i+1];
        buffer[i+1] ^= buffer[i];
        buffer[i]   ^= buffer[i+1];
    }
}

void read_disc_data(unsigned char* buffer, unsigned int offset, unsigned int len, unsigned int dtype){
    unsigned int lba_sz = 2352;
    unsigned int dest_offset = 0;
    unsigned char* tb = (unsigned char*)malloc(2352);
    for(unsigned int i=offset; i<offset+len; i++){
        memset(tb,0x00,2352);
        int retval = libchd_cdrom_read_data(&cdrf, i, tb, dtype, 1);
        if(retval == 0){ libchd_cdrom_read_data(&cdrf, i, tb, 7, 1); bswap_buffer(tb, 2352); }
        memcpy(buffer+dest_offset, tb, lba_sz);
        dest_offset += 2352;
    }
    free(tb);
}

typedef struct _SCSI_PASS_THROUGH_DIRECT {
    unsigned short Length;
    unsigned char  ScsiStatus;
    unsigned char  PathId;
    unsigned char  TargetId;
    unsigned char  Lun;
    unsigned char  CdbLength;
    unsigned char  SenseInfoLength;
    unsigned char  DataIn;
    unsigned int  DataTransferLength;
    unsigned int  TimeOutValue;
    void*  DataBuffer;
    unsigned int  SenseInfoOffset;
    unsigned char  Cdb[16];
}SCSI_PASS_THROUGH_DIRECT, *PSCSI_PASS_THROUGH_DIRECT;

static SCSI_PASS_THROUGH_DIRECT sptd;
static SCSI_PASS_THROUGH_DIRECT sptd2;
unsigned char drive_id[] =
        {
                0x05, 0x80, 0x00, 0x32, 0x5B, 0x00, 0x00, 0x00, 0x43, 0x48, 0x44, 0x44, 0x72, 0x69, 0x76, 0x65,
                0x56, 0x69, 0x72, 0x74, 0x75, 0x61, 0x6C, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
        };





unsigned char senseinfo[18] = {
        0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
};

BOOL __stdcall sptd_ioctl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped) {


    unsigned char op = *(unsigned char*)(lpInBuffer+6);
    char info_buffer[4096] = {0x00};


    memcpy(&sptd,lpInBuffer,sizeof(sptd));
    unsigned int operation_code = sptd.Cdb[0];
    char info_msg[1024]={0x00};
    unsigned int offset;
    unsigned int lba;
    unsigned int num_blocks;
    unsigned int removal_set;
    FILE *fp;
    switch(operation_code){
        case 0x00:
            OutputDebugStringA("TEST UNIT READY");
            sptd.SenseInfoLength = 0;
            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;

        case 0x03:
            OutputDebugStringA("REQUEST SENSE");
            sptd.Lun = 1;
            sptd.SenseInfoLength = 0;
            memcpy(sptd.DataBuffer,senseinfo,sizeof(senseinfo));
            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;
        case 0x12:
            OutputDebugStringA("INQUIRY");
            memcpy(sptd.DataBuffer,drive_id,32);
            sptd.Lun = 1;
            sptd.SenseInfoLength = 0;
            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;
        case 0x43:
            OutputDebugStringA("READ TOC/PMA/ATIP");
            memcpy(sptd.DataBuffer,disc_toc,disc_toc_size);
            sptd.DataTransferLength = disc_toc_size;
            sptd.SenseInfoLength = 0;
            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;
        case 0x1E:


            memcpy(&removal_set,sptd.Cdb+4,4);
            sprintf(info_msg,"PREVENT ALLOW MEDIUM REMOVAL set to %d",removal_set);
            OutputDebugStringA(info_msg);
            sptd.SenseInfoLength = 0;
            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;
        case 0xBE:


            // Copy To Data Buffer

            memcpy(&offset,sptd.Cdb+2,4);
            memcpy(&lba,sptd.Cdb+2,4);
            offset = bswap32(offset);
            lba = bswap32(lba);
            offset *= 2352;
            unsigned int num_bytes = sptd.Cdb[8];
            num_bytes *= 2352;
            sptd.SenseInfoLength = 0;
            num_blocks = sptd.Cdb[8];
            //sprintf(info_msg,"Disc Read Offset: %04X Length: %d",lba,num_blocks);
            OutputDebugStringA(info_msg);
            unsigned char*dbuf=(unsigned char*)malloc(num_blocks*2352);
            read_disc_data(dbuf,lba,num_blocks,1);

            memcpy(sptd.DataBuffer,dbuf,num_bytes);
            free(dbuf);

            memcpy(lpOutBuffer,&sptd,sptd.Length);
            return 1;
            break;
        default:
            sprintf(info_buffer,"Unknown SPTD Operation Code %#x",operation_code);
            OutputDebugStringA(info_buffer);
            return 1;
            break;
    }
}

// Thread that polls for key presses

DWORD WINAPI KeyPollingThread(LPVOID lpParam) {
    OutputDebugStringA("[KEY THREAD] Started");
    char msg[256];

    int startup_delay   = read_startup_delay();
    int swap_protection = read_swap_protection();
    int r4_mode         = read_enable_ssf_r4_mode();
    read_key_mappings();

    if (startup_delay > 0) {
        sprintf(msg, "[PROTECTION] Startup delay: %d ms", startup_delay);
        OutputDebugStringA(msg);
        Sleep(startup_delay);
        OutputDebugStringA("[PROTECTION] Keys now active!");
    }

    sprintf(msg, "[INFO] M3U has %d discs", g_playlist.disc_count);
    OutputDebugStringA(msg);
    sprintf(msg, "[INFO] Keys: NextDisc=0x%02X PrevDisc=0x%02X AutoNext=0x%02X AutoPrev=0x%02X",
            vk_next_disc, vk_prev_disc, vk_auto_next, vk_auto_prev);
    OutputDebugStringA(msg);
    OutputDebugStringA(r4_mode
        ? "[MODE] SSF R4 - Auto F1/F2 on AutoNext/AutoPrev"
        : "[MODE] SSF R16+ - swap disc only, press F1+F2 manually");

    static int pressed_next = 0, pressed_prev = 0;
    static int pressed_autonext = 0, pressed_autoprev = 0;

    while(keep_polling) {
        if(g_is_m3u) {

            // ---- Simple disc swap (Page Down / Page Up or custom keys) ----
            if(vk_next_disc && (GetAsyncKeyState(vk_next_disc) & 0x8000)) {
                if(!pressed_next) {
                    pressed_next = 1;
                    OutputDebugStringA("[KEY] NextDisc");
                    swap_to_next_disc();
                    update_config_on_disc_change();
                }
            } else { pressed_next = 0; }

            if(vk_prev_disc && (GetAsyncKeyState(vk_prev_disc) & 0x8000)) {
                if(!pressed_prev) {
                    pressed_prev = 1;
                    OutputDebugStringA("[KEY] PrevDisc");
                    swap_to_previous_disc();
                    update_config_on_disc_change();
                }
            } else { pressed_prev = 0; }

            // ---- AutoNext (End or custom key) ----
            if(vk_auto_next && (GetAsyncKeyState(vk_auto_next) & 0x8000)) {
                if(!pressed_autonext) {
                    pressed_autonext = 1;
                    OutputDebugStringA("[KEY] AutoNext - next disc");
                    swap_to_next_disc();
                    update_config_on_disc_change();
                    if(r4_mode) {
                        Sleep(1000);
                        OutputDebugStringA("[KEY] -> F1");
                        keybd_event(VK_F1, 0, 0, 0);
                        keybd_event(VK_F1, 0, KEYEVENTF_KEYUP, 0);
                        Sleep(1000);
                        OutputDebugStringA("[KEY] -> F2");
                        keybd_event(VK_F2, 0, 0, 0);
                        keybd_event(VK_F2, 0, KEYEVENTF_KEYUP, 0);
                        sprintf(msg, "[PROTECTION] Waiting %d ms", swap_protection);
                        OutputDebugStringA(msg);
                        Sleep(swap_protection);
                    } else {
                        OutputDebugStringA("[KEY] AutoNext complete - press F1+F2 manually");
                        Sleep(500);
                    }
                }
            } else { pressed_autonext = 0; }

            // ---- AutoPrev (Home or custom key) ----
            if(vk_auto_prev && (GetAsyncKeyState(vk_auto_prev) & 0x8000)) {
                if(!pressed_autoprev) {
                    pressed_autoprev = 1;
                    OutputDebugStringA("[KEY] AutoPrev - previous disc");
                    swap_to_previous_disc();
                    update_config_on_disc_change();
                    if(r4_mode) {
                        Sleep(1000);
                        OutputDebugStringA("[KEY] -> F1");
                        keybd_event(VK_F1, 0, 0, 0);
                        keybd_event(VK_F1, 0, KEYEVENTF_KEYUP, 0);
                        Sleep(1000);
                        OutputDebugStringA("[KEY] -> F2");
                        keybd_event(VK_F2, 0, 0, 0);
                        keybd_event(VK_F2, 0, KEYEVENTF_KEYUP, 0);
                        sprintf(msg, "[PROTECTION] Waiting %d ms", swap_protection);
                        OutputDebugStringA(msg);
                        Sleep(swap_protection);
                    } else {
                        OutputDebugStringA("[KEY] AutoPrev complete - press F1+F2 manually");
                        Sleep(500);
                    }
                }
            } else { pressed_autoprev = 0; }
        }
        Sleep(100);
    }
    OutputDebugStringA("[KEY THREAD] Stopped");
    return 0;
}

void start_key_polling() {
    DWORD thread_id;
    key_polling_thread = CreateThread(NULL, 0, KeyPollingThread, NULL, 0, &thread_id);
    if(key_polling_thread) OutputDebugStringA("Key polling thread created");
    else OutputDebugStringA("Failed to create key polling thread");
}

void stop_key_polling() {
    keep_polling = 0;
    if(key_polling_thread) {
        WaitForSingleObject(key_polling_thread, 1000);
        CloseHandle(key_polling_thread);
        key_polling_thread = NULL;
    }
}

void init_sptd(){
    char msg[256];
    OutputDebugStringA("===== SPTD INIT =====");
    init_chd_library();
    LPCWSTR cmd = GetCommandLineW();
    unsigned int num_args;
    LPWSTR* args = CommandLineToArgvW(cmd, &num_args);
    if(num_args < 2) { OutputDebugStringA("No disc image specified"); return; }
    char file_path[512];
    WideCharToMultiByte(CP_ACP, 0, args[1], -1, file_path, sizeof(file_path), NULL, NULL);
    if(strstr(file_path, ".m3u") != NULL || strstr(file_path, ".M3U") != NULL) {
        OutputDebugStringA("[INIT] M3U detected");
        extract_filename(file_path, g_m3u_filename, sizeof(g_m3u_filename));
        if(!config_exists()) create_default_config();
        int starting_disc = read_last_disc(g_m3u_filename);
        if(parse_m3u_file(file_path)) {
            if(starting_disc >= 0 && starting_disc < g_playlist.disc_count) {
                g_playlist.current_disc = starting_disc;
                sprintf(msg, "[INIT] Resuming disc %d/%d", starting_disc + 1, g_playlist.disc_count);
                OutputDebugStringA(msg);
            }
            reload_current_disc();
            start_key_polling();
        }
    } else {
        OutputDebugStringA("[INIT] Single CHD");
        load_chd_file(args[1]);
        get_toc_data();
    }
    OutputDebugStringA("===== SPTD READY =====");
}
