#include "sptd.h"
#include "global.h"
#include "chd_helper.h"
#include "m3u.h"
#include <string.h>

static HANDLE ldll;
static int(*libchd_cdrom_open)(int);
static int(*libchd_cdrom_get_toc)(int,unsigned int*);
static int(*libchd_cdrom_read_data)(struct _cdrom_file*,unsigned int,unsigned char*,unsigned int,unsigned char);
static unsigned char* disc_toc = NULL;
static unsigned int disc_toc_size = NULL;

// Key polling thread variables
static HANDLE key_polling_thread = NULL;
static int keep_polling = 1;

bswap32(unsigned int x)
{
    return	((x << 24) & 0xff000000 ) |
              ((x <<  8) & 0x00ff0000 ) |
              ((x >>  8) & 0x0000ff00 ) |
              ((x >> 24) & 0x000000ff );
}

unsigned short bswap16(unsigned short x)
{
    /*LINTED*/
    return ((x << 8) & 0xff00) | ((x >> 8) & 0x00ff);
}


void get_toc_data(){
    // Free old TOC if exists
    if(disc_toc != NULL) {
        free(disc_toc);
        disc_toc = NULL;
    }

    disc_toc = (unsigned char*)malloc(12+(cdrf.cdtoc.numtrks*8));

    memset(disc_toc,0x00,12+(cdrf.cdtoc.numtrks*8));

    unsigned short toc_sz = 10+(cdrf.cdtoc.numtrks*8);
    toc_sz = bswap16(toc_sz);

    memcpy(disc_toc,&toc_sz,2);
    memset(disc_toc+2,0x01,1);
    memset(disc_toc+3,cdrf.cdtoc.numtrks&0xFF,1);

    unsigned int toc_buffer_track_offset = 4;
    unsigned int leadout_start = 0;

    for(int i=0;i< cdrf.cdtoc.numtrks;i++){
        unsigned char track_info[8] = {0x00};
        if(cdrf.cdtoc.tracks[i].subtype == 0x01){
            track_info[1] = 0x14;
        }else{
            track_info[1] = 0x10;
        }

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

// Original function - takes WCHAR* (wide string) like before
void load_chd_file(unsigned char* chd_path){
    int addr = libchd_cdrom_open(chd_path);
    if(addr == 1 || addr == 0){
        OutputDebugStringA("Opening CHD Failed!");
    }else{
        OutputDebugStringA("Opened CHD");
    }
    memcpy(&cdrf,addr,sizeof(cdrf));
}

// New function for M3U - converts char* to WCHAR* before calling load_chd_file
void load_chd_file_from_path(const char* chd_path_char){
    // Convert char* to WCHAR*
    WCHAR chd_path_wide[512];
    MultiByteToWideChar(CP_ACP, 0, chd_path_char, -1, chd_path_wide, 512);

    OutputDebugStringA("Loading CHD from char path:");
    OutputDebugStringA(chd_path_char);

    // Call original function with wide string
    load_chd_file((unsigned char*)chd_path_wide);
}

void init_chd_library(){
    ldll = LoadLibrary("libchd.dll");
    libchd_cdrom_open = GetProcAddress(ldll, "libchd_cdrom_open");
    libchd_cdrom_get_toc = GetProcAddress(ldll,"libchd_cdrom_get_toc");
    libchd_cdrom_read_data = GetProcAddress(ldll,"libchd_cdrom_read_data");
}

/*
	The Saturn Archive.org CHD Audio is Endian-Reversed for... I don't know, it is, we'll swap it while reading it out.
	Yes - I know this is a horrible way to swap this, I'll fix it later.
*/
void bswap_buffer(unsigned char* buffer,unsigned int num_bytes){
    for(int i=0;i<num_bytes;i+=2){

        buffer[i] ^= buffer[i+1];
        buffer[i+1] ^= buffer[i];
        buffer[i] ^= buffer[i+1];
    }
}


void read_disc_data(unsigned char*buffer,unsigned int offset,unsigned int len,unsigned int dtype){

    unsigned int lba_sz = 2352;
    unsigned dest_offset = 0;
    unsigned char* tb = (unsigned char*)malloc(2352);
    for(unsigned int i =offset;i<offset+len;i++){
        memset(tb,0x00,2352);
        int retval = libchd_cdrom_read_data(&cdrf, i,tb, dtype,1);
        if(retval == 0){
            libchd_cdrom_read_data(&cdrf,i,tb,7,1);
            bswap_buffer(tb,2352);
        }
        memcpy(buffer+dest_offset,tb,lba_sz);
        dest_offset+=2352;

    }
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
    OutputDebugStringA("Key polling thread started");
    
    while(keep_polling) {
        // Only check if we're in M3U mode
        if(g_is_m3u) {
            // Check Page Down (VK_NEXT = 0x22)
            if(GetAsyncKeyState(VK_NEXT) & 0x8000) {
                OutputDebugStringA("Page Down detected - swapping to next disc");
                swap_to_next_disc();
                Sleep(500);  // Debounce - wait before checking again
            }
            
            // Check Page Up (VK_PRIOR = 0x21)
            if(GetAsyncKeyState(VK_PRIOR) & 0x8000) {
                OutputDebugStringA("Page Up detected - swapping to previous disc");
                swap_to_previous_disc();
                Sleep(500);  // Debounce
            }
        }
        
        Sleep(100);  // Check every 100ms
    }
    
    OutputDebugStringA("Key polling thread stopped");
    return 0;
}

// Start the key polling thread
void start_key_polling() {
    DWORD thread_id;
    key_polling_thread = CreateThread(NULL, 0, KeyPollingThread, NULL, 0, &thread_id);
    if(key_polling_thread) {
        OutputDebugStringA("Key polling thread created - PageDown=next, PageUp=previous");
    } else {
        OutputDebugStringA("Failed to create key polling thread");
    }
}

// Stop the key polling thread
void stop_key_polling() {
    keep_polling = 0;
    if(key_polling_thread) {
        WaitForSingleObject(key_polling_thread, 1000);
        CloseHandle(key_polling_thread);
        key_polling_thread = NULL;
    }
}

void init_sptd(){
    OutputDebugStringA("Starting up SPTD Emulator");
    init_chd_library();

    // Get Target CHD/M3U Image from command line
    LPCWSTR cmd;
    cmd = GetCommandLineW();
    unsigned int num_args;
    LPWSTR* args = CommandLineToArgvW(cmd,&num_args);

    if(num_args < 2) {
        OutputDebugStringA("No disc image specified");
        return;
    }

    // Convert wide string to char for extension checking
    char file_path[512];
    WideCharToMultiByte(CP_ACP, 0, args[1], -1, file_path, sizeof(file_path), NULL, NULL);

    OutputDebugStringA("Loading disc image:");
    OutputDebugStringW(args[1]);

    // Check if M3U or CHD
    if(strstr(file_path, ".m3u") != NULL || strstr(file_path, ".M3U") != NULL) {
        // M3U playlist - multi-disc
        OutputDebugStringA("Detected M3U playlist");
        if(parse_m3u_file(file_path)) {
            OutputDebugStringA("M3U parsed successfully, loading first disc");
            reload_current_disc();
            start_key_polling();  // Start polling instead of hook
        } else {
            OutputDebugStringA("Failed to parse M3U file");
        }
    } else {
        // Single CHD file - pass wide string directly
        OutputDebugStringA("Detected single CHD file");
        load_chd_file(args[1]);  // Pass WCHAR* directly, not converted char*
        get_toc_data();
    }
}