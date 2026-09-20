# Open Citadel

Open Citadel is an experimental native compatibility host for the original
**Epic Citadel 1.07 Android/UE3 build**. The active target is a native 32-bit
Windows application; Linux and other native hosts remain secondary targets.

It loads the original Android x86 game library directly on Windows instead of
running Android or an emulator. The Windows build currently starts UE3, reads
the donor assets, compiles the game's shaders, and renders the Epic Citadel
scene. Mouse clicks and drags are translated to the original touch controls,
Windows SDL audio handles the Java MP3-song/WAV-sound callbacks, and display
and mouse-look settings persist. A portable Windows ZIP now bundles the host,
runtime DLLs, donor importer, and dependency notices without proprietary game
data. WASD movement is confirmed working, and F2 opens an in-game settings
overlay for controls, performance, and display options. Native gamepad support
and OpenSL ES effects remain unfinished.
The project began from the compatibility substrate developed in
`i-port-katamari`, but Open Citadel is now a standalone project and is not
limited to PortMaster.

## Current runtime status

The Windows x86 host currently reaches real UE3 startup and renders on an
NVIDIA GeForce RTX 4090 through ANGLE. The bring-up has verified:

- Win32 ELF32 mapping/relocation and native import resolution
- fake Java/JNI activity and `JNI_OnLoad` registration
- original UE3 OBB and cooked asset reads
- GLES through ANGLE, with 292 shaders and programs compiled/linked
- ATITC fallback using the texture caches shipped in the donor
- visible 3D scene rendering; mouse click/drag reaches the game's touch UI
- native SDL audio device started the donor's `town_render` MP3 callback
- MP3/WAV decoding and playback tests, plus eight Windows loader/ABI/input/
  settings/audio tests

This is a working bring-up, not a finished port. The Java audio callback path
now plays donor music and supports WAV sound callbacks. WASD movement has been
confirmed; native gamepad input and OpenSL ES are not implemented yet.
Clean-machine validation and an installer remain future work; the portable ZIP
can be generated with CPack.

See [OPEN_CITADEL.md](OPEN_CITADEL.md) for detailed donor-format notes,
architecture, reverse-engineering findings, and the milestone tracker.

## Native Windows build

Use CMake 3.21 or newer and a vcpkg installation (set `VCPKG_ROOT` if it is
not already configured), then configure the 32-bit Windows host. The root
`vcpkg.json` supplies SDL2,
ANGLE, mpg123, pthreads, dirent, and zlib:

CMake also downloads and SHA-256 verifies the pinned Dear ImGui v1.92.9b
sources used for the Windows GLES2 settings overlay.

```powershell
cmake -S tools/windows -B build/windows-app-win32 `
  -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x86-windows `
  -DVCPKG_INSTALLED_DIR=build/vcpkg-installed `
  -DOPEN_CITADEL_BUILD_APP=ON
cmake --build build/windows-app-win32 --config Release --parallel
ctest --test-dir build/windows-app-win32 -C Release --output-on-failure
cpack --config build/windows-app-win32/CPackConfig.cmake -C Release `
  -G ZIP -B build/windows-app-win32/package
```

The resulting ZIP includes a Windows quick start, the donor importer, and
third-party runtime notices; it deliberately excludes game data. See
`WINDOWS_QUICKSTART.md` after extracting it.

Import your own XAPK if the donor has not already been imported:

```powershell
python tools/open_citadel_import.py EpicCitadel.xapk gamedata/epic-citadel-1.07
```

Run from the repository root with no arguments when the imported directory is
in that default location, or pass its path explicitly:

```powershell
.\build\windows-app-win32\Release\open-citadel.exe
.\build\windows-app-win32\Release\open-citadel.exe .\gamedata\epic-citadel-1.07
```

