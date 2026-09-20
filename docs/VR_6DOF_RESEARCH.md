# Open Citadel 6DoF VR research and implementation plan

> Research baseline: the deep-research pass began from `4fba16acf3ea015272ce29414aafd25413f4127e`.
> This branch is based on the newer `d5336fcba4ca967afab459dbcddcba1f0716539c`
> (`feat: add uncapped benchmark mode`). That intervening benchmark/settings change
> does not alter the VR architecture described here.

## Executive recommendation

Open Citadel is unusually well positioned for a source-less VR conversion because
the host already owns the platform boundary around the original Epic Citadel 1.07
Android/UE3 binary:

- ELF loading/relocation and Android/Bionic/JNI compatibility are in-tree.
- The donor importer keeps proprietary Epic files outside the repository.
- Windows already boots the original x86 UE3 guest and renders the real scene.
- GLES calls are routed through project-owned thunk/probe layers.
- Guest input callbacks are already synthesized from host input.

The recommended architecture is therefore:

### PC VR

Keep the current 32-bit Windows guest process and add OpenXR outside UE3.
Start with a 32-bit OpenXR runtime probe. If the installed runtime exposes a
working 32-bit OpenXR path, keep the MVP single-process. If it does not, keep the
x86 UE3 guest intact and add a 64-bit compositor broker that imports GPU-shared
eye textures and submits them to OpenXR.

The decisive proof is **true stereo**, not simply displaying the existing
monoscopic framebuffer in a headset. The source-less renderer must produce two
independently positioned asymmetric eye views while advancing UE3 gameplay once.

### Quest 2 / Quest 3

Build a normal ARM64 OpenXR Android shell first. Treat any current ability to run
an ARMv7 APK on a particular headset firmware as a disposable experiment, not a
shipping assumption.

The donor UE3 engine is ARMv7/ELF32. A durable ARM64 Quest build therefore needs
one of:

1. an ARM64 UE3 guest binary (not currently available),
2. a separate 32-bit process that the OS actually permits and can integrate with
   the XR compositor, or
3. ARMv7 guest execution inside the ARM64 host.

The third option is the practical long-term direction. Dynarmic is a strong
candidate because it supports ARMv7-A guests, AArch64 hosts, Android, custom
address spaces, and host callbacks/instrumentation.

Do **not** begin the large ARMv7-on-AArch64 effort until the PC build has proven
that Citadel's closed renderer can be turned into correct stereo.

## Hard technical gates

### Gate 1: identify the camera/projection path

The first VR engineering target is to classify the guest's render passes and
matrix uploads. Instrument:

- `glCreateProgram`
- `glLinkProgram`
- `glGetActiveUniform`
- `glGetUniformLocation`
- `glUniformMatrix4fv`
- `glUseProgram`
- `glBindFramebuffer`
- `glFramebufferTexture2D`
- `glViewport`
- `glScissor`
- `glClear`
- `glDrawArrays`
- `glDrawElements`
- guest present / EGL swap

Capture program IDs, shader hashes, active uniforms, matrices, framebuffer,
viewport and draw counts while:

1. stationary,
2. rotating horizontally,
3. pitching,
4. translating,
5. changing FOV/resolution.

Classify matrices from observed behavior rather than assuming UE3 uniform names.

### Gate 2: true stereo without double simulation

The preferred source-less implementation is a GLES stereo multiplexer:

1. let UE3 perform one simulation/update,
2. classify the main view-dependent render pass,
3. bind the left-eye target,
4. replace the discovered view/projection values with the left OpenXR view,
5. issue the scene draw,
6. bind the right-eye target,
7. replace the same values with the right OpenXR view,
8. issue the scene draw again,
9. restore guest-visible state,
10. submit both eyes through `XrCompositionLayerProjection`.

Do not blindly duplicate every offscreen pass. Shadow maps, static reflections and
other view-independent intermediates should usually execute once. Build an FBO
graph first.

Use an intentionally exaggerated test eye separation during bring-up. If the two
eyes do not show obviously different viewpoints, the implementation is only
duplicating mono.

