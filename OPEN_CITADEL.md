# Open Citadel experiment

This project runs the original **Epic Citadel 1.07** Android UE3 binary
directly on desktop Windows, without Android. The first target is a native
32-bit Windows host with keyboard and mouse controls; Linux and other platforms
can follow as the compatibility layer becomes more portable.

The work is intentionally donor-driven. Proprietary Epic Citadel binaries and
content are never committed or distributed.

## Verified donor

The development donor is the Epic Citadel 1.07 XAPK:

- package: `com.epicgames.EpicCitadel`
- version name: `1.07`
- version code: `903107`
- ARM engine: `lib/armeabi-v7a/libUnrealEngine3.so`
- ARM engine SHA-256:
  `39f30710ea08c8f89db3e0a8a907813acbc4c1f9173255a8f458324b3d0454fa`
- expansion:
  `main.903107.com.epicgames.EpicCitadel.obb`
- expansion size: 118,119,179 bytes

The same APK also carries an x86 `libUnrealEngine3.so`, which is useful as a
host-side bring-up target.

## OBB format

The expansion is not ZIP and is not encrypted. It starts with:

```text
UE3AndroidOBB| 01 00 00
```

At offset `0x11` begins a repeated table:

```text
u32  filename_bytes_including_NUL
char filename[filename_bytes]
u64  payload_offset
u32  payload_size
```

The table ends at the first payload offset.

The verified donor contains exactly 380 records:

- table/payload boundary: `0x7050`
- all 380 payloads are contiguous
- the last payload ends exactly at byte 118,119,179
- stored paths are rooted as `..\Engine\...`, `..\UDKGame\...`, etc.

`tools/open_citadel_import.py` validates those invariants before extracting.

## Donor output

A successful import produces a normal UE3 filesystem rather than emulating the
OBB container at runtime:

```text
game/
├── .open-citadel-donor.json
├── assets/
│   └── UE3CommandLine.txt
├── lib/
│   └── armeabi-v7a/
│       └── libUnrealEngine3.so
├── Binaries/
├── Engine/
└── UDKGame/
    ├── AndroidTOC.txt
    ├── CookedAndroid/
    │   ├── EpicCitadel.xxx
    │   ├── AllShaders.bin
    │   ├── Textures_ATITC.tfc
    │   ├── Lighting_ATITC.tfc
    │   └── ...
    ├── Movies/
    └── Music/
```

The importer stages atomically and validates the engine hash, package/version
metadata, OBB structure, `AndroidTOC.txt`, and `EpicCitadel.xxx`.

## Native binary observations

Both Android binaries are ELF32 shared objects:

- ARM: ARMv7/EABI5
- x86: i386
- both depend on:
  `libz.so libstdc++.so libc.so libm.so liblog.so libdl.so libGLESv2.so libEGL.so libandroid.so`

Direct Android-native API use is small:

```text
AAssetManager_fromJava
AAssetManager_open
AAsset_close
AAsset_openFileDescriptor
__android_log_print
```

The engine imports GLES2 and only `eglGetProcAddress` directly from EGL.

The x86 binary has 281 unique undefined dynamic symbols. Most are covered by
the existing Katamari bionic/libc/GLES compatibility work.

## JNI contract

`classes.dex` contains exact Java method signatures. `JNI_OnLoad` locates
`com/epicgames/EpicCitadel/UE3JavaApp`, registers the native callbacks, and
caches the Java callbacks.

The Java callbacks needed by the native engine include:

```text
JavaCallback_GetAppCommandLine()Ljava/lang/String;
JavaCallback_GetAssetManager()Landroid/content/res/AssetManager;
JavaCallback_GetDepthSize()I
JavaCallback_GetDeviceModel()Ljava/lang/String;
JavaCallback_GetMainAPKExpansionName()Ljava/lang/String;
JavaCallback_GetPatchAPKExpansionName()Ljava/lang/String;
JavaCallback_GetPerformanceLevel()I
JavaCallback_GetResolutionScale()F
JavaCallback_GetSDKVersion()I
JavaCallback_HideReloader()V
JavaCallback_HideSplash()V
JavaCallback_IsExpansionInAPK()Z
JavaCallback_OpenSettingsMenu()V
JavaCallback_SetFixedSizeScale(F)V
JavaCallback_SetMaxPerformanceLevel(I)V
JavaCallback_ShowExitDialog()V
JavaCallback_ShowWebPage(Ljava/lang/String;)V
JavaCallback_ShutDownApp()V
JavaCallback_StartVideo(Ljava/io/FileDescriptor;JJZ)V
JavaCallback_StopVideo()V
JavaCallback_VideoAddTextOverlay(Ljava/lang/String;)V
```

