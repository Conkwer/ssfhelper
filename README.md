# SSF CHD Patcher + M3U Multi-Disc Support

Play Sega Saturn CHD files in SSF emulator with automatic multi-disc game support.

## What This Does

- Load CHD disc images directly in SSF
- Play multi-disc games (Sakura Taisen, Panzer Dragoon Saga, Shining Force III) with easy disc swapping
- Press **Page Down**/**Page Up** to switch discs - no menus needed

## Quick Setup

1. Put these files in your SSF folder:
   - `SSFHelper.exe`
   - `ssf_patch.dll`  
   - `libchd.dll`

2. Run: `SSFHelper.exe "game.chd"`

## Multi-Disc Games (M3U)

Create a text file named `game.m3u`:

```
Disc 1.chd
Disc 2.chd  
Disc 3.chd
```

Launch: `SSFHelper.exe "game.m3u"`

### During Gameplay

When game asks for next disc:

1. Press **Page Down** (switches to next disc internally)
2. Press **F1** (opens tray in SSF)
3. Press **F2** (closes tray)
4. Keep playing!

**Hotkeys:**
- `Page Down` = Next disc
- `Page Up` = Previous disc

## Why Antivirus May Flag This

This uses DLL injection to add features to SSF, which antivirus software sometimes flags as suspicious. **It's a false positive** - the same technique used by ReShade, ENB, and other game mods. Source code is fully available for inspection.

## Files

- `SSFHelper.exe` - Loader that patches SSF
- `ssf_patch.dll` - Main CHD/SCSI emulation
- `libchd.dll` - CHD reading library

## Credits

- Original CHD patch: [batteryshark/chdssf](https://github.com/batteryshark/chdssf)
- M3U multi-disc support: Added in this fork
- Inspired by Mednafen/Beetle Saturn implementation

## How It Works (Technical Overview)

### Architecture

SSFHelper uses DLL injection to intercept and enhance SSF's disc access:

1. **Loader** (`SSFHelper.exe`):
   - Launches SSF.exe
   - Injects `ssf_patch.dll` into SSF process
   - Passes CHD/M3U file path

2. **Patch DLL** (`ssf_patch.dll`):
   - Hooks Windows filesystem APIs (CreateFile, ReadFile)
   - Intercepts CD-ROM related calls
   - Implements SCSI Pass-Through Direct (SPTD) emulation

### Disc Access Flow

```
SSF → Windows API → Hook → ssf_patch → libchd → CHD file
```

### Key Components

**Filesystem Hooks**
- Patches `kernel32.dll` and `ntdll.dll` functions
- Redirects CD-ROM device access to CHD data
- Traps IOCTL_SCSI_PASS_THROUGH commands

**SCSI Emulation**
- Virtual SCSI device responds to INQUIRY, READ TOC, READ CD commands
- Translates SCSI CDB blocks to CHD sector reads
- Returns 2352-byte raw sector data with proper headers

**CHD Integration**
- Uses MAME's `libchd.dll` for CHD decompression
- Supports CHD v5 format (CD-ROM images)
- Handles data/audio track mixing

**M3U Multi-Disc**
- Parses M3U playlist files
- Maintains disc index and current disc state
- On Page Up/Down: unloads current CHD, loads next/previous
- Regenerates TOC (Table of Contents) for new disc

**Input Handling**
- Background thread polls `GetAsyncKeyState()` for Page Up/Down
- 100ms polling interval, 500ms debounce on keypress
- Works independently of SSF's window focus

### Data Flow for Disc Read

```
1. SSF issues SCSI READ CD command
2. Hook intercepts IOCTL request
3. sptd.c parses CDB (Command Descriptor Block)
4. Extracts LBA (sector) and block count
5. Calls libchd_cdrom_read_data()
6. CHD data decompressed into buffer
7. Audio sectors byte-swapped (Saturn quirk)
8. Data returned to SSF via SPTD buffer
```

### Memory Layout

- `cdrf` global structure holds current CHD file handle and TOC
- `disc_toc` buffer stores generated TOC data for SCSI queries
- Track info cached for fast TOC regeneration on disc swap

### Why It Works

SSF expects a physical CD-ROM drive with SCSI commands. This patch creates a virtual drive that speaks SCSI but reads from CHD files.