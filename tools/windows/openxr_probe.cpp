#include <openxr/openxr.h>

#include <cstdint>
#include <cstdio>
#include <vector>

static const char *result_name(XrResult result)
{
    switch (result) {
    case XR_SUCCESS: return "XR_SUCCESS";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
    case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
    default: return "XR_ERROR/UNKNOWN";
    }
}

static int fail(const char *operation, XrResult result)
{
    std::fprintf(stderr, "%s failed: %s (%d)\n",
                 operation, result_name(result), static_cast<int>(result));
    return 1;
}

int main()
{
    std::printf("Open Citadel OpenXR capability probe\n");
    std::printf("process pointer width: %zu-bit\n", sizeof(void *) * 8);

    uint32_t extension_count = 0;
    XrResult xr = xrEnumerateInstanceExtensionProperties(
        nullptr, 0, &extension_count, nullptr);
    if (XR_FAILED(xr))
        return fail("xrEnumerateInstanceExtensionProperties(count)", xr);
    std::vector<XrExtensionProperties> extensions(
        extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    xr = xrEnumerateInstanceExtensionProperties(
        nullptr, extension_count, &extension_count, extensions.data());
    if (XR_FAILED(xr))
        return fail("xrEnumerateInstanceExtensionProperties(list)", xr);
    std::printf("instance extensions: %u\n", extension_count);

    XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::snprintf(create_info.applicationInfo.applicationName,
                  sizeof(create_info.applicationInfo.applicationName),
                  "%s", "Open Citadel XR Probe");
    create_info.applicationInfo.applicationVersion = 1;
    std::snprintf(create_info.applicationInfo.engineName,
                  sizeof(create_info.applicationInfo.engineName),
                  "%s", "Open Citadel");
    create_info.applicationInfo.engineVersion = 1;

    XrInstance instance = XR_NULL_HANDLE;
    xr = xrCreateInstance(&create_info, &instance);
    if (XR_FAILED(xr))
        return fail("xrCreateInstance", xr);

    XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
    xr = xrGetInstanceProperties(instance, &instance_properties);
    if (XR_FAILED(xr)) {
        xrDestroyInstance(instance);
        return fail("xrGetInstanceProperties", xr);
    }
    std::printf("runtime: %s %u.%u.%u\n",
                instance_properties.runtimeName,
                XR_VERSION_MAJOR(instance_properties.runtimeVersion),
                XR_VERSION_MINOR(instance_properties.runtimeVersion),
                XR_VERSION_PATCH(instance_properties.runtimeVersion));

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    xr = xrGetSystem(instance, &system_info, &system_id);
    if (XR_FAILED(xr)) {
        xrDestroyInstance(instance);
        return fail("xrGetSystem(HMD)", xr);
    }

    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
    xr = xrGetSystemProperties(instance, system_id, &system_properties);
    if (XR_FAILED(xr)) {
        xrDestroyInstance(instance);
        return fail("xrGetSystemProperties", xr);
    }
    std::printf("system: %s vendor=%u maxSwapchain=%ux%u maxLayers=%u\n",
                system_properties.systemName,
                system_properties.vendorId,
                system_properties.graphicsProperties.maxSwapchainImageWidth,
                system_properties.graphicsProperties.maxSwapchainImageHeight,
                system_properties.graphicsProperties.maxLayerCount);

    uint32_t view_count = 0;
    xr = xrEnumerateViewConfigurationViews(
        instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &view_count, nullptr);
    if (XR_FAILED(xr)) {
        xrDestroyInstance(instance);
        return fail("xrEnumerateViewConfigurationViews(count)", xr);
    }
    std::vector<XrViewConfigurationView> views(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xr = xrEnumerateViewConfigurationViews(
        instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views.data());
    if (XR_FAILED(xr)) {
        xrDestroyInstance(instance);
        return fail("xrEnumerateViewConfigurationViews(list)", xr);
    }

    std::printf("primary stereo views: %u\n", view_count);
    for (uint32_t i = 0; i < view_count; ++i) {
        const auto &view = views[i];
        std::printf(
            "view[%u]: recommended=%ux%u samples=%u max=%ux%u samples=%u\n",
            i,
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            view.recommendedSwapchainSampleCount,
            view.maxImageRectWidth,
            view.maxImageRectHeight,
            view.maxSwapchainSampleCount);
    }

    xrDestroyInstance(instance);
    return view_count == 2 ? 0 : 2;
}
