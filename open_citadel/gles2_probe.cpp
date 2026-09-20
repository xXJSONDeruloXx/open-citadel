/*
 * GLES2 observation and compatibility layer for Epic Citadel.
 *
 * Keep this separate from Katamari's symtab_glprobe.cpp: that probe forwards
 * through a GLES1 table, while UE3 is a shader-era GLES2 renderer. These
 * wrappers sit before symtable_gles2 in the resolver and forward through the
 * already-populated GLAD GLES2 pointers.
 */
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "atc_decompress.h"
#include "khronos/glad.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "trace.h"

#ifndef GL_ATC_RGB_AMD
#define GL_ATC_RGB_AMD 0x8C92
#endif
#ifndef GL_ATC_RGBA_EXPLICIT_ALPHA_AMD
#define GL_ATC_RGBA_EXPLICIT_ALPHA_AMD 0x8C93
#endif
#ifndef GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD
#define GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD 0x87EE
#endif

static std::atomic<int> g_shaders_ok{0};
static std::atomic<int> g_shaders_failed{0};
static std::atomic<int> g_programs_ok{0};
static std::atomic<int> g_programs_failed{0};
static std::atomic<long> g_draws{0};
static std::atomic<long> g_textures{0};
static std::atomic<long> g_atc_decoded{0};
static std::atomic<long> g_compressed_passthrough{0};


extern "C" long android_egl_frames(void);

struct VrTraceConfig {
    bool enabled = false;
    bool matrices = false;
    bool framebuffers = false;
    long max_events = 5000;
};

static bool env_flag(const char *name)
{
    const char *value = std::getenv(name);
    return value && *value && *value != '0';
}

static long env_positive_long(const char *name, long fallback)
{
    const char *value = std::getenv(name);
    if (!value || !*value)
        return fallback;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (!end || *end || parsed <= 0)
        return fallback;
    return std::min<long>(parsed, 1000000);
}

static const VrTraceConfig &vr_trace_config()
{
    static const VrTraceConfig config = [] {
        VrTraceConfig value{};
        value.enabled = env_flag("OPEN_CITADEL_VR_TRACE");
        value.matrices = env_flag("OPEN_CITADEL_VR_TRACE_MATRICES");
        value.framebuffers = env_flag("OPEN_CITADEL_VR_TRACE_FRAMEBUFFERS");
        value.enabled = value.enabled || value.matrices || value.framebuffers;
        value.max_events = env_positive_long(
            "OPEN_CITADEL_VR_TRACE_MAX_EVENTS", value.max_events);
        return value;
    }();
    return config;
}

static std::atomic<long> g_vr_trace_events{0};
static GLuint g_vr_current_program = 0;
static GLuint g_vr_current_framebuffer = 0;
static GLint g_vr_viewport[4] = {0, 0, 0, 0};

static void vr_trace(const char *kind, const char *format, ...)
{
    const VrTraceConfig &config = vr_trace_config();
    if (!config.enabled)
        return;

    const long event = g_vr_trace_events.fetch_add(1, std::memory_order_relaxed);
    if (event >= config.max_events)
        return;

    const long frame = android_egl_frames() + 1;
    std::fprintf(stderr, "VRTRACE frame=%ld event=%ld kind=%s ",
                 frame, event + 1, kind);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

static void vr_trace_active_uniforms(GLuint program)
{
    const VrTraceConfig &config = vr_trace_config();
    if (!config.matrices)
        return;

    GLint count = 0;
    GLint max_length = 0;
    glad_glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glad_glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_length);
    if (count <= 0 || max_length <= 1)
        return;

    std::vector<GLchar> name(static_cast<std::size_t>(max_length) + 1);
    for (GLint index = 0; index < count; ++index) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glad_glGetActiveUniform(program, static_cast<GLuint>(index),
                                max_length, &length, &size, &type, name.data());
        name[std::max<GLsizei>(0, length)] = 0;
        const GLint location = glad_glGetUniformLocation(program, name.data());
        vr_trace("uniform",
                 "program=%u index=%d location=%d type=0x%04x size=%d name=%s",
                 program, index, location, type, size, name.data());
    }
}