### Gate 3: actual positional 6DoF

HMD movement must not be translated into mouse-look.

Keep separate transforms for:

- body/pawn world transform,
- tracking origin,
- physical HMD pose,
- per-eye offset/projection,
- controller poses.

Conceptually:

```
WorldFromEye =
    WorldFromBody
  * BodyFromTrackingOrigin
  * TrackingOriginFromHead
  * HeadFromEye
```

The guest's CPU-side visibility/culling camera must also track the HMD center or
use a conservative stereo-union frustum. Otherwise leaning around a wall will
cause geometry to disappear even if the GPU matrices are correct.

World scale must be calibrated experimentally and exposed as a setting. Do not
hard-code a UE4-style centimeters assumption for this UE3 mobile build.

## OpenXR frame model

XR mode must be paced by OpenXR, not by the desktop SDL loop or the original
60 Hz cap.

Target loop:

```
poll XR events
xrWaitFrame()
xrBeginFrame()

if shouldRender:
    xrSyncActions()
    locate controller spaces
    acquire eye swapchain images
    run exactly one UE3 simulation frame
    xrLocateViews(predictedDisplayTime) as late as practical
    render left/right scene views
    release swapchain images

xrEndFrame(projection layer)
```

The application should provide accurate predicted poses and timely images.
Reprojection/timewarp remains the runtime/compositor's responsibility.

## PC OpenXR path

### First probe

Build a minimal 32-bit executable that only:

- enumerates instance extensions,
- creates an OpenXR instance,
- gets the HMD system,
- enumerates stereo view configuration,
- prints runtime/system/view information.

Also inspect runtime registration:

```powershell
reg query "HKLM\WOW6432Node\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime
reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime
```

If this works from Win32, the MVP can remain in one process.

### 64-bit broker fallback

If the runtime has no useful 32-bit path:

```
Win32 guest:
  UE3 -> GLES/ANGLE -> shareable D3D11 left/right textures
                         |
                         | shared handles + GPU synchronization
                         v
Win64 broker:
  open shared textures -> OpenXR swapchains -> xrEndFrame()
```

Never use full-frame CPU `glReadPixels` as the production bridge.

## Quest architecture

### ARM64 host shell first

Create an ARM64 native OpenXR APK independent of the guest. It should prove:

- OpenXR loader initialization,
- immersive activity lifecycle,
- stereo swapchains,
- predicted HMD poses,
- controller action bindings,
- focus/pause/resume handling,
- clean shutdown,
- donor-free packaging.

Suggested baseline:

- `compileSdk 34`
- `targetSdk 34`
- `minSdk 32`
- `arm64-v8a`
- Khronos OpenXR Android loader

On actual Quest 2/3 hardware record:

```bash
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.product.cpu.abilist32
adb shell getprop ro.product.cpu.abilist64
adb shell getprop ro.zygote
```

Do not infer 32-bit execution support from SoC capability.

### ARMv7 guest abstraction

Before adding a JIT, refactor guest execution behind an interface equivalent to:

```cpp
class GuestCpu {
public:
    virtual GuestAddress loadModule(...) = 0;
    virtual uint32_t call32(GuestAddress fn, const GuestCallArgs&) = 0;
    virtual void registerImport(std::string_view name,
                                HostImportCallback callback) = 0;
    virtual ~GuestCpu() = default;
};
```

Backends:

- `NativeX86GuestCpu` -> current Windows/Linux x86 path,
- `NativeArm32GuestCpu` -> current armhf path,
- `DynarmicArm32GuestCpu` -> Quest ARM64 path.

The JIT backend must explicitly bridge imported functions, callbacks, TLS,
atomics, pthread semantics, JNI pointers, and unwind/setjmp behavior. Avoid
spreading `#ifdef __aarch64__` throughout the loader/JNI code.

## Controller and locomotion architecture

Use OpenXR actions, not vendor-specific polling.

Initial action set:

```
left_grip_pose
left_aim_pose
right_grip_pose
right_aim_pose
move              vector2
turn              vector2
select            float/bool
squeeze           float
menu              bool
teleport          bool
haptic_left
haptic_right
```

