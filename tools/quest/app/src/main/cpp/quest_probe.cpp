#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/api-level.h>
#include <android/log.h>
#include <android/native_activity.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr const char *kTag = "OpenCitadelVR";
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
};

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

std::atomic<bool> g_stop{false};
std::thread g_xr_thread;
XrInstance g_instance = XR_NULL_HANDLE;

const char *result_name(XrResult result)
{
    switch (result) {
    case XR_SUCCESS: return "XR_SUCCESS";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
        return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
    default: return "XR_ERROR/UNKNOWN";
    }
}

bool xr_ok(XrResult result, const char *operation)
{
    if (XR_SUCCEEDED(result))
        return true;
    LOGE("%s failed: %s (%d)", operation, result_name(result),
         static_cast<int>(result));
    return false;
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
    return xr_ok(
        initialize_loader(
            reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&info)),
        "xrInitializeLoaderKHR");
}

bool create_egl(EglState *egl)
{
    egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl->display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(egl->display, &major, &minor)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }
    LOGI("EGL %d.%d", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOGE("eglBindAPI failed: 0x%x", eglGetError());
        return false;
    }

    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(egl->display, config_attributes, &egl->config, 1,
                         &count) ||
        count < 1) {
        LOGE("eglChooseConfig failed: 0x%x", eglGetError());
        return false;
    }

    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    egl->context = eglCreateContext(
        egl->display, egl->config, EGL_NO_CONTEXT, context_attributes);
    if (egl->context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    const EGLint pbuffer_attributes[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    egl->surface = eglCreatePbufferSurface(
        egl->display, egl->config, pbuffer_attributes);
    if (egl->surface == EGL_NO_SURFACE) {
        LOGE("eglCreatePbufferSurface failed: 0x%x", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(egl->display, egl->surface, egl->surface,
                        egl->context)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }

    LOGI("OpenGL ES: %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    return true;
}

void destroy_egl(EglState *egl)
{
    if (egl->display == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (egl->surface != EGL_NO_SURFACE)
        eglDestroySurface(egl->display, egl->surface);
    if (egl->context != EGL_NO_CONTEXT)
        eglDestroyContext(egl->display, egl->context);
    eglTerminate(egl->display);
    *egl = {};
}

int64_t choose_color_format(const std::vector<int64_t> &formats)
{
    for (const int64_t preferred : {
             static_cast<int64_t>(GL_SRGB8_ALPHA8),
             static_cast<int64_t>(GL_RGBA8),
         }) {
        if (std::find(formats.begin(), formats.end(), preferred) != formats.end())
            return preferred;
    }
    return formats.empty() ? -1 : formats.front();
}

bool create_swapchain(XrSession session,
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

    uint32_t count = 0;
    if (!xr_ok(xrEnumerateSwapchainImages(out->handle, 0, &count, nullptr),
               "xrEnumerateSwapchainImages(count)"))
        return false;
    out->images.resize(count);
    for (auto &image : out->images)
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    return xr_ok(
        xrEnumerateSwapchainImages(
            out->handle, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(
                out->images.data())),
        "xrEnumerateSwapchainImages(list)");
}

void destroy_swapchain(EyeSwapchain *eye)
{
    eye->images.clear();
    if (eye->handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(eye->handle);
        eye->handle = XR_NULL_HANDLE;
    }
}

bool clear_eye(EyeSwapchain &eye, GLuint framebuffer, const float color[4])
{
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    if (!xr_ok(xrAcquireSwapchainImage(eye.handle, &acquire, &index),
               "xrAcquireSwapchainImage"))
        return false;

    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (!xr_ok(xrWaitSwapchainImage(eye.handle, &wait),
               "xrWaitSwapchainImage"))
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, eye.images.at(index).image, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("eye framebuffer incomplete");
        return false;
    }
    glViewport(0, 0, eye.width, eye.height);
    glClearColor(color[0], color[1], color[2], color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();

    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return xr_ok(xrReleaseSwapchainImage(eye.handle, &release),
                 "xrReleaseSwapchainImage");
}

void xr_main(ANativeActivity *activity)
{
    LOGI("Quest XR shell: pointer_width=%zu api=%d",
         sizeof(void *) * 8, android_get_device_api_level());

    EglState egl{};
    XrSession session = XR_NULL_HANDLE;
    XrSpace local_space = XR_NULL_HANDLE;
    std::array<EyeSwapchain, 2> eyes{};
    GLuint framebuffer = 0;

    if (!initialize_loader(activity))
        goto cleanup;

    uint32_t extension_count = 0;
    if (!xr_ok(xrEnumerateInstanceExtensionProperties(
                   nullptr, 0, &extension_count, nullptr),
               "xrEnumerateInstanceExtensionProperties(count)"))
        goto cleanup;

    {
        std::vector<XrExtensionProperties> extensions(
            extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (!xr_ok(xrEnumerateInstanceExtensionProperties(
                       nullptr, extension_count, &extension_count,
                       extensions.data()),
                   "xrEnumerateInstanceExtensionProperties(list)"))
            goto cleanup;

        if (!require_extension(
                extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
            !require_extension(
                extensions, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
            LOGE("required Android/OpenGL ES OpenXR extensions missing");
            goto cleanup;
        }
    }

    {
        const char *enabled_extensions[] = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
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
        create_info.enabledExtensionCount = 2;
        create_info.enabledExtensionNames = enabled_extensions;

        if (!xr_ok(xrCreateInstance(&create_info, &g_instance),
                   "xrCreateInstance"))
            goto cleanup;
    }

    {
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(xrGetInstanceProperties(g_instance, &properties))) {
            LOGI("runtime=%s version=%u.%u.%u",
                 properties.runtimeName,
                 XR_VERSION_MAJOR(properties.runtimeVersion),
                 XR_VERSION_MINOR(properties.runtimeVersion),
                 XR_VERSION_PATCH(properties.runtimeVersion));
        }
    }

    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    {
        XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
        system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (!xr_ok(xrGetSystem(g_instance, &system_info, &system_id),
                   "xrGetSystem(HMD)"))
            goto cleanup;
    }

    if (!create_egl(&egl))
        goto cleanup;

    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR get_requirements = nullptr;
        if (!xr_ok(xrGetInstanceProcAddr(
                       g_instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                       reinterpret_cast<PFN_xrVoidFunction *>(
                           &get_requirements)),
                   "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)") ||
            !get_requirements)
            goto cleanup;

        XrGraphicsRequirementsOpenGLESKHR requirements{
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        if (!xr_ok(get_requirements(g_instance, system_id, &requirements),
                   "xrGetOpenGLESGraphicsRequirementsKHR"))
            goto cleanup;

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        const XrVersion version = XR_MAKE_VERSION(major, minor, 0);
        if (version < requirements.minApiVersionSupported ||
            version > requirements.maxApiVersionSupported) {
            LOGE("GLES %d.%d outside runtime range %u.%u..%u.%u",
                 major, minor,
                 XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                 XR_VERSION_MINOR(requirements.minApiVersionSupported),
                 XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                 XR_VERSION_MINOR(requirements.maxApiVersionSupported));
            goto cleanup;
        }
    }

    {
        XrGraphicsBindingOpenGLESAndroidKHR binding{
            XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        binding.display = egl.display;
        binding.config = egl.config;
        binding.context = egl.context;

        XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
        session_info.next = &binding;
        session_info.systemId = system_id;
        if (!xr_ok(xrCreateSession(g_instance, &session_info, &session),
                   "xrCreateSession"))
            goto cleanup;
    }

    {
        XrReferenceSpaceCreateInfo space_info{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        space_info.poseInReferenceSpace.orientation.w = 1.0f;
        if (!xr_ok(xrCreateReferenceSpace(
                       session, &space_info, &local_space),
                   "xrCreateReferenceSpace"))
            goto cleanup;
    }

    uint32_t view_count = 0;
    std::array<XrViewConfigurationView, 2> view_configs{{
        {XR_TYPE_VIEW_CONFIGURATION_VIEW},
        {XR_TYPE_VIEW_CONFIGURATION_VIEW},
    }};
    {
        if (!xr_ok(xrEnumerateViewConfigurationViews(
                       g_instance, system_id,
                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                       0, &view_count, nullptr),
                   "xrEnumerateViewConfigurationViews(count)") ||
            view_count != 2) {
            LOGE("expected two primary stereo views, got %u", view_count);
            goto cleanup;
        }
        if (!xr_ok(xrEnumerateViewConfigurationViews(
                       g_instance, system_id,
                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                       2, &view_count, view_configs.data()),
                   "xrEnumerateViewConfigurationViews(list)"))
            goto cleanup;
    }

    {
        uint32_t format_count = 0;
        if (!xr_ok(xrEnumerateSwapchainFormats(
                       session, 0, &format_count, nullptr),
                   "xrEnumerateSwapchainFormats(count)"))
            goto cleanup;
        std::vector<int64_t> formats(format_count);
        if (!xr_ok(xrEnumerateSwapchainFormats(
                       session, format_count, &format_count, formats.data()),
                   "xrEnumerateSwapchainFormats(list)"))
            goto cleanup;
        const int64_t format = choose_color_format(formats);
        if (format < 0) {
            LOGE("runtime exposed no color swapchain format");
            goto cleanup;
        }
        LOGI("swapchain format=0x%llx",
             static_cast<unsigned long long>(format));
        for (uint32_t eye = 0; eye < 2; ++eye) {
            if (!create_swapchain(
                    session, view_configs[eye], format, &eyes[eye]))
                goto cleanup;
            LOGI("eye[%u] swapchain=%dx%d images=%zu", eye,
                 eyes[eye].width, eyes[eye].height,
                 eyes[eye].images.size());
        }
    }

    glGenFramebuffers(1, &framebuffer);

    bool session_running = false;
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    uint64_t submitted_frames = 0;

    while (!g_stop.load(std::memory_order_relaxed)) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto *changed =
                    reinterpret_cast<const XrEventDataSessionStateChanged *>(
                        &event);
                session_state = changed->state;
                LOGI("session state=%d", static_cast<int>(session_state));

                if (session_state == XR_SESSION_STATE_READY &&
                    !session_running) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (!xr_ok(xrBeginSession(session, &begin),
                               "xrBeginSession"))
                        g_stop = true;
                    else
                        session_running = true;
                } else if (session_state == XR_SESSION_STATE_STOPPING &&
                           session_running) {
                    xrEndSession(session);
                    session_running = false;
                } else if (session_state == XR_SESSION_STATE_EXITING ||
                           session_state == XR_SESSION_STATE_LOSS_PENDING) {
                    g_stop = true;
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                g_stop = true;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }

        if (!session_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame_state.predictedDisplayTime;
        locate.space = local_space;
        uint32_t located_count = 0;
        const XrResult located = xrLocateViews(
            session, &locate, &view_state, 2, &located_count, views.data());

        std::array<XrCompositionLayerProjectionView, 2> projections{{
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        }};
        XrCompositionLayerProjection layer{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = local_space;

        bool render =
            frame_state.shouldRender == XR_TRUE &&
            XR_SUCCEEDED(located) &&
            located_count == 2 &&
            (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
            (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);

        if (render) {
            const float colors[2][4] = {
                {0.30f, 0.04f, 0.04f, 1.0f},
                {0.04f, 0.08f, 0.30f, 1.0f},
            };
            for (uint32_t eye = 0; eye < 2; ++eye) {
                if (!clear_eye(eyes[eye], framebuffer, colors[eye])) {
                    render = false;
                    break;
                }
                projections[eye].pose = views[eye].pose;
                projections[eye].fov = views[eye].fov;
                projections[eye].subImage.swapchain = eyes[eye].handle;
                projections[eye].subImage.imageRect.offset = {0, 0};
                projections[eye].subImage.imageRect.extent = {
                    eyes[eye].width, eyes[eye].height};
                projections[eye].subImage.imageArrayIndex = 0;
            }
        }

        const XrCompositionLayerBaseHeader *layers[1] = {};
        if (render) {
            layer.viewCount = 2;
            layer.views = projections.data();
            layers[0] =
                reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
        }

        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame_state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = render ? 1u : 0u;
        end.layers = render ? layers : nullptr;
        if (!xr_ok(xrEndFrame(session, &end), "xrEndFrame"))
            break;

        if (render && (++submitted_frames % 300) == 0)
            LOGI("submitted frames=%llu",
                 static_cast<unsigned long long>(submitted_frames));
    }

cleanup:
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    for (auto &eye : eyes)
        destroy_swapchain(&eye);
    if (local_space != XR_NULL_HANDLE)
        xrDestroySpace(local_space);
    if (session != XR_NULL_HANDLE)
        xrDestroySession(session);
    if (g_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_instance);
        g_instance = XR_NULL_HANDLE;
    }
    destroy_egl(&egl);
    LOGI("Quest XR thread stopped");
}

void on_destroy(ANativeActivity *)
{
    g_stop = true;
    if (g_xr_thread.joinable())
        g_xr_thread.join();
    LOGI("activity destroyed");
}

void on_pause(ANativeActivity *) { LOGI("activity paused"); }
void on_resume(ANativeActivity *) { LOGI("activity resumed"); }

} // namespace

extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity *activity, void *, size_t)
{
    LOGI("ANativeActivity_onCreate");
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onResume = on_resume;

    g_stop = false;
    g_xr_thread = std::thread(xr_main, activity);
}