#if defined(_WIN32)
static std::string windows_guest_extensions(const char *host_extensions)
{
    std::string guest_extensions;
    bool has_atc = false;
    const std::string host(host_extensions ? host_extensions : "");
    std::size_t position = 0;
    while (position < host.size()) {
        const std::size_t end = host.find(' ', position);
        const std::string extension = host.substr(
            position, end == std::string::npos ? std::string::npos :
                                                   end - position);
        std::string lower = extension;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char value) { return (char)std::tolower(value); });
        const bool dxt = lower.find("s3tc") != std::string::npos ||
                         lower.find("dxt") != std::string::npos;
        has_atc = has_atc ||
            lower.find("compressed_atc") != std::string::npos ||
            lower.find("texture_compression_atitc") != std::string::npos;
        if (!dxt) {
            if (!guest_extensions.empty())
                guest_extensions.push_back(' ');
            guest_extensions += extension;
        }
        if (end == std::string::npos)
            break;
        position = end + 1;
    }

    /* UE3 selects an Android texture-cache suffix from this list. The game
     * ships ATITC TFCs, not the DXT cache chosen by a desktop PC driver. Our
     * compressed-upload thunk decodes ATC to RGBA, so expose that software
     * capability while leaving the real host context untouched. */
    if (!has_atc) {
        if (!guest_extensions.empty())
            guest_extensions.push_back(' ');
        guest_extensions += "GL_AMD_compressed_ATC_texture";
    }
    return guest_extensions;
}

extern "C" const GLubyte *open_citadel_glGetString(GLenum name)
{
    const GLubyte *host = glad_glGetString(name);
    if (name != GL_EXTENSIONS || !host)
        return host;

    static const std::string guest = windows_guest_extensions(
        reinterpret_cast<const char *>(host));
    return reinterpret_cast<const GLubyte *>(guest.c_str());
}
#endif

static void report_shader_log(GLuint object, bool shader)
{
    GLint length = 0;
    if (shader)
        glad_glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    else
        glad_glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1)
        return;

    std::vector<GLchar> log(static_cast<std::size_t>(length) + 1);
    if (shader)
        glad_glGetShaderInfoLog(object, length, nullptr, log.data());
    else
        glad_glGetProgramInfoLog(object, length, nullptr, log.data());
    trace("OpenCitadel GL: %s log: %s", shader ? "shader" : "program",
          log.data());
}

extern "C" void open_citadel_glCompileShader(GLuint shader)
{
    glad_glCompileShader(shader);
    GLint ok = 0;
    glad_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) {
        ++g_shaders_ok;
    } else {
        ++g_shaders_failed;
        report_shader_log(shader, true);
    }
}

extern "C" void open_citadel_glLinkProgram(GLuint program)
{
    glad_glLinkProgram(program);
    GLint ok = 0;
    glad_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) {
        ++g_programs_ok;
        vr_trace("link", "program=%u ok=1", program);
        vr_trace_active_uniforms(program);
    } else {
        ++g_programs_failed;
        report_shader_log(program, false);
    }
}

extern "C" GLint open_citadel_glGetUniformLocation(
    GLuint program, const GLchar *name)
{
    const GLint location = glad_glGetUniformLocation(program, name);
    if (vr_trace_config().matrices)
        vr_trace("uniform-location", "program=%u location=%d name=%s",
                 program, location, name ? name : "<null>");
    return location;
}

extern "C" void open_citadel_glUseProgram(GLuint program)
{
    g_vr_current_program = program;
    if (vr_trace_config().enabled)
        vr_trace("program", "program=%u", program);
    glad_glUseProgram(program);
}

extern "C" void open_citadel_glBindFramebuffer(GLenum target,
                                                GLuint framebuffer)
{
    if (target == GL_FRAMEBUFFER)
        g_vr_current_framebuffer = framebuffer;
    if (vr_trace_config().framebuffers)
        vr_trace("fbo-bind", "target=0x%04x framebuffer=%u",
                 target, framebuffer);
    glad_glBindFramebuffer(target, framebuffer);
}

