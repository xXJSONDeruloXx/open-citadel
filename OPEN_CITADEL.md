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

Audio and preferences add more callbacks and can be implemented in later
bring-up stages. Analytics callbacks are safe candidates for no-op behavior.

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

The validation sequence is:

1. ELF/bionic relocation
2. fake JNI registration and Java callbacks
3. Android asset access
4. UE3 filesystem startup
5. GLES2 context and shaders
6. audio compatibility
7. native keyboard, mouse and controller input
8. lifecycle and quality-of-life settings

The Windows loader memory backend and ELF32 ABI parser have dedicated Win32
tests, and the GLES resolver now has a 32-bit cdecl-to-stdcall regression test.
The native application target is still in bring-up: Windows replacements for
the POSIX runtime layer, a complete link, and an actual game launch are the next
gates. The existing Linux x86 CI build remains useful as a secondary platform
check, not as the Windows deliverable.

## Milestones

- [x] decode and validate XAPK/OBB donor
- [x] transactional donor importer
- [x] exact UE3JavaApp JNI signature inventory
- [x] Win32 memory backend and ELF32 ABI smoke tests
- [x] Win32 GLES cdecl-to-stdcall thunk regression test
- [ ] generic game profile separated from Katamari-specific host code
- [ ] native Windows application build and complete symbol resolution
- [ ] libandroid/AAsset compatibility
- [ ] UE3JavaApp fake class
- [ ] all Epic Citadel ELF imports resolve
- [ ] JNI_OnLoad succeeds and native methods register
- [ ] x86 UE3 initialization under CI
- [ ] UE3 opens `EpicCitadel.xxx`
- [ ] first GLES2 shader compile
- [ ] first draw / first frame
- [ ] ATITC fallback verified
- [ ] OpenSL ES compatibility
- [ ] controller callbacks
- [ ] WASD and mouse-look controls with rebindable settings
- [ ] window, resolution, sensitivity and input quality-of-life settings
- [ ] ARMHF CI build
- [ ] Linux and other native host backends
