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
fullscreen. Fresh Windows runs were visually checked at 1024x768 (4:3) and
2560x1080 (21:9 ultrawide); the scene filled each requested viewport without
letterboxing or obvious aspect distortion. Width and height are independently
selectable within 320–7680 by 240–4320, with no preset aspect-ratio list. The
Windows window is resizable at runtime (320x240 minimum). On
`SDL_WINDOWEVENT_SIZE_CHANGED`, the host queries the GL drawable size, updates
the guest through
`NativeCallback_PostInitUpdate`, and scales mouse/touch input into drawable
pixels. The windowed size is saved after a 500 ms resize debounce. A live
1280x720-to-1203x720 resize was visually checked with the scene still rendering.
Startup borderless fullscreen was visually verified at 3440x1440: the host
window, drawable, and UE3 guest all reported that size, and the scene filled
the display. F11/Alt+Enter request borderless fullscreen toggles, but the live
hotkey transition remains to be visually verified. A Windows settings file is created under
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`; width, height, fullscreen,
VSync, the FPS cap, benchmark mode, render scale, mouse sensitivity, vertical
inversion, native mouse-look availability, and movement keys are persistent;
`OPEN_CITADEL_CONFIG` can select another file.
F2 opens the in-game Dear ImGui settings overlay. Its Game tab shows live
frame-rate/frame-time readings and controls mouse-look, live VSync, and
next-launch FPS/benchmark/render-scale modes. F3 captures the pointer for
clickless relative camera look; F3 or Escape releases it, and the settings
overlay or focus loss also releases it. The Game tab can disable this feature;
it is enabled by default. The captured pointer drives the game's existing
right-stick axes through the native controller callback, avoiding touch
emulation in this mode; click-and-drag remains available as a fallback. Set
`OPEN_CITADEL_NATIVE_MOUSE_LOOK=0` to disable F3 capture or
`OPEN_CITADEL_NATIVE_MOUSE_LOOK_CAPTURE=1` to start captured. The donor app
uses 50%, 75%, and
100% render-scale tiers; the desktop defaults to 100%. Render scale is
independent of window size and aspect ratio. Controls can rebind movement keys
or reset to WASD. Display accepts arbitrary window width/height and a fullscreen
preference for the next launch. Benchmark mode starts UE3 with `-benchmark` and
forces VSync off without changing the saved VSync preference. Shift+F2 retains
the native options dialogs as a fallback.
Display-mode, FPS-cap, benchmark-mode, and render-scale edits take effect after
restarting, and matching environment variables override the saved values. Set
`OPEN_CITADEL_UNCAPPED_BENCHMARK=1` to select benchmark mode from the command
line, or `OPEN_CITADEL_RESOLUTION_SCALE` to `0.50`, `0.75`, or `1.00` to
override internal rendering scale. VSync defaults on;
`OPEN_CITADEL_VSYNC=0` disables it. The game defaults
to its 60 FPS cap; set `OPEN_CITADEL_UNCAP_FPS=1` or use the F2 option to
disable it. Disabling the game cap alone does not disable VSync, which can
still limit presentation to the monitor's refresh rate. With VSync off, the
1280x720 Windows build measured about 59.6 FPS with the game cap enabled. A
12-second normal (non-benchmark) run with both the game cap and VSync disabled
logged 1,291–1,418 frame submissions/s. With the game cap disabled and VSync
on, the same machine presented at 239.5–240.6 FPS on its 240 Hz display. A
separate 20-second uncapped benchmark run on an RTX 4090 logged 1,264–1,395
frame submissions/s in a fresh validation. Startup reported the game cap
disabled and VSync off, so the host presentation path is not capped at 250 or
299 FPS. These are host swap submissions, not monitor scanout or a separate
guest benchmark counter; results vary by hardware and scene load. Uncapped mode
can significantly increase CPU/GPU use. The host reports average submission rate
and frame time once per second after the initial scene frames.

The Windows host maps configurable movement keys (W/A/S/D by default) to a
virtual left-stick axis, releases held movement on focus loss, and keeps mouse
click-to-walk and drag-to-look input. WASD movement has been confirmed working
in a live user test; rebinding is not yet confirmed. The standard SDL gamepad
button/axis mapping now has a virtual-controller regression test for left-stick
X/Y and the A button, but physical-controller and in-game behavior are not yet
confirmed. Mouse sensitivity and vertical inversion can be configured at
launch. Input tracing records SDL key events and the guest callback results for
keyboard and virtual-stick delivery. SDL opens a native
44.1 kHz stereo device; the donor's `town_render` MP3 and a real donor WAV have
both decoded in tests, and a no-`-nosound` game run reached the song callback.
The Windows OpenSL ES shim implements engine/output-mix/player creation,
volume/play interfaces, and Android PCM16 mono/stereo buffer queues through the
SDL mixer. A dummy-device integration test verifies completion callbacks,
re-enqueue, queue state/clear, volume, and teardown. The latest live scene
startup smoke run did not enter the OpenSL ES path, so in-game effect behavior
remains unverified. Physical gamepad behavior and a clean-machine/CI run remain
open.
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
SDL mixer has dummy-device WAV/stream and OpenSL queue lifecycle tests. The
native application reaches a rendered scene and confirmed WASD movement; the
remaining gates include movement-rebind and physical gamepad verification,
live OpenSL ES effect verification, settings polish, and automated Windows
validation. The existing
Linux x86 CI build remains a useful secondary platform check, not the current
deliverable.

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
- [x] SDL virtual-controller button and left-stick mapping regression test
- [x] SDL audio output and Java MP3/WAV callback mixer
- [ ] generic game profile separated from Katamari-specific host code
- [x] manual end-to-end WASD movement verification
- [ ] movement-key rebind verification
- [ ] native gamepad behavior
- [ ] verify OpenSL ES PCM effects through the actual in-game path (shim and queue test are implemented)
- [x] persistent Windows settings, custom display sizes, render scale, and live performance overlay
- [x] live visual validation of the ImGui settings panel and tab mouse navigation
- [ ] F2 open/close shortcut validation with physical keyboard (desktop key injection was inconclusive)
- [x] optional uncapped benchmark mode with VSync disabled for measurement
- [x] live Windows viewport resizing with guest size updates and input scaling
- [x] extracted Windows ZIP smoke run with external donor data and clean exit
- [ ] live fullscreen switching visual verification
- [ ] robust lifecycle/frame-pacing validation and clean-machine packaging
- [ ] Windows CI launch/smoke test with donor data supplied privately
- [ ] ARMHF CI build
- [ ] Linux and other native host backends