extern "C" void open_citadel_glFramebufferTexture2D(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
    GLint level)
{
    if (vr_trace_config().framebuffers)
        vr_trace("fbo-texture",
                 "fbo=%u target=0x%04x attachment=0x%04x textarget=0x%04x "
                 "texture=%u level=%d",
                 g_vr_current_framebuffer, target, attachment, textarget,
                 texture, level);
    glad_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

extern "C" void open_citadel_glViewport(
    GLint x, GLint y, GLsizei width, GLsizei height)
{
    g_vr_viewport[0] = x;
    g_vr_viewport[1] = y;
    g_vr_viewport[2] = width;
    g_vr_viewport[3] = height;
    if (vr_trace_config().framebuffers)
        vr_trace("viewport", "fbo=%u x=%d y=%d width=%d height=%d",
                 g_vr_current_framebuffer, x, y, width, height);
    glad_glViewport(x, y, width, height);
}

extern "C" void open_citadel_glScissor(
    GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (vr_trace_config().framebuffers)
        vr_trace("scissor", "fbo=%u x=%d y=%d width=%d height=%d",
                 g_vr_current_framebuffer, x, y, width, height);
    glad_glScissor(x, y, width, height);
}

extern "C" void open_citadel_glClear(GLbitfield mask)
{
    if (vr_trace_config().framebuffers)
        vr_trace("clear", "fbo=%u mask=0x%08x", g_vr_current_framebuffer, mask);
    glad_glClear(mask);
}

extern "C" void open_citadel_glUniformMatrix4fv(
    GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (vr_trace_config().matrices && value && count > 0) {
        vr_trace(
            "mat4",
            "program=%u fbo=%u location=%d count=%d transpose=%u "
            "viewport=%d,%d,%d,%d "
            "m=[%.7g %.7g %.7g %.7g | %.7g %.7g %.7g %.7g | "
            "%.7g %.7g %.7g %.7g | %.7g %.7g %.7g %.7g]",
            g_vr_current_program, g_vr_current_framebuffer, location, count,
            static_cast<unsigned>(transpose),
            g_vr_viewport[0], g_vr_viewport[1],
            g_vr_viewport[2], g_vr_viewport[3],
            value[0], value[1], value[2], value[3],
            value[4], value[5], value[6], value[7],
            value[8], value[9], value[10], value[11],
            value[12], value[13], value[14], value[15]);
    }
    glad_glUniformMatrix4fv(location, count, transpose, value);
}

extern "C" void open_citadel_glDrawArrays(GLenum mode, GLint first,
                                            GLsizei count)
{
    const long draw = ++g_draws;
    if (vr_trace_config().enabled)
        vr_trace("draw-arrays",
                 "draw=%ld program=%u fbo=%u mode=0x%04x first=%d count=%d "
                 "viewport=%d,%d,%d,%d",
                 draw, g_vr_current_program, g_vr_current_framebuffer,
                 mode, first, count,
                 g_vr_viewport[0], g_vr_viewport[1],
                 g_vr_viewport[2], g_vr_viewport[3]);
    glad_glDrawArrays(mode, first, count);
}

extern "C" void open_citadel_glDrawElements(GLenum mode, GLsizei count,
                                              GLenum type,
                                              const void *indices)
{
    const long draw = ++g_draws;
    if (vr_trace_config().enabled)
        vr_trace("draw-elements",
                 "draw=%ld program=%u fbo=%u mode=0x%04x count=%d type=0x%04x "
                 "viewport=%d,%d,%d,%d",
                 draw, g_vr_current_program, g_vr_current_framebuffer,
                 mode, count, type,
                 g_vr_viewport[0], g_vr_viewport[1],
                 g_vr_viewport[2], g_vr_viewport[3]);
    glad_glDrawElements(mode, count, type, indices);
}

extern "C" void open_citadel_glTexImage2D(
    GLenum target, GLint level, GLint internalformat, GLsizei width,
    GLsizei height, GLint border, GLenum format, GLenum type,
    const void *pixels)
{
    ++g_textures;
    glad_glTexImage2D(target, level, internalformat, width, height, border,
                      format, type, pixels);
}

enum class AtcKind { none, rgb, explicit_alpha, interpolated_alpha };

static AtcKind atc_kind(GLenum format)
{
    switch (format) {
    case GL_ATC_RGB_AMD: return AtcKind::rgb;
    case GL_ATC_RGBA_EXPLICIT_ALPHA_AMD: return AtcKind::explicit_alpha;
    case GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD:
        return AtcKind::interpolated_alpha;
    default: return AtcKind::none;
    }
}

static bool decode_atc(AtcKind kind, GLsizei width, GLsizei height,
                       GLsizei image_size, const void *data,
                       std::vector<std::uint8_t> &rgba)
{
    if (kind == AtcKind::none || width <= 0 || height <= 0 ||
        image_size < 0 || !data)
        return false;

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / 4)
        return false;

    try {
        rgba.resize(w * h * 4);
    } catch (const std::bad_alloc &) {
        return false;
    }

    bool ok = false;
    switch (kind) {
    case AtcKind::rgb:
        ok = atc::decode_rgb(data, static_cast<std::size_t>(image_size),
                             width, height, rgba.data());
        break;
    case AtcKind::explicit_alpha:
        ok = atc::decode_rgba_explicit(
            data, static_cast<std::size_t>(image_size),
            width, height, rgba.data());
        break;
    case AtcKind::interpolated_alpha:
        ok = atc::decode_rgba_interpolated(
            data, static_cast<std::size_t>(image_size),
            width, height, rgba.data());
        break;
    default:
        break;
    }
    if (!ok)
        rgba.clear();
    return ok;
}

