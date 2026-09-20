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
data. F2 also provides persistent movement-key rebinding. Live keyboard/gamepad
behavior and OpenSL ES effects still need work. Keyboard event translation and
host shortcuts are implemented, but native key delivery has not yet been
verified.
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
- MP3/WAV decoding and playback tests, plus seven Win32 ABI/input/settings tests

This is a working bring-up, not a finished port. The Java audio callback path
now plays donor music and supports WAV sound callbacks, but OpenSL ES is not
implemented and live keyboard/gamepad behavior still needs verification.
Clean-machine validation and an installer remain future work; the portable ZIP
can be generated with CPack.

See [OPEN_CITADEL.md](OPEN_CITADEL.md) for detailed donor-format notes,
architecture, reverse-engineering findings, and the milestone tracker.

## Native Windows build

Use CMake 3.21 or newer and a vcpkg installation (set `VCPKG_ROOT` if it is
not already configured), then configure the 32-bit Windows host. The root
`vcpkg.json` supplies SDL2,
ANGLE, mpg123, pthreads, dirent, and zlib:

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
mouse button looks around; W/A/S/D send a virtual movement-stick input. On
first launch, the host creates
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`. F2 opens a native settings dialog for
live mouse-look sensitivity, vertical inversion, and VSync changes; those
choices are saved. Select **Controls...** in that dialog to rebind Forward,
Backward, Left, and Right; Escape cancels a rebind and Reset to WASD restores
the defaults. Bindings persist in the same settings file.
`OPEN_CITADEL_CONFIG` can select a different settings file.

The settings file also stores window width/height and fullscreen preference;
those display choices apply on the next launch because live Win32 viewport
resizing/fullscreen switching is not reliable yet. Environment variables
override matching settings-file values. Mouse sensitivity defaults to `1.0`
(accepted range `0.1`–`4.0`):

```powershell
$env:OPEN_CITADEL_MOUSE_SENSITIVITY = '1.5'
$env:OPEN_CITADEL_INVERT_MOUSE_Y = '1'
```

On Windows, choose a windowed render size before launch; the window is fixed
after startup because live UE3 viewport resizing is not reliable yet:

```powershell
$env:OPEN_CITADEL_WIDTH = '1920'
$env:OPEN_CITADEL_HEIGHT = '1080'
.\build\windows-app-win32\Release\open-citadel.exe
```

For borderless desktop fullscreen, set `$env:OPEN_CITADEL_FULLSCREEN = '1'`
before launching. Fullscreen at the desktop's 3440×1440 size and windowed
1920×1080 startup have both rendered successfully. F11 and Alt+Enter currently
report that live display-mode changes are unavailable. F1 shows help; Escape
sends Back to the game. WASD movement is wired through the guest joystick
callback but still needs live end-to-end verification. Rebindable movement
controls are available from F2; gamepad behavior remains future work. VSync is
on by default and can be changed with F2 or `OPEN_CITADEL_VSYNC=0`.
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
