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

## Building from Source

```bash
cd ssf_patch
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

## Files

- `SSFHelper.exe` - Loader that patches SSF
- `ssf_patch.dll` - Main CHD/SCSI emulation
- `libchd.dll` - CHD reading library

## Credits

- Original CHD patch: [batteryshark/chdssf](https://github.com/batteryshark/chdssf)
- M3U multi-disc support: Added in this fork
- Based on Mednafen/Beetle Saturn implementation

## License

[GPL/Whatever the original used]