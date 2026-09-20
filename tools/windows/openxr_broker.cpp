#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;
};

bool xr_ok(XrResult result, const char *what)
{
    if (XR_SUCCEEDED(result))
        return true;
    std::fprintf(stderr, "%s failed: %d\n", what, static_cast<int>(result));
    return false;
}

bool has_extension(const std::vector<XrExtensionProperties> &extensions,
                   const char *name)
{
    return std::any_of(
        extensions.begin(), extensions.end(),
        [name](const XrExtensionProperties &extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

bool luid_equal(const LUID &a, const LUID &b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

ComPtr<IDXGIAdapter1> find_adapter(const LUID &luid)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return {};

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(index, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            return {};

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
            continue;
        if (luid_equal(desc.AdapterLuid, luid))
            return adapter;
    }
    return {};
}

bool create_device(const XrGraphicsRequirementsD3D11KHR &requirements,
                   ComPtr<ID3D11Device> *device,
                   ComPtr<ID3D11DeviceContext> *context)
{
    const ComPtr<IDXGIAdapter1> adapter = find_adapter(requirements.adapterLuid);
    if (!adapter) {
        std::fprintf(stderr, "OpenXR-selected DXGI adapter was not found\n");
        return false;
    }

    const std::array<D3D_FEATURE_LEVEL, 7> candidates = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
    };
    std::vector<D3D_FEATURE_LEVEL> levels;
    for (const D3D_FEATURE_LEVEL level : candidates) {
        if (level >= requirements.minFeatureLevel)
            levels.push_back(level);
    }
    if (levels.empty()) {
        std::fprintf(stderr, "No compatible D3D11 feature level\n");
        return false;
    }

    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_9_1;
    const HRESULT hr = D3D11CreateDevice(
        adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        device->ReleaseAndGetAddressOf(),
        &obtained,
        context->ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11CreateDevice failed: 0x%08lx\n",
                     static_cast<unsigned long>(hr));
        return false;
    }

    std::printf("D3D11 feature level: 0x%04x\n",
                static_cast<unsigned>(obtained));
    return true;
}

int64_t choose_color_format(const std::vector<int64_t> &formats)
{
    const std::array<DXGI_FORMAT, 4> preferred = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
    };
    for (const DXGI_FORMAT candidate : preferred) {
        if (std::find(formats.begin(), formats.end(),
                      static_cast<int64_t>(candidate)) != formats.end())
            return static_cast<int64_t>(candidate);
    }
    return formats.empty() ? -1 : formats.front();
}

bool create_eye_swapchain(
    XrSession session,
    ID3D11Device *device,
    const XrViewConfigurationView &view,
    int64_t format,
    EyeSwapchain *out)
{
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = view.recommendedImageRectWidth;
    info.height = view.recommendedImageRectHeight;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;

    if (!xr_ok(xrCreateSwapchain(session, &info, &out->handle),
               "xrCreateSwapchain"))
        return false;
    out->width = static_cast<int32_t>(info.width);
    out->height = static_cast<int32_t>(info.height);

    uint32_t image_count = 0;
    if (!xr_ok(xrEnumerateSwapchainImages(
                   out->handle, 0, &image_count, nullptr),
               "xrEnumerateSwapchainImages(count)"))
        return false;

    out->images.resize(image_count);
    for (auto &image : out->images)
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
    if (!xr_ok(xrEnumerateSwapchainImages(
                   out->handle,
                   image_count,
                   &image_count,
                   reinterpret_cast<XrSwapchainImageBaseHeader *>(
                       out->images.data())),
               "xrEnumerateSwapchainImages(list)"))
        return false;

    out->rtvs.resize(image_count);
    for (uint32_t index = 0; index < image_count; ++index) {
        const HRESULT hr = device->CreateRenderTargetView(
            out->images[index].texture,
            nullptr,
            &out->rtvs[index]);
        if (FAILED(hr)) {
            std::fprintf(stderr,
                         "CreateRenderTargetView(%u) failed: 0x%08lx\n",
                         index, static_cast<unsigned long>(hr));
            return false;
        }
    }
    return true;
}

bool clear_eye(XrSwapchain swapchain,
               const std::vector<ComPtr<ID3D11RenderTargetView>> &rtvs,
               ID3D11DeviceContext *context,
               const float color[4])
{
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    if (!xr_ok(xrAcquireSwapchainImage(swapchain, &acquire, &index),
               "xrAcquireSwapchainImage"))
        return false;

    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (!xr_ok(xrWaitSwapchainImage(swapchain, &wait),
               "xrWaitSwapchainImage"))
        return false;

    ID3D11RenderTargetView *rtv = rtvs.at(index).Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);
    context->ClearRenderTargetView(rtv, color);

    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return xr_ok(xrReleaseSwapchainImage(swapchain, &release),
                 "xrReleaseSwapchainImage");
}

