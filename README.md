# SSF CHD Patcher + M3U Multi-Disc Support  

Play Sega Saturn CHD files in SSF emulator with automatic multi-disc game support and hotswap feature. Good for LaunchBox and arcade cabinets with low-spec hardware.

# Compatibility  

Mostly for SSF_R16, but will be compatible with SSF_012_beta_R4 and partially with SSF_011_alpha_R5'' (but not lower then this).  
Can be compatible with versions newer then SSF_R16 but not tested. The proper HotSwap feature currently availible only for SSF_012_beta_R4

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

### Why It Works

SSF expects a physical CD-ROM drive with SCSI commands. This patch creates a virtual drive that speaks SCSI but reads from CHD files.

# License

This project is licensed under the **BSD 3-Clause License** (same as libchdr).

## Third-Party Components

- **libchdr**: BSD 3-Clause License - CHD decompression library from MAME project
- **Original chdssf**: No license specified - CHD hook concept by [batteryshark](https://github.com/batteryshark/chdssf)
- **M3U multi-disc support**: Original addition by this fork
