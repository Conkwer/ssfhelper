/*
 * SSF Helper - Launch SSF with CHD support + CPU Affinity
 * Sets affinity to cores 0,1 (affinity mask 0x3) - equivalent to "start /affinity 3"
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <Windows.h>
#include <commdlg.h>

static char inject_dll_path[4096];
static char full_exe_path[4096];
static std::string chd_file_path;

void createShellcode(int ret, int str, unsigned char** shellcode, int* shellcodeSize)
{
    auto* retChar = reinterpret_cast<unsigned char*>(&ret);
    auto* strChar = reinterpret_cast<unsigned char*>(&str);
    int api = reinterpret_cast<int>(GetProcAddress(LoadLibraryA("kernel32.dll"), "LoadLibraryA"));
    auto* apiChar = reinterpret_cast<unsigned char*>(&api);

    unsigned char sc[] = {
            // Push ret
            0x68, retChar[0], retChar[1], retChar[2], retChar[3],
            // Push all flags
            0x9C,
            // Push all register
            0x60,
            // Push 0x66666666 (later we convert it to the string of injected dll)
            0x68, strChar[0], strChar[1], strChar[2], strChar[3],
            // Mov eax, 0x66666666 (later we convert it to LoadLibrary address)
            0xB8, apiChar[0], apiChar[1], apiChar[2], apiChar[3],
            // Call eax
            0xFF, 0xD0,
            // Pop all register
            0x61,
            // Pop all flags
            0x9D,
            // Ret
            0xC3
    };

    *shellcodeSize = 22;
    *shellcode = static_cast<unsigned char*>(malloc(22));
    memcpy(*shellcode, sc, 22);
}

bool openFileDialog(std::string& selectedPath) {
    OPENFILENAME ofn;
    char fileName[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "CHD Files (*.chd)\0*.chd\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Select Sega Saturn CHD image";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileName(&ofn)) {
        selectedPath = fileName;
        return true;
    }
    return false;
}

void launchSSF(const std::string& chdPath) {
    unsigned char* shellcode;
    int shellcodeLen;

    LPVOID remote_dllStringPtr;
    LPVOID remote_shellcodePtr;

    CONTEXT ctx;

    // Get the Full DLL Path for our injection
    GetFullPathName("ssf_patch.dll", 4096, inject_dll_path, nullptr);

    // Create Process SUSPENDED
    PROCESS_INFORMATION pi;
    STARTUPINFOA Startup;

    // Build command line with CHD path if provided
    std::string cmd_line;
    if (!chdPath.empty()) {
        cmd_line = "SSF.exe \"" + chdPath + "\"";
    } else {
        cmd_line = "SSF.exe";
    }

    ZeroMemory(&Startup, sizeof(Startup));
    ZeroMemory(&pi, sizeof(pi));
    GetFullPathName("SSF.exe", 4096, full_exe_path, nullptr);

    // CreateProcessA needs writable buffer
    std::vector<char> cmd_buffer(cmd_line.begin(), cmd_line.end());
    cmd_buffer.push_back('\0');

    if (!CreateProcessA(full_exe_path, cmd_buffer.data(), nullptr, nullptr, FALSE, 
                        CREATE_SUSPENDED, nullptr, nullptr, &Startup, &pi)) {
        MessageBox(nullptr, "Failed to launch SSF.exe\nMake sure SSFHelper.exe is in the same folder as SSF.exe", 
                   "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Set CPU affinity to cores 0 and 1 (affinity mask 0x3)
    // This is equivalent to "start /affinity 3"
    // Binary 0011 = cores 0,1 (may be same physical core on HT CPUs)
    DWORD_PTR affinityMask = 0x3;

    if (!SetProcessAffinityMask(pi.hProcess, affinityMask)) {
        // Affinity setting failed, but continue anyway
        char msg[256];
        sprintf(msg, "Warning: Failed to set CPU affinity (error %lu)\nSSF may have sync issues with VDP1/VDP2", 
                GetLastError());
        MessageBox(nullptr, msg, "Warning", MB_OK | MB_ICONWARNING);
    }

    remote_dllStringPtr = VirtualAllocEx(pi.hProcess, nullptr, strlen(inject_dll_path) + 1, 
                                         MEM_COMMIT, PAGE_READWRITE);

    ctx.ContextFlags = CONTEXT_CONTROL;
    GetThreadContext(pi.hThread, &ctx);

    createShellcode(ctx.Eip, reinterpret_cast<int>(remote_dllStringPtr), &shellcode, &shellcodeLen);

    // Allocate Memory for Shellcode
    remote_shellcodePtr = VirtualAllocEx(pi.hProcess, nullptr, shellcodeLen, 
                                         MEM_COMMIT, PAGE_EXECUTE_READWRITE);

    WriteProcessMemory(pi.hProcess, remote_dllStringPtr, inject_dll_path, strlen(inject_dll_path) + 1, nullptr);
    WriteProcessMemory(pi.hProcess, remote_shellcodePtr, shellcode, shellcodeLen, nullptr);

    // Set EIP To Shellcode
    ctx.Eip = reinterpret_cast<DWORD>(remote_shellcodePtr);
    ctx.ContextFlags = CONTEXT_CONTROL;
    SetThreadContext(pi.hThread, &ctx);

    ResumeThread(pi.hThread);

    Sleep(8000);

    VirtualFreeEx(pi.hProcess, remote_dllStringPtr, strlen(inject_dll_path) + 1, MEM_DECOMMIT);
    VirtualFreeEx(pi.hProcess, remote_shellcodePtr, shellcodeLen, MEM_DECOMMIT);

    free(shellcode);
}

int main(int argc, char* argv[]) {
    // Check if we have command line arguments (file path passed)
    if (argc > 1) {
        // Running from command line - keep console for debug output
        printf("Launching SSF with: %s\n", argv[1]);
        launchSSF(argv[1]);
    } else {
        // No arguments - show file picker (no console)
        if (openFileDialog(chd_file_path)) {
            launchSSF(chd_file_path);
        }
    }

    return 0;
}