Windows implements the Java audio callbacks with SDL output: MP3/WAV assets can
be decoded, songs are read from bounded file-descriptor ranges, and sound
IDs support play/stop/unload/volume. Preferences are persisted by the native
host. Analytics callbacks remain safe candidates for no-op behavior.

Important native callbacks exported by the engine include:

```text
NativeCallback_Initialize(IIFZ)Z
NativeCallback_InitEGLCallback()Z
NativeCallback_PostInitUpdate(II)V
NativeCallback_Cleanup()V
NativeCallback_InputEvent(IIIIJ)Z
NativeCallback_JoystickAxisEvent(IIIFJ)Z
NativeCallback_JoystickButtonEvent(IIIJ)Z
NativeCallback_KeyboardEvent(IIII)Z
NativeCallback_MovieFinished()V
NativeCallback_NetworkUpdate(ZZ)V
```

## Texture path

The donor is predominantly ATITC:

- `Textures_ATITC.tfc`: 76,602,463 bytes
- `Lighting_ATITC.tfc`: 16,116,982 bytes

The Katamari branch already contains an ATC decoder and
`glCompressedTexImage2D` fallback infrastructure. This is directly reusable.
The remaining gap to audit is ATC interpolated-alpha handling and making the
probe forward through the GLES2 path instead of assuming Katamari's GLES1
context.

## Native Windows strategy

The bundled x86 engine is especially useful because it eliminates ARM ABI
noise while bringing up the native Windows host. Windows itself must run a
32-bit process so the engine's pointers, relocations and calling conventions
match the guest ABI. The current toolchain is Visual Studio 2022 with SDL2 and
ANGLE; WSL is not part of the target runtime.

### Current Windows status

The native Win32 x86 application now builds with Visual Studio 2022 and the
dependencies in the root `vcpkg.json`. It runs without Android or WSL and, with
the imported Epic Citadel 1.07 data, completes UE3 startup and renders the
original scene and UI through ANGLE on the development PC. The default data
search, explicit directory argument, and `OPEN_CITADEL_GAME_DIR` override are
available.