Initial mappings:

| XR input | Host behavior |
|---|---|
| Left stick | existing guest virtual movement joystick |
| Right stick X | host body yaw; 45-degree snap by default |
| HMD pose | VR camera only |
| HMD translation | room-scale local offset |
| Right trigger | UI/select/click |
| Right aim pose | UI ray / teleport target |
| Menu/B/Y | back/pause/options |
| Haptics | OpenXR output action |

Comfort defaults:

- room-scale physical movement always active,
- teleport + 45-degree snap turn default,
- smooth locomotion opt-in,
- smooth turning opt-in,
- vignette available for artificial movement,
- seated height/turning mode,
- no artificial smoothing/head bob/shake applied to physical head pose.

The current click-to-walk path is useful as an intermediate "point-to-walk"
comfort mode but is not true teleport because traversal remains visible.

## Audio

Keep music/UI audio non-positional initially. Recover world effect events before
attempting full spatialization.

Target split:

```
guest audio calls
   +-- music/UI -> existing SDL path
   +-- world source(position/gain/loop)
         -> VR positional mixer / HRTF
HMD pose -> listener transform
```

OpenAL Soft is a reasonable cross-platform HRTF candidate. The listener should
follow HMD orientation, not only body yaw.

## Quest performance plan

Initial requirements:

- Quest 2: sustained 72 Hz baseline (~13.9 ms/frame).
- Quest 3: sustained 72 Hz first; evaluate 90 Hz (~11.1 ms/frame) second.
- Never label a mode stable from a cold-start benchmark; perform thermal soak.

Instrument separately:

CPU:
- guest simulation,
- guest JIT time on Quest,
- GL thunk overhead,
- stereo duplication,
- XR acquire/wait/release,
- audio.

GPU:
- depth/prepass,
- opaque,
- alpha/translucent,
- postprocess,
- UI,
- eye resolve/copy.

Counters:
- draws,
- indices/triangles,
- shader/program switches,
- texture binds,
- FBO changes,
- uploaded bytes,
- ATC fallbacks,
- eye resolution/render scale.

Sequential stereo should be the first implementation. Multiview is an
optimization only after shader/pass classification.

For Quest, derive a local ASTC cache from the user's donor rather than expanding
ATITC to RGBA every run. The cache must remain local and be keyed to donor hash;
it must never be committed or distributed.

## Build and packaging policy

All public artifacts remain donor-free. No Epic APK/XAPK/OBB, guest
`libUnrealEngine3.so`, map, texture cache, movie, music or extracted proprietary
asset may be committed to the repository or uploaded by public CI.

Windows releases should retain the current importer-driven model.

Quest should eventually support:

1. install public host APK,
2. user supplies/pushes their own valid 1.07 XAPK,
3. device validates/imports it,
4. locally generated derivative caches remain device-local.

## Implementation milestones

| Phase | Exit criterion |
|---|---|
| P0 baseline | desktop non-VR tests still pass; VR disabled by default |
| P1 XR shell | OpenXR runtime/system/views probe on PC |
| P2 render introspection | matrix/uniform/FBO traces identify candidate camera path |
| P3 true stereo | two genuinely different Citadel eye views; one simulation step |
| P4 real 6DoF | 1:1 head rotation + translation; scale and culling sane |
| P5 PC controls | XR actions, snap turn, smooth locomotion, seated mode |
| P6 PC polish | stable refresh, lifecycle, audio, packaging |
| P7 Quest shell | ARM64 OpenXR APK and ABI facts recorded from Quest 2/3 |
| P8 guest CPU | ARMv7 guest reaches initialization via ARM64 backend |
| P9 Quest render | guest GLES reaches XR stereo swapchains |
| P10 Quest controls | core PC VR interaction parity |
| P11 Quest perf | sustained Quest 2 72 Hz; Quest 3 90 Hz evaluated |
| P12 release | clean donor-free ZIP/APK, validation checklist complete |

## Autonomous coding-agent work packages

### WP1: shared VR math/settings

Implement and unit-test:

