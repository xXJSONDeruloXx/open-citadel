# Open Citadel Quest ARM64 capability shell

This directory is the first standalone Quest milestone for the 6DoF work. It is
**donor-free** and intentionally does not run Epic Citadel yet.

It builds an `arm64-v8a` NativeActivity APK that:

1. initializes the Khronos Android OpenXR loader,
2. creates an OpenXR instance with `XR_KHR_android_create_instance`,
3. obtains the HMD system,
4. logs the runtime/system,
5. enumerates primary-stereo view recommendations.

It does not yet create a graphics-bound OpenXR session or eye swapchains. That
is the next milestone after this capability/ABI shell is validated on real
Quest 2/3 hardware.

## Requirements

- Android SDK 34
- Android NDK with CMake 3.22.1 support
- JDK 17+
- Gradle compatible with Android Gradle Plugin 8.7.3

The project uses the Khronos Android OpenXR loader AAR:

`org.khronos.openxr:openxr_loader_for_android:1.1.63`

## Build

From this directory with a suitable Gradle installation:

~~~bash
gradle :app:assembleDebug
~~~

The output APK is under:

~~~text
app/build/outputs/apk/debug/app-debug.apk
~~~

## Sideload and capture facts

~~~bash
adb install -r app/build/outputs/apk/debug/app-debug.apk

adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.product.cpu.abilist32
adb shell getprop ro.product.cpu.abilist64
adb shell getprop ro.zygote
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk

adb logcat -c
adb shell monkey -p org.opencitadel.vr 1
adb logcat -s OpenCitadelVR
~~~

Record the output in the hardware-validation notes. In particular, do not infer
ARMv7 process support from the Snapdragon CPU; use the OS ABI properties and an
actual canary if appropriate.

Expected successful log milestones include:

~~~text
Open Citadel Quest capability shell: pointer_width=64
runtime=...
system=...
primary_stereo_views=2
view[0] recommended=...
view[1] recommended=...
OpenXR capability probe complete
~~~

## Donor policy

The APK must never contain the Epic Citadel XAPK/APK/OBB, extracted assets, or
`libUnrealEngine3.so`. Future donor import happens locally after installation.

Audit a built APK with:

~~~bash
unzip -l app/build/outputs/apk/debug/app-debug.apk | tee /tmp/quest-apk.txt
! grep -Ei 'EpicCitadel|libUnrealEngine3|Textures_ATITC|Lighting_ATITC|\.obb|\.xapk' /tmp/quest-apk.txt
~~~

## Next hardware-gated step

Once this shell succeeds on Quest 2/3, add an EGL/OpenGL ES OpenXR session,
stereo swapchains, `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`, late
`xrLocateViews`, and controller actions while still remaining donor-free.
Only after that should the ARMv7 guest/JIT layer be integrated.
