# VR hardware validation checklist

Use this document when a coding agent has physical access to a PC VR headset,
Quest 2, Quest 3, or both. Do not convert an unchecked item into a README claim.

## Evidence policy

For every validation run record:

- branch and commit SHA,
- donor manifest hash/version (never upload donor files),
- headset model,
- runtime and runtime version,
- GPU/driver or Quest OS build,
- refresh rate,
- render resolution / render scale,
- exact command line and relevant environment variables,
- pass/fail for each checklist item,
- logs and screenshots that contain no proprietary donor files.

Keep captured logs small and redact local paths/usernames if needed.

## PC: OpenXR loader/runtime probe

### Runtime registration

Run:

~~~powershell
reg query "HKLM\WOW6432Node\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime
reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime
~~~

Record both results.

### Probe executable

Build the optional 32-bit probe:

~~~powershell
cmake -S tools/windows -B build/windows-vr-probe `
  -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x86-windows `
  -DVCPKG_INSTALLED_DIR=build/vcpkg-installed `
  -DOPEN_CITADEL_BUILD_OPENXR_PROBE=ON

cmake --build build/windows-vr-probe --config Release --parallel
.\build\windows-vr-probe\Release\open-citadel-openxr-probe.exe
~~~

Expected evidence:

- [ ] OpenXR instance created.
- [ ] runtime name/version printed.
- [ ] HMD system ID obtained.
- [ ] primary stereo configuration enumerated.
- [ ] exactly two primary stereo views reported for ordinary PC HMD runtime.
- [ ] recommended/max eye sizes printed.
- [ ] process architecture is Win32/x86.
- [ ] no proprietary donor data required.

If the runtime cannot supply an HMD system to a Win32 process:

- [ ] reproduce once with the 64-bit runtime registration confirmed.
- [ ] record the exact OpenXR error.
- [ ] stop trying to make the UE3 guest 64-bit.
- [ ] switch the implementation plan to the Win64 compositor broker.

## PC: non-VR regression

Before any headset-specific validation:

- [ ] configure/build the normal Win32 app.
- [ ] all existing CTest tests pass.
- [ ] donor imports successfully.
- [ ] non-VR Citadel still reaches the scene.
- [ ] mouse look works.
- [ ] click-to-walk works.
- [ ] WASD works.
- [ ] audio still plays.
- [ ] F2 settings still persist.
- [ ] VR-disabled startup shows no OpenXR dependency/runtime error.

A VR commit that breaks the non-VR host with VR disabled is not acceptable.

## PC: GLES tracing

Use a short deterministic route and capture the smallest useful trace.

Candidate environment variables on this branch:

~~~powershell
$env:OPEN_CITADEL_VR_TRACE = "1"
$env:OPEN_CITADEL_VR_TRACE_MATRICES = "1"
$env:OPEN_CITADEL_VR_TRACE_FRAMEBUFFERS = "1"
~~~

Capture separate runs for:

1. stationary camera,
2. horizontal rotation only,
3. pitch only,
4. forward/back translation,
5. a different launch resolution.

Validate:

- [ ] matrix traces are timestamped/frame-numbered.
- [ ] current program is known for each matrix upload.
- [ ] current FBO and viewport are known for each draw sample.
- [ ] at least one matrix changes predictably with yaw.
- [ ] at least one matrix changes predictably with pitch.
- [ ] translation-sensitive candidate(s) are identified.
- [ ] projection-sensitive candidate(s) are identified.
- [ ] UI/postprocess matrices are distinguished from the 3D scene.
- [ ] tracing off returns normal performance/behavior.

Do not hard-code a hook offset until the candidate is repeatable with the verified
donor hash.

## PC: true-stereo hard gate

This is the most important validation.

With a development build that intentionally exaggerates eye separation:

- [ ] left and right eye images are visibly different viewpoints.
- [ ] covering one eye at a time confirms the viewpoints are not copied mono.
- [ ] near geometry has stronger parallax than distant geometry.
- [ ] left/right are not swapped.
- [ ] vertical disparity is negligible in normal view.
- [ ] game simulation speed is unchanged from non-VR.
- [ ] animations do not advance twice per headset frame.
- [ ] view-independent offscreen passes are not needlessly doubled.

Then switch to runtime-provided OpenXR eye poses/FOV:

- [ ] IPD comes from OpenXR views, not a constant.
- [ ] asymmetric FOV is respected.
- [ ] near/far clipping is stable.
- [ ] no obvious postprocess or HUD double vision.

Stop/reassess if stereo requires running a full UE3 update twice.

## PC: 6DoF hard gate

Physical head tracking:

- [ ] yaw is 1:1.
- [ ] pitch is 1:1.
- [ ] roll is 1:1.
- [ ] lean left/right changes viewpoint.
- [ ] lean forward/back changes viewpoint.
- [ ] crouching changes viewpoint.
- [ ] no mouse-look approximation is used for physical head pose.
- [ ] body/pawn yaw can differ from HMD yaw.
- [ ] recenter/reference-space reset behaves predictably.

World scale:

- [ ] doorway width feels plausible.
- [ ] stair dimensions feel plausible.
- [ ] eye height feels plausible.
- [ ] a scale multiplier can correct perception without code changes.

Culling:

- [ ] lean around a doorway without geometry popping out.
- [ ] lean around a wall edge without large occlusion errors.
- [ ] inspect extreme IPD/exaggerated stereo test for edge-frustum clipping.
- [ ] CPU culling camera/frustum behavior is documented.

## PC: frame timing and latency

For the active headset refresh:

- [ ] xrWaitFrame drives the XR frame cadence.
- [ ] xrBeginFrame / xrEndFrame are balanced.
- [ ] xrLocateViews uses the returned predicted display time.
- [ ] view location occurs late enough that head motion is responsive.
- [ ] UE3 runs exactly one simulation step per XR frame.
- [ ] no arbitrary SDL_Delay controls headset pacing.
- [ ] missed-frame/reprojection rate is recorded.

Test at least one sustained traversal, not only the opening view.

## PC: controls and comfort

OpenXR actions:

- [ ] left controller pose.
- [ ] right controller pose.
- [ ] left stick.
- [ ] right stick.
- [ ] triggers.
- [ ] grip/squeeze.
- [ ] menu/back behavior.
- [ ] haptics.
- [ ] all inputs recover after system-menu focus loss.

Locomotion:

- [ ] room-scale movement always works.
- [ ] snap turn default is 45 degrees.
- [ ] smooth turn is opt-in.
- [ ] smooth locomotion is opt-in.
- [ ] point-to-walk mode can use the existing guest click path.
- [ ] teleport destination validation is safe before true teleport is enabled.
- [ ] seated mode has an adjustable height/turning path.
- [ ] no forced head bob/shake/smoothing is applied to HMD tracking.

## Quest 2 / Quest 3: ABI fact collection

On each headset:

~~~bash
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.product.cpu.abilist32
adb shell getprop ro.product.cpu.abilist64
adb shell getprop ro.zygote
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk
~~~

Record verbatim.

- [ ] Quest 2 values recorded.
- [ ] Quest 3 values recorded.
- [ ] no claim about ARMv7 execution made solely from Snapdragon hardware capability.
- [ ] optional ARMv7 canary APK attempted only if the OS reports a 32-bit ABI.
- [ ] if canary fails, exact install/launch error recorded.

## Quest: ARM64 OpenXR shell

Without any donor:

- [ ] APK is arm64-v8a.
- [ ] installs with adb install -r.
- [ ] launches as immersive HMD activity.
- [ ] creates OpenXR instance/system/session.
- [ ] creates stereo swapchains.
- [ ] both eyes clear/render.
- [ ] HMD predicted pose changes correctly.
- [ ] Touch controller actions enumerate.
- [ ] focus loss/system menu is handled.
- [ ] pause/resume is handled.
- [ ] headset removal/resume does not wedge the session.
- [ ] clean shutdown does not crash.
- [ ] APK inspection contains no Epic donor file.

## Quest: guest CPU/JIT bring-up

Before running the full UE3 donor, use tiny synthetic ARMv7 guest snippets.

- [ ] 32-bit guest address model is deterministic.
- [ ] basic integer call/return works.
- [ ] stack arguments work.
- [ ] host import callback round-trip works.
- [ ] guest callback back into JIT works.
- [ ] TLS strategy is validated.
- [ ] atomics strategy is validated.
- [ ] pthread synchronization strategy is validated.
- [ ] signal/setjmp/unwind limitations are documented.

Then donor milestones:

- [ ] ELF maps/relocates.
- [ ] all imports resolve.
- [ ] static constructors reach the same count as native host.
- [ ] JNI_OnLoad succeeds.
- [ ] UE3 asset/OBB reads succeed.
- [ ] first shader/program activity occurs.
- [ ] first real Citadel draw occurs.

Do not proceed by patching random guest instructions until the failing ABI boundary
is understood and reproducible.

## Quest: stereo/6DoF parity

Repeat the PC hard gates:

- [ ] true stereo.
- [ ] one simulation step.
- [ ] correct OpenXR FOV.
- [ ] HMD translation.
- [ ] body/head separation.
- [ ] culling while leaning.
- [ ] controller actions.
- [ ] snap turn.
- [ ] comfort locomotion.

## Quest: performance and thermals

Quest 2 baseline:

- [ ] 72 Hz requested/active.
- [ ] sustained representative traversal at 72 Hz.
- [ ] CPU/GPU levels recorded.
- [ ] thermal state recorded after a prolonged run.
- [ ] missed frames recorded.
- [ ] draw count and eye resolution recorded.

Quest 3:

- [ ] same 72 Hz baseline.
- [ ] 90 Hz tested separately.
- [ ] 90 Hz is not enabled by default unless it is sustained through the route.
- [ ] render scale differences from Quest 2 are documented.

For both:

- [ ] no CPU framebuffer readback in normal eye transport.
- [ ] view-independent render passes are shared where practical.
- [ ] render scale can be reduced without code changes.
- [ ] local texture-cache generation does not redistribute donor-derived files.

## Packaging audit

Windows ZIP:

- [ ] no XAPK/APK/OBB.
- [ ] no libUnrealEngine3.so.
- [ ] no extracted Citadel map/textures/audio/movie.
- [ ] OpenXR dependency behavior documented.
- [ ] non-VR launch remains possible.

Quest APK:

~~~bash
unzip -l app-release.apk | tee apk-files.txt
grep -Ei 'EpicCitadel|libUnrealEngine3|Textures_ATITC|Lighting_ATITC|\.obb|\.xapk' apk-files.txt
~~~

The grep should return nothing proprietary.

- [ ] stable signing key used for persistent sideload upgrades.
- [ ] donor is imported locally after installation.
- [ ] generated donor caches remain outside public artifacts.

## Final claim levels

Use these exact meanings in project documentation:

- **implemented**: code exists and unit/compile tests pass.
- **runtime-probed**: OpenXR/OS capability was observed on hardware.
- **rendering**: headset displays an image.
- **stereo**: independently positioned left/right views verified.
- **6DoF**: stereo plus physical HMD rotation/translation and sane culling.
- **playable VR**: 6DoF plus controls/locomotion/lifecycle.
- **Quest-ready**: ARM64 standalone build plus sustained device performance and
  donor-free packaging.

Never promote a milestone based only on code review or headless CI.