Clicking the ground walks to that destination. Holding and dragging either
mouse button looks around; W/A/S/D send a virtual movement-stick input. F3
captures the pointer for clickless relative camera look; press F3 or Escape to
release it. F2/settings and focus loss also release capture. While captured,
mouse motion feeds the game's right-stick axes; click-to-walk and
drag-look are available again when the pointer is released. On first launch,
the host creates
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`. F2 opens an in-game Dear
ImGui settings overlay. Its Game tab shows live FPS/frame time and controls
mouse-look sensitivity, vertical inversion, VSync, the next-launch FPS cap,
uncapped benchmark mode, native relative mouse look (enabled by default), and
the game's 3D render scale (50%, 75%, or 100%; 100% by default). The Controls
tab can rebind Forward, Backward, Left, and
Right; Escape cancels a rebind and Reset restores WASD. The Display tab accepts
custom width/height values and selects fullscreen for the next launch. Shift+F2
opens the legacy native dialog as a fallback. All options are saved in the
settings file.
`OPEN_CITADEL_CONFIG` can select a different settings file. Set
`OPEN_CITADEL_NATIVE_MOUSE_LOOK=0` to disable the F3 capture option.
`OPEN_CITADEL_NATIVE_MOUSE_LOOK_CAPTURE=1` starts with the pointer captured.

On Windows, the Game tab offers **Disable the game's 60 FPS cap next launch**.
The default is the game's 60 FPS cap; changing the option requires a restart.
The saved setting can also be overridden with `OPEN_CITADEL_UNCAP_FPS=1`.
VSync is independent:
when enabled it still synchronizes presentation to the display, while uncapped
mode with VSync off can use substantially more CPU/GPU. A 20-second uncapped
benchmark run on an RTX 4090 logged 1,265–1,475 frame submissions/s at
1280x720; actual rates depend on hardware and scene load.
Disabling only the game's 60 FPS cap can still leave presentation limited to
the monitor's refresh rate. The dedicated benchmark option disables both.
For a fully uncapped benchmark, choose **Uncapped benchmark mode next launch**
in the Game tab or set `OPEN_CITADEL_UNCAPPED_BENCHMARK=1`. This starts UE3 in
benchmark mode, removes its 60 FPS limit, and forces VSync off for that run;
the saved VSync preference is preserved.

The settings file stores the startup window size and fullscreen preference;
the Display tab and environment variables accept arbitrary sizes/aspect ratios.
Render scale applies on the next launch and is independent of window size and
aspect ratio. `OPEN_CITADEL_RESOLUTION_SCALE` can override it with `0.50`,
`0.75`, or `1.00`.
On Windows the window can also be resized live (minimum 320x240). Each resize
updates UE3 with the GL drawable dimensions and scales mouse/touch coordinates
to match; the windowed size is saved after resizing settles. A live resize from
1280x720 to 1203x720 was visually checked with the scene still rendering.
F11/Alt+Enter request borderless fullscreen, but that transition still needs a
separate visual validation pass. Environment variables override matching
settings-file values. Mouse sensitivity defaults to `1.0` (accepted range
`0.1`–`4.0`):

```powershell
$env:OPEN_CITADEL_MOUSE_SENSITIVITY = '1.5'
$env:OPEN_CITADEL_INVERT_MOUSE_Y = '1'
```

On Windows, choose a starting window size before launch:

```powershell
$env:OPEN_CITADEL_WIDTH = '1920'
$env:OPEN_CITADEL_HEIGHT = '1080'
.\build\windows-app-win32\Release\open-citadel.exe
```

Width and height are selected independently (320–7680 by 240–4320); there is no
preset aspect-ratio list. Startup rendering has been verified at 1024x768 (4:3)
and 2560x1080 (21:9 ultrawide), as well as 1920x1080. The Display tab's saved
startup size applies on the next launch; a running window can be dragged to
another size immediately.

For borderless desktop fullscreen, set `$env:OPEN_CITADEL_FULLSCREEN = '1'`
before launching. Fullscreen at the desktop's 3440×1440 size and windowed
1920×1080 startup have both rendered successfully. F1 shows help; Escape
sends Back to the game except while the settings overlay is open. WASD movement
through the guest joystick callback is confirmed working. Rebindable movement
controls are available from F2; gamepad behavior remains future work. VSync is
on by default and can be changed with
the Game tab or `OPEN_CITADEL_VSYNC=0`.
The host reports average FPS and frame time to its console once per second,
after the initial scene frames.

Windows audio uses SDL output with mpg123 for MP3 music and SDL decoding for
WAV effects. `LOADER_TRACE=1` prints audio-device and callback diagnostics.
The UE3 OpenSL ES path is a separate, unfinished compatibility item.

## Donor policy

This repository does **not** distribute Epic Citadel APK/XAPK/OBB files,
`libUnrealEngine3.so`, or other proprietary game data. Supply your own Epic
Citadel 1.07 donor.

Import a donor with:

```bash
python3 tools/open_citadel_import.py EpicCitadel.xapk game/
```

The importer validates the expected package/version/native hash and extracts
the runtime layout while preserving the original UE3 OBB.

## Build

x86 host:

```bash
make open-citadel-x86
```

ARMHF / PortMaster-oriented target:

```bash
make open-citadel-armhf
```

GitHub Actions builds and smoke-tests the i386 compatibility host and packages
a portable runtime used for private-donor testing.

## Lineage

The loader/JNI/thunk compatibility substrate originated in
[`xXJSONDeruloXx/i-port-katamari`](https://github.com/xXJSONDeruloXx/i-port-katamari).
Open Citadel carries that substrate in-tree so it is independently buildable.

## License

See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
