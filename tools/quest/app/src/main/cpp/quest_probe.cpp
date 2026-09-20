#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/api-level.h>
#include <android/log.h>
#include <android/native_activity.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr const char *kTag = "OpenCitadelVR";
XrInstance g_instance = XR_NULL_HANDLE;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

const char *result_name(XrResult result)
{
    switch (result) {
    case XR_SUCCESS: return "XR_SUCCESS";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
    default: return "XR_ERROR/UNKNOWN";
    }
}

bool require_extension(const std::vector<XrExtensionProperties> &extensions,
                       const char *name)
{
    return std::any_of(
        extensions.begin(), extensions.end(),
        [name](const XrExtensionProperties &extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

bool initialize_loader(ANativeActivity *activity)
{
    PFN_xrInitializeLoaderKHR initialize_loader = nullptr;
    XrResult xr = xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction *>(&initialize_loader));
    if (XR_FAILED(xr) || !initialize_loader) {
        LOGE("xrGetInstanceProcAddr(xrInitializeLoaderKHR) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    info.applicationVM = activity->vm;
    info.applicationContext = activity->clazz;
    xr = initialize_loader(
        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&info));
    if (XR_FAILED(xr)) {
        LOGE("xrInitializeLoaderKHR failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }
    return true;
}

bool initialize_openxr(ANativeActivity *activity)
{
    LOGI("Open Citadel Quest capability shell: pointer_width=%zu api=%d",
         sizeof(void *) * 8, android_get_device_api_level());

    if (!initialize_loader(activity))
        return false;

    uint32_t extension_count = 0;
    XrResult xr = xrEnumerateInstanceExtensionProperties(
        nullptr, 0, &extension_count, nullptr);
    if (XR_FAILED(xr)) {
        LOGE("xrEnumerateInstanceExtensionProperties(count) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    std::vector<XrExtensionProperties> extensions(
        extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    xr = xrEnumerateInstanceExtensionProperties(
        nullptr, extension_count, &extension_count, extensions.data());
    if (XR_FAILED(xr)) {
        LOGE("xrEnumerateInstanceExtensionProperties(list) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    if (!require_extension(
            extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
        LOGE("runtime does not expose %s",
             XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
        return false;
    }

    const char *enabled_extensions[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
    };

    XrInstanceCreateInfoAndroidKHR android_info{
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = activity->vm;
    android_info.applicationActivity = activity->clazz;

    XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
    create_info.next = &android_info;
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::strncpy(create_info.applicationInfo.applicationName,
                 "Open Citadel VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(create_info.applicationInfo.engineName,
                 "Open Citadel", XR_MAX_ENGINE_NAME_SIZE - 1);
    create_info.applicationInfo.applicationVersion = 1;
    create_info.applicationInfo.engineVersion = 1;
    create_info.enabledExtensionCount = 1;
    create_info.enabledExtensionNames = enabled_extensions;

    xr = xrCreateInstance(&create_info, &g_instance);
    if (XR_FAILED(xr)) {
        LOGE("xrCreateInstance failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        g_instance = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
    xr = xrGetInstanceProperties(g_instance, &instance_properties);
    if (XR_SUCCEEDED(xr)) {
        LOGI("runtime=%s version=%u.%u.%u",
             instance_properties.runtimeName,
             XR_VERSION_MAJOR(instance_properties.runtimeVersion),
             XR_VERSION_MINOR(instance_properties.runtimeVersion),
             XR_VERSION_PATCH(instance_properties.runtimeVersion));
    }

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    xr = xrGetSystem(g_instance, &system_info, &system_id);
    if (XR_FAILED(xr)) {
        LOGE("xrGetSystem(HMD) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
    xr = xrGetSystemProperties(g_instance, system_id, &system_properties);
    if (XR_SUCCEEDED(xr)) {
        LOGI("system=%s vendor=%u max_swapchain=%ux%u layers=%u",
             system_properties.systemName,
             system_properties.vendorId,
             system_properties.graphicsProperties.maxSwapchainImageWidth,
             system_properties.graphicsProperties.maxSwapchainImageHeight,
             system_properties.graphicsProperties.maxLayerCount);
    }

    uint32_t view_count = 0;
    xr = xrEnumerateViewConfigurationViews(
        g_instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &view_count, nullptr);
    if (XR_FAILED(xr)) {
        LOGE("xrEnumerateViewConfigurationViews(count) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    std::vector<XrViewConfigurationView> views(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xr = xrEnumerateViewConfigurationViews(
        g_instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views.data());
    if (XR_FAILED(xr)) {
        LOGE("xrEnumerateViewConfigurationViews(list) failed: %s (%d)",
             result_name(xr), static_cast<int>(xr));
        return false;
    }

    LOGI("primary_stereo_views=%u", view_count);
    for (uint32_t index = 0; index < view_count; ++index) {
        const auto &view = views[index];
        LOGI("view[%u] recommended=%ux%u samples=%u max=%ux%u samples=%u",
             index,
             view.recommendedImageRectWidth,
             view.recommendedImageRectHeight,
             view.recommendedSwapchainSampleCount,
             view.maxImageRectWidth,
             view.maxImageRectHeight,
             view.maxSwapchainSampleCount);
    }

    LOGI("OpenXR capability probe complete; graphics/session bring-up is next");
    return view_count == 2;
}

void on_destroy(ANativeActivity *)
{
    if (g_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_instance);
        g_instance = XR_NULL_HANDLE;
    }
    LOGI("activity destroyed");
}

void on_pause(ANativeActivity *) { LOGI("activity paused"); }
void on_resume(ANativeActivity *) { LOGI("activity resumed"); }

} // namespace

extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity *activity,
                              void *, size_t)
{
    LOGI("ANativeActivity_onCreate");
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onResume = on_resume;

    if (!initialize_openxr(activity))
        LOGE("OpenXR capability shell initialization failed");
}