extern "C" void open_citadel_glCompressedTexImage2D(
    GLenum target, GLint level, GLenum internalformat, GLsizei width,
    GLsizei height, GLint border, GLsizei image_size, const void *data)
{
    ++g_textures;
    const AtcKind kind = atc_kind(internalformat);
    if (kind != AtcKind::none) {
        std::vector<std::uint8_t> rgba;
        if (decode_atc(kind, width, height, image_size, data, rgba)) {
            glad_glTexImage2D(target, level, GL_RGBA, width, height, border,
                              GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            const long count = ++g_atc_decoded;
            if (count <= 16 || (count % 128) == 0)
                trace("OpenCitadel ATC: decoded upload #%ld level=%d "
                      "format=0x%04x %dx%d (%d -> %zu bytes)",
                      count, level, internalformat, width, height, image_size,
                      rgba.size());
            return;
        }
        trace("OpenCitadel ATC: decode failed; passing format 0x%04x "
              "%dx%d bytes=%d to driver",
              internalformat, width, height, image_size);
    }

    ++g_compressed_passthrough;
    glad_glCompressedTexImage2D(target, level, internalformat, width, height,
                                border, image_size, data);
}

extern "C" void open_citadel_glCompressedTexSubImage2D(
    GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
    GLsizei height, GLenum format, GLsizei image_size, const void *data)
{
    /* The AMD ATC extension explicitly disallows compressed sub-image updates.
     * Forward unchanged so the driver's GL error remains observable if UE3
     * unexpectedly attempts one. */
    glad_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width,
                                   height, format, image_size, data);
}

extern "C" long open_citadel_gl_draws(void) { return g_draws.load(); }
extern "C" long open_citadel_gl_textures(void) { return g_textures.load(); }
extern "C" long open_citadel_gl_atc_decoded(void) { return g_atc_decoded.load(); }
extern "C" int open_citadel_gl_shaders_ok(void) { return g_shaders_ok.load(); }
extern "C" int open_citadel_gl_shaders_failed(void) { return g_shaders_failed.load(); }
extern "C" int open_citadel_gl_programs_ok(void) { return g_programs_ok.load(); }
extern "C" int open_citadel_gl_programs_failed(void) { return g_programs_failed.load(); }

DynLibFunction symtable_open_citadel_gles2_probe[] = {
#if defined(_WIN32)
    THUNK_SPECIFIC("glGetString", open_citadel_glGetString),
#endif
    THUNK_SPECIFIC("glCompileShader", open_citadel_glCompileShader),
    THUNK_SPECIFIC("glLinkProgram", open_citadel_glLinkProgram),
    THUNK_SPECIFIC("glGetUniformLocation", open_citadel_glGetUniformLocation),
    THUNK_SPECIFIC("glUseProgram", open_citadel_glUseProgram),
    THUNK_SPECIFIC("glBindFramebuffer", open_citadel_glBindFramebuffer),
    THUNK_SPECIFIC("glFramebufferTexture2D",
                   open_citadel_glFramebufferTexture2D),
    THUNK_SPECIFIC("glViewport", open_citadel_glViewport),
    THUNK_SPECIFIC("glScissor", open_citadel_glScissor),
    THUNK_SPECIFIC("glClear", open_citadel_glClear),
    THUNK_SPECIFIC("glUniformMatrix4fv",
                   open_citadel_glUniformMatrix4fv),
    THUNK_SPECIFIC("glDrawArrays", open_citadel_glDrawArrays),
    THUNK_SPECIFIC("glDrawElements", open_citadel_glDrawElements),
    THUNK_SPECIFIC("glTexImage2D", open_citadel_glTexImage2D),
    THUNK_SPECIFIC("glCompressedTexImage2D",
                   open_citadel_glCompressedTexImage2D),
    THUNK_SPECIFIC("glCompressedTexSubImage2D",
                   open_citadel_glCompressedTexSubImage2D),
    { nullptr, 0 },
};