void destroy_swapchain(EyeSwapchain *eye)
{
    eye->rtvs.clear();
    eye->images.clear();
    if (eye->handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(eye->handle);
        eye->handle = XR_NULL_HANDLE;
    }
}

} // namespace

int main()
{
    uint32_t extension_count = 0;
    if (!xr_ok(xrEnumerateInstanceExtensionProperties(
                   nullptr, 0, &extension_count, nullptr),
               "xrEnumerateInstanceExtensionProperties(count)"))
        return 1;

    std::vector<XrExtensionProperties> extensions(
        extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!xr_ok(xrEnumerateInstanceExtensionProperties(
                   nullptr, extension_count, &extension_count,
                   extensions.data()),
               "xrEnumerateInstanceExtensionProperties(list)"))
        return 1;

    if (!has_extension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
        std::fprintf(stderr, "Runtime lacks %s\n",
                     XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
        return 2;
    }

    const char *enabled_extensions[] = {
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
    };

    XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
    instance_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::snprintf(instance_info.applicationInfo.applicationName,
                  XR_MAX_APPLICATION_NAME_SIZE,
                  "%s", "Open Citadel VR Broker");
    std::snprintf(instance_info.applicationInfo.engineName,
                  XR_MAX_ENGINE_NAME_SIZE,
                  "%s", "Open Citadel");
    instance_info.applicationInfo.applicationVersion = 1;
    instance_info.applicationInfo.engineVersion = 1;
    instance_info.enabledExtensionCount = 1;
    instance_info.enabledExtensionNames = enabled_extensions;

    XrInstance instance = XR_NULL_HANDLE;
    if (!xr_ok(xrCreateInstance(&instance_info, &instance),
               "xrCreateInstance"))
        return 3;

    XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(
            instance, &instance_properties))) {
        std::printf("OpenXR runtime: %s %u.%u.%u\n",
                    instance_properties.runtimeName,
                    XR_VERSION_MAJOR(instance_properties.runtimeVersion),
                    XR_VERSION_MINOR(instance_properties.runtimeVersion),
                    XR_VERSION_PATCH(instance_properties.runtimeVersion));
    }

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    if (!xr_ok(xrGetSystem(instance, &system_info, &system_id),
               "xrGetSystem")) {
        xrDestroyInstance(instance);
        return 4;
    }

    PFN_xrGetD3D11GraphicsRequirementsKHR get_requirements = nullptr;
    if (!xr_ok(xrGetInstanceProcAddr(
                   instance, "xrGetD3D11GraphicsRequirementsKHR",
                   reinterpret_cast<PFN_xrVoidFunction *>(&get_requirements)),
               "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)") ||
        !get_requirements) {
        xrDestroyInstance(instance);
        return 5;
    }

    XrGraphicsRequirementsD3D11KHR requirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!xr_ok(get_requirements(instance, system_id, &requirements),
               "xrGetD3D11GraphicsRequirementsKHR")) {
        xrDestroyInstance(instance);
        return 6;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!create_device(requirements, &device, &context)) {
        xrDestroyInstance(instance);
        return 7;
    }

    XrGraphicsBindingD3D11KHR binding{
        XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device.Get();

    XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
    session_info.next = &binding;
    session_info.systemId = system_id;
    XrSession session = XR_NULL_HANDLE;
    if (!xr_ok(xrCreateSession(instance, &session_info, &session),
               "xrCreateSession")) {
        xrDestroyInstance(instance);
        return 8;
    }

    XrReferenceSpaceCreateInfo space_info{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    XrSpace local_space = XR_NULL_HANDLE;
    if (!xr_ok(xrCreateReferenceSpace(session, &space_info, &local_space),
               "xrCreateReferenceSpace")) {
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 9;
    }

    uint32_t view_count = 0;
    if (!xr_ok(xrEnumerateViewConfigurationViews(
                   instance, system_id,
                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                   0, &view_count, nullptr),
               "xrEnumerateViewConfigurationViews(count)") ||
        view_count != 2) {
        std::fprintf(stderr, "Expected 2 primary stereo views, got %u\n",
                     view_count);
        xrDestroySpace(local_space);
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 10;
    }

    std::vector<XrViewConfigurationView> view_configs(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!xr_ok(xrEnumerateViewConfigurationViews(
                   instance, system_id,
                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                   view_count, &view_count, view_configs.data()),
               "xrEnumerateViewConfigurationViews(list)")) {
        xrDestroySpace(local_space);
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 11;
    }

    uint32_t format_count = 0;
    if (!xr_ok(xrEnumerateSwapchainFormats(
                   session, 0, &format_count, nullptr),
               "xrEnumerateSwapchainFormats(count)")) {
        xrDestroySpace(local_space);
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 12;
    }
    std::vector<int64_t> formats(format_count);
    if (!xr_ok(xrEnumerateSwapchainFormats(
                   session, format_count, &format_count, formats.data()),
               "xrEnumerateSwapchainFormats(list)")) {
        xrDestroySpace(local_space);
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 13;
    }
    const int64_t color_format = choose_color_format(formats);
    if (color_format < 0) {
        std::fprintf(stderr, "Runtime exposed no swapchain formats\n");
        xrDestroySpace(local_space);
        xrDestroySession(session);
        xrDestroyInstance(instance);
        return 14;
    }
    std::printf("Swapchain DXGI format: %lld\n",
                static_cast<long long>(color_format));

    std::array<EyeSwapchain, 2> eyes{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        if (!create_eye_swapchain(
                session, device.Get(), view_configs[eye],
                color_format, &eyes[eye])) {
            for (auto &cleanup : eyes)
                destroy_swapchain(&cleanup);
            xrDestroySpace(local_space);
            xrDestroySession(session);
            xrDestroyInstance(instance);
            return 15;
        }
        std::printf("eye[%u] swapchain=%dx%d images=%zu\n",
                    eye, eyes[eye].width, eyes[eye].height,
                    eyes[eye].images.size());
    }

    bool session_running = false;
    bool exit_requested = false;
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    uint64_t submitted_frames = 0;

    while (!exit_requested) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto *changed =
                    reinterpret_cast<const XrEventDataSessionStateChanged *>(
                        &event);
                session_state = changed->state;
                std::printf("session state: %d\n",
                            static_cast<int>(session_state));

                if (session_state == XR_SESSION_STATE_READY &&
                    !session_running) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (!xr_ok(xrBeginSession(session, &begin),
                               "xrBeginSession")) {
                        exit_requested = true;
                        break;
                    }
                    session_running = true;
                } else if (session_state == XR_SESSION_STATE_STOPPING &&
                           session_running) {
                    xrEndSession(session);
                    session_running = false;
                } else if (session_state == XR_SESSION_STATE_EXITING ||
                           session_state == XR_SESSION_STATE_LOSS_PENDING) {
                    exit_requested = true;
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                exit_requested = true;
            }

            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }

        if (exit_requested)
            break;
        if (!session_running) {
            Sleep(10);
            continue;
        }

        XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame_state{XR_TYPE_FRAME_STATE};
        if (!xr_ok(xrWaitFrame(session, &wait_info, &frame_state),
                   "xrWaitFrame"))
            break;

        XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
        if (!xr_ok(xrBeginFrame(session, &begin_info), "xrBeginFrame"))
            break;

        std::array<XrView, 2> views{{
            {XR_TYPE_VIEW},
            {XR_TYPE_VIEW},
        }};
        XrViewState view_state{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo locate_info{XR_TYPE_VIEW_LOCATE_INFO};
        locate_info.viewConfigurationType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = frame_state.predictedDisplayTime;
        locate_info.space = local_space;

        uint32_t located_count = 0;
        XrResult locate_result = xrLocateViews(
            session, &locate_info, &view_state,
            static_cast<uint32_t>(views.size()), &located_count,
            views.data());

        std::array<XrCompositionLayerProjectionView, 2> projection_views{{
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        }};
        XrCompositionLayerProjection layer{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = local_space;

        bool submit_projection =
            frame_state.shouldRender == XR_TRUE &&
            XR_SUCCEEDED(locate_result) &&
            located_count == 2 &&
            (view_state.viewStateFlags &
             XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 &&
            (view_state.viewStateFlags &
             XR_VIEW_STATE_POSITION_VALID_BIT) != 0;

        if (submit_projection) {
            const float colors[2][4] = {
                {0.30f, 0.04f, 0.04f, 1.0f},
                {0.04f, 0.08f, 0.30f, 1.0f},
            };
            for (uint32_t eye = 0; eye < 2; ++eye) {
                if (!clear_eye(eyes[eye].handle, eyes[eye].rtvs,
                               context.Get(), colors[eye])) {
                    submit_projection = false;
                    break;
                }

                auto &projection = projection_views[eye];
                projection.pose = views[eye].pose;
                projection.fov = views[eye].fov;
                projection.subImage.swapchain = eyes[eye].handle;
                projection.subImage.imageRect.offset = {0, 0};
                projection.subImage.imageRect.extent = {
                    eyes[eye].width, eyes[eye].height};
                projection.subImage.imageArrayIndex = 0;
            }
        }

        const XrCompositionLayerBaseHeader *layers[1] = {};
        if (submit_projection) {
            layer.viewCount = 2;
            layer.views = projection_views.data();
            layers[0] =
                reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
        }

        XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount = submit_projection ? 1u : 0u;
        end_info.layers = submit_projection ? layers : nullptr;
        if (!xr_ok(xrEndFrame(session, &end_info), "xrEndFrame"))
            break;

        if (submit_projection && (++submitted_frames % 300) == 0)
            std::printf("submitted frames: %llu\n",
                        static_cast<unsigned long long>(submitted_frames));
    }

    if (session_running)
        xrEndSession(session);
    for (auto &eye : eyes)
        destroy_swapchain(&eye);
    xrDestroySpace(local_space);
    xrDestroySession(session);
    xrDestroyInstance(instance);
    return 0;
}
