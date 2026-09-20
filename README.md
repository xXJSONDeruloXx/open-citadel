# Open Citadel

Open Citadel is an experimental native compatibility host for the original
**Epic Citadel 1.07 Android/UE3 build**. Linux is the currently running host;
native Windows bring-up has started, but the complete game is not yet playable
on Windows.

It loads the original Android native game library directly on Linux instead of
running a complete Android environment. The project began from the compatibility
substrate developed in `i-port-katamari`, but Open Citadel is now a standalone
project and is not limited to PortMaster.

## Current x86 status

The original x86 Android donor currently reaches real UE3 startup on Linux:

- Android ELF mapping/relocation
- all native imports resolved
- all 186 UE3 static constructors execute
- `JNI_OnLoad` succeeds
- fake Java/JNI activity and dynamic `RegisterNatives`
- original UE3 OBB parsing through `FFileManagerAndroid`
- GLES2 through Mesa
- 292/292 shaders compile
- 292/292 programs link
- ATC/ATITC texture fallback
- Android touch/keyboard/gamepad/lifecycle translation
- startup movie lifecycle compatibility under active bring-up

The current runtime investigation found that Epic's startup sequence blocks in
`GameThreadWaitForMovie()` until Java reports
`NativeCallback_MovieFinished()`. Open Citadel now completes that callback
when video is skipped; proper Linux video playback is the next parity step.

See [OPEN_CITADEL.md](OPEN_CITADEL.md) for detailed donor-format notes,
architecture, reverse-engineering findings, and the milestone tracker.

## Native Windows bring-up

The first Windows-specific slice moves guest ELF memory allocation,
protection, and instruction-cache flushing behind a host API. A 32-bit MSVC
smoke test for those operations can be built and run with:

```powershell
cmake -S tools/windows -B build/windows-host-memory -G "Visual Studio 17 2022" -A Win32
cmake --build build/windows-host-memory --config Release
ctest --test-dir build/windows-host-memory -C Release --output-on-failure
```

This validates only the loader memory substrate, not a runnable Windows game
build; the remaining POSIX host APIs and Windows graphics/runtime dependencies
are still to be ported.

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