The Windows compatibility layer now provides the ELF32 loader, JNI/activity
surface, Android asset/OBB reads, Bionic/POSIX shims, 32-bit guest `wchar_t`
handling, GLES calling-convention bridge, and packaged ATITC decoding. At
1280x720, the live scene has rendered with the original cooked assets and
shaders. Mouse clicks and held drags reach the game's touch UI and camera.
VSync defaults on, with `OPEN_CITADEL_VSYNC=0` as an override.
Windowed resolution is selected at launch with `OPEN_CITADEL_WIDTH` and
`OPEN_CITADEL_HEIGHT`; `OPEN_CITADEL_FULLSCREEN=1` starts borderless desktop
fullscreen. Startup rendering has also been verified at 1024x768 (4:3) and
2560x1080 (21:9 ultrawide); width and height are independently selectable
within 320–7680 by 240–4320, with no preset aspect-ratio list. The Windows
window is resizable at runtime (320x240 minimum). On `SDL_WINDOWEVENT_SIZE_CHANGED`,
the host queries the GL drawable size, updates the guest through
`NativeCallback_PostInitUpdate`, and scales mouse/touch input into drawable
pixels. The windowed size is saved after a 500 ms resize debounce. A live
1280x720-to-1203x720 resize was visually checked with the scene still rendering.
F11/Alt+Enter now request borderless fullscreen; that transition remains to be
visually verified. A Windows settings file is created under
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`; width, height, fullscreen,
VSync, the FPS cap, benchmark mode, mouse sensitivity, vertical inversion, and
movement keys are persistent; `OPEN_CITADEL_CONFIG` can select another file.
F2 opens the in-game Dear ImGui settings overlay. Its Game tab shows live
frame-rate/frame-time readings and controls mouse-look, live VSync, and
next-launch FPS/benchmark modes. Controls can rebind movement keys or reset to
WASD. Display accepts arbitrary window width/height and a fullscreen preference
for the next launch. Benchmark mode starts UE3 with `-benchmark` and forces
VSync off without changing the saved VSync preference. Shift+F2 retains the
native options dialogs as a fallback.
Display-mode, FPS-cap, and benchmark-mode edits take effect after restarting,
and matching environment variables override the saved values. Set
`OPEN_CITADEL_UNCAPPED_BENCHMARK=1` to select benchmark mode from the command
line. VSync defaults on;
`OPEN_CITADEL_VSYNC=0` disables it. The game defaults
to its 60 FPS cap; set `OPEN_CITADEL_UNCAP_FPS=1` or use the F2 option to
disable it. With VSync off, the 1280x720 Windows build measured about 59.6 FPS
with the cap. A 20-second uncapped benchmark run on an RTX 4090 logged
1,265–1,475 frame submissions/s. These are host swap submissions, not monitor
scanout rates; results vary by hardware and scene load. Uncapped mode can
significantly increase CPU/GPU use. The host reports average submission rate
and frame time once per second after the initial scene frames.

The Windows host maps configurable movement keys (W/A/S/D by default) to a
virtual left-stick axis, releases held movement on focus loss, and keeps mouse
click-to-walk and drag-to-look input. WASD movement has been confirmed working
in a live user test; rebinding and native gamepad input are not yet confirmed.
Mouse sensitivity and vertical inversion can be configured at launch. Input
tracing records SDL key events and the guest callback results for keyboard and
virtual-stick delivery. SDL opens a native
44.1 kHz stereo device; the donor's `town_render` MP3 and a real donor WAV have
both decoded in tests, and a no-`-nosound` game run reached the song callback.
OpenSL ES is not implemented, so engine-side effects may still be silent.
Gamepad behavior and a clean-machine/CI run remain open.
A CPack ZIP now packages the Windows host, runtime DLLs, donor importer, and
dependency notices while excluding the proprietary game data. Linux and other
native hosts are follow-on targets.

The validation sequence is:

1. ELF/bionic relocation
2. fake JNI registration and Java callbacks
3. Android asset access
4. UE3 filesystem startup
5. GLES2 context and shaders
6. default and uncapped frame pacing, plus window lifecycle
7. keyboard/gamepad actions and rebinding
8. audio compatibility
9. user-facing quality-of-life settings and packaging

The Windows loader memory backend and ELF32 ABI parser have dedicated Win32
tests, the GLES resolver has a 32-bit cdecl-to-stdcall regression test, and the
SDL mixer has a dummy-device WAV/stream lifecycle test. The native application
reaches a rendered scene and confirmed WASD movement; the remaining gates
include movement-rebind and gamepad verification, OpenSL ES effects, settings
polish, packaging, and automated Windows validation. The existing Linux x86 CI
build remains a useful secondary platform check, not the current deliverable.

## Milestones

- [x] decode and validate XAPK/OBB donor
- [x] transactional donor importer
- [x] exact UE3JavaApp JNI signature inventory
- [x] Win32 memory backend and ELF32 ABI smoke tests
- [x] Win32 GLES cdecl-to-stdcall thunk regression test
- [x] native Win32 x86 application build and dependency manifest
- [x] Windows replacements for the POSIX runtime layer
- [x] Android asset/OBB access and JNI startup
- [x] all exercised Epic Citadel ELF imports resolve
- [x] UE3 opens `EpicCitadel.xxx` and reaches rendered frames
- [x] first GLES2 shader compile/draw and ATITC fallback
- [x] default game-data discovery and explicit path overrides
- [x] launch-time window resolution and borderless fullscreen
- [x] windowed startup rendering at 4:3 and 21:9 aspect ratios
- [x] optional Windows uncapped FPS mode, verified against the 60 FPS default
- [x] mouse click/drag translated to the game's touch controls
- [x] Android input key/axis constants checked against ABI values
- [x] SDL audio output and Java MP3/WAV callback mixer
- [ ] generic game profile separated from Katamari-specific host code
- [x] manual end-to-end WASD movement verification
- [ ] movement-key rebind verification
- [ ] native gamepad behavior
- [ ] OpenSL ES compatibility
- [x] persistent Windows settings, custom display sizes, and live performance overlay
- [ ] live visual/input validation of the in-game ImGui overlay
- [x] optional uncapped benchmark mode with VSync disabled for measurement
- [x] live Windows viewport resizing with guest size updates and input scaling
- [ ] live fullscreen switching visual verification
- [ ] robust lifecycle/frame-pacing validation and clean-machine packaging
- [ ] Windows CI launch/smoke test with donor data supplied privately
- [ ] ARMHF CI build
- [ ] Linux and other native host backends