- pose/quaternion composition,
- asymmetric FOV projection,
- per-eye state structures,
- conservative stereo FOV union,
- world-scale multiplier,
- VR settings serialization.

Acceptance: tests run without an OpenXR runtime or headset and non-VR defaults
remain unchanged.

### WP2: PC OpenXR capability probe

Implement an optional Win32 OpenXR probe target.

Acceptance: compile-only CI when OpenXR SDK is available; on hardware/runtime
machine print runtime name/version, HMD system, stereo view count and recommended
eye dimensions.

Stop/reassess: if no 32-bit runtime can create a system on target machines, move
to the x64 broker design rather than attempting to make the UE3 guest 64-bit.

### WP3: GLES VR trace

Add opt-in, low-overhead tracing for program use, matrix uploads, framebuffers,
viewport and draws. Output must be deterministic enough to diff two short
captures.

Acceptance: desktop scene is unchanged with tracing off. With tracing on, a
hardware agent can correlate matrix values with mouse camera changes.

### WP4: PC stereo proof

Do not proceed to controllers/Quest before this passes.

Acceptance:
- exaggerated eye separation visibly changes geometry,
- no animation/time-speed doubling,
- exact OpenXR FOVs work,
- left/right are not swapped,
- near geometry has plausible parallax.

### WP5: 6DoF/culling

Acceptance:
- head yaw/pitch/roll 1:1,
- leaning changes viewpoint at expected physical scale,
- doorway/wall lean tests do not expose culling breakage,
- body yaw is independent of HMD yaw.

### WP6: Quest ARM64 shell

Acceptance on Quest 2 and Quest 3:
- installs/sideloads,
- launches immersive OpenXR session,
- clears/stereo-renders both eyes,
- reports ABI properties,
- controller poses/actions recover after system-menu focus loss,
- APK contains no donor data.

### WP7: ARMv7 guest execution

Build behind `GuestCpu`, not directly in renderer code.

Acceptance:
- loader maps the donor guest in a 32-bit virtual address model,
- known test guest functions execute correctly,
- imported host callbacks round-trip,
- JNI registration reaches the same milestone as native armhf before GLES work.

## Hardware validation checklist

See `docs/VR_HARDWARE_VALIDATION.md` on this branch. Every hardware-dependent
claim should be checked there before being promoted to "working" in the README.

## Sources

Primary/current references used by the research:

- Khronos OpenXR specification:
  https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html
- Khronos OpenXR SDK Source / hello_xr:
  https://github.com/KhronosGroup/OpenXR-SDK-Source
- Khronos loader/runtime registration:
  https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/specification/loader/runtime.adoc
- Meta native OpenXR:
  https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/
- Meta Quest manifest/release requirements:
  https://developers.meta.com/horizon/resources/publish-mobile-manifest/
- Meta locomotion best practices:
  https://developers.meta.com/horizon/design/locomotion-best-practices/
- Meta locomotion preferences:
  https://developers.meta.com/horizon/design/locomotion-user-preferences/
- Meta device optimization comparison:
  https://developers.meta.com/horizon/resources/device-optimization-comparison/
- Dynarmic:
  https://github.com/lioncash/dynarmic
- OpenAL Soft:
  https://github.com/kcat/openal-soft
- Epic's historical Oculus-ready UDK/Citadel announcement:
  https://www.unrealengine.com/blog/custom-udk-to-ship-with-all-oculus-rift-developer-kits

Historical Oculus/UDK material is useful as implementation archaeology, not as a
substitute for modern OpenXR or as evidence that stock UDK contains full UE3 C++
source.

## Non-negotiable stop conditions

An autonomous agent should stop and record evidence rather than papering over a
problem when any of these occur:

- stereo requires advancing UE3 twice per headset frame,
- a proposed Quest architecture assumes 32-bit process support without device
  evidence,
- a PC bridge requires full-frame CPU readback each eye,
- head pose is being approximated through mouse input,
- a binary hook is based on a guessed offset rather than donor hash/signature and
  repeatable discovery,
- public CI/release packaging includes proprietary donor content,
- changes regress the non-VR desktop host with VR disabled.
