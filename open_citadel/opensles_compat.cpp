#include "opensles_compat.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "citadel_audio.h"
#include "opensles_abi.h"
#include "so_util.h"
#include "trace.h"
#include "thunk_gen.h"

#if defined(_MSC_VER)
#define OPENSL_CALL __cdecl
#else
#define OPENSL_CALL
#endif

namespace {

const SLInterfaceID_ kEngineId = {
    0x4c6f4f43, 0x6e45, 0x4947, 0x8001, {0x4f, 0x70, 0x65, 0x6e, 0x43, 0x69}};
const SLInterfaceID_ kPlayId = {
    0x4c6f4f43, 0x6e50, 0x4c41, 0x8002, {0x4f, 0x70, 0x65, 0x6e, 0x43, 0x69}};
const SLInterfaceID_ kVolumeId = {
    0x4c6f4f43, 0x6e56, 0x4f4c, 0x8003, {0x4f, 0x70, 0x65, 0x6e, 0x43, 0x69}};
const SLInterfaceID_ kBufferQueueId = {
    0x4c6f4f43, 0x6e42, 0x51, 0x8004, {0x4f, 0x70, 0x65, 0x6e, 0x43, 0x69}};

} // namespace

extern "C" {
extern const SLInterfaceID SL_IID_ENGINE = &kEngineId;
extern const SLInterfaceID SL_IID_PLAY = &kPlayId;
extern const SLInterfaceID SL_IID_VOLUME = &kVolumeId;
extern const SLInterfaceID SL_IID_BUFFERQUEUE = &kBufferQueueId;
}

namespace open_citadel::opensles {
namespace {

enum class ObjectKind {
    Engine,
    OutputMix,
    AudioPlayer,
};

struct Object;

template <typename VTable>
struct InterfaceRef {
    const VTable *vtable = nullptr;
    Object *owner = nullptr;
};

struct RequestedPlayerInterfaces {
    bool play = false;
    bool volume = false;
    bool buffer_queue = false;
};

extern const SLObjectItf_ g_object_vtable;
extern const SLEngineItf_ g_engine_vtable;
extern const SLPlayItf_ g_play_vtable;
extern const SLVolumeItf_ g_volume_vtable;
extern const SLBufferQueueItf_ g_buffer_queue_vtable;

SLresult object_realize(SLObjectItf self, SLboolean async);
SLresult object_resume(SLObjectItf self, SLboolean async);
SLresult object_get_state(SLObjectItf self, SLuint32 *state);
SLresult object_get_interface(SLObjectItf self, SLInterfaceID iid,
                              void *interface_pointer);
SLresult object_register_callback(SLObjectItf self,
                                  slObjectCallback callback, void *context);
void object_abort_async(SLObjectItf self);
void object_destroy(SLObjectItf self);
SLresult object_set_priority(SLObjectItf self, SLint32 priority,
                             SLboolean preemptable);
SLresult object_get_priority(SLObjectItf self, SLint32 *priority,
                             SLboolean *preemptable);
SLresult object_set_loss_of_control(SLObjectItf self, SLint16 count,
                                    SLInterfaceID *interface_ids,
                                    SLboolean enabled);

SLresult engine_create_led(SLEngineItf self, SLObjectItf *device,
                           SLuint32 device_id, SLuint32 count,
                           const SLInterfaceID *interface_ids,
                           const SLboolean *required);
SLresult engine_create_vibra(SLEngineItf self, SLObjectItf *device,
                             SLuint32 device_id, SLuint32 count,
                             const SLInterfaceID *interface_ids,
                             const SLboolean *required);
SLresult engine_create_player(SLEngineItf self, SLObjectItf *player,
                              SLDataSource *source, SLDataSink *sink,
                              SLuint32 count,
                              const SLInterfaceID *interface_ids,
                              const SLboolean *required);
SLresult engine_create_recorder(SLEngineItf self, SLObjectItf *recorder,
                                SLDataSource *source, SLDataSink *sink,
                                SLuint32 count,
                                const SLInterfaceID *interface_ids,
                                const SLboolean *required);
SLresult engine_create_midi_player(
    SLEngineItf self, SLObjectItf *player, SLDataSource *midi_source,
    SLDataSource *bank_source, SLDataSink *audio_output, SLDataSink *vibra,
    SLDataSink *led_array, SLuint32 count,
    const SLInterfaceID *interface_ids, const SLboolean *required);
SLresult engine_create_listener(SLEngineItf self, SLObjectItf *listener,
                                SLuint32 count,
                                const SLInterfaceID *interface_ids,
                                const SLboolean *required);
SLresult engine_create_3d_group(SLEngineItf self, SLObjectItf *group,
                                SLuint32 count,
                                const SLInterfaceID *interface_ids,
                                const SLboolean *required);
SLresult engine_create_output_mix(SLEngineItf self, SLObjectItf *mix,
                                  SLuint32 count,
                                  const SLInterfaceID *interface_ids,
                                  const SLboolean *required);
SLresult engine_create_metadata_extractor(
    SLEngineItf self, SLObjectItf *extractor, SLDataSource *source,
    SLuint32 count, const SLInterfaceID *interface_ids,
    const SLboolean *required);
SLresult engine_create_extension_object(
    SLEngineItf self, SLObjectItf *object, void *parameters,
    SLuint32 object_id, SLuint32 count,
    const SLInterfaceID *interface_ids, const SLboolean *required);
SLresult engine_query_num_interfaces(SLEngineItf self, SLuint32 object_id,
                                     SLuint32 *count);
SLresult engine_query_interfaces(SLEngineItf self, SLuint32 object_id,
                                 SLuint32 index, SLInterfaceID *iid);
SLresult engine_query_num_extensions(SLEngineItf self, SLuint32 *count);
SLresult engine_query_extension(SLEngineItf self, SLuint32 index,
                                uint8_t *name, SLint16 *name_length);
SLresult engine_is_extension_supported(SLEngineItf self, const uint8_t *name,
                                       SLboolean *supported);

SLresult play_set_state(SLPlayItf self, SLuint32 state);
SLresult play_get_state(SLPlayItf self, SLuint32 *state);
SLresult play_get_duration(SLPlayItf self, SLmillisecond *milliseconds);
SLresult play_get_position(SLPlayItf self, SLmillisecond *milliseconds);
SLresult play_register_callback(SLPlayItf self, slPlayCallback callback,
                                void *context);
SLresult play_set_callback_events(SLPlayItf self, SLuint32 event_flags);
SLresult play_get_callback_events(SLPlayItf self, SLuint32 *event_flags);
SLresult play_set_marker(SLPlayItf self, SLmillisecond milliseconds);
SLresult play_clear_marker(SLPlayItf self);
SLresult play_get_marker(SLPlayItf self, SLmillisecond *milliseconds);
SLresult play_set_update_period(SLPlayItf self, SLmillisecond milliseconds);
SLresult play_get_update_period(SLPlayItf self, SLmillisecond *milliseconds);

SLresult volume_set_level(SLVolumeItf self, SLmillibel level);
SLresult volume_get_level(SLVolumeItf self, SLmillibel *level);
SLresult volume_get_max_level(SLVolumeItf self, SLmillibel *level);
SLresult volume_set_mute(SLVolumeItf self, SLboolean mute);
SLresult volume_get_mute(SLVolumeItf self, SLboolean *mute);
SLresult volume_enable_stereo(SLVolumeItf self, SLboolean enable);
SLresult volume_get_stereo_enabled(SLVolumeItf self, SLboolean *enabled);
SLresult volume_set_stereo_position(SLVolumeItf self, SLpermille position);
SLresult volume_get_stereo_position(SLVolumeItf self,
                                    SLpermille *position);

SLresult buffer_queue_enqueue(SLBufferQueueItf self, const void *buffer,
                              SLuint32 size);
SLresult buffer_queue_clear(SLBufferQueueItf self);
SLresult buffer_queue_get_state(SLBufferQueueItf self,
                                SLBufferQueueState *state);
SLresult buffer_queue_register_callback(SLBufferQueueItf self,
                                        slBufferQueueCallback callback,
                                        void *context);

const SLObjectItf_ g_object_vtable = {
    object_realize, object_resume, object_get_state, object_get_interface,
    object_register_callback, object_abort_async, object_destroy,
    object_set_priority, object_get_priority, object_set_loss_of_control,
};

const SLEngineItf_ g_engine_vtable = {
    engine_create_led, engine_create_vibra, engine_create_player,
    engine_create_recorder, engine_create_midi_player,
    engine_create_listener, engine_create_3d_group,
    engine_create_output_mix, engine_create_metadata_extractor,
    engine_create_extension_object, engine_query_num_interfaces,
    engine_query_interfaces, engine_query_num_extensions,
    engine_query_extension, engine_is_extension_supported,
};

const SLPlayItf_ g_play_vtable = {
    play_set_state, play_get_state, play_get_duration, play_get_position,
    play_register_callback, play_set_callback_events,
    play_get_callback_events, play_set_marker, play_clear_marker,
    play_get_marker, play_set_update_period, play_get_update_period,
};

const SLVolumeItf_ g_volume_vtable = {
    volume_set_level, volume_get_level, volume_get_max_level,
    volume_set_mute, volume_get_mute, volume_enable_stereo,
    volume_get_stereo_enabled, volume_set_stereo_position,
    volume_get_stereo_position,
};

const SLBufferQueueItf_ g_buffer_queue_vtable = {
    buffer_queue_enqueue, buffer_queue_clear, buffer_queue_get_state,
    buffer_queue_register_callback,
};

struct Object {
    explicit Object(ObjectKind object_kind);

    ObjectKind kind;
    InterfaceRef<SLObjectItf_> object_interface;
    InterfaceRef<SLEngineItf_> engine_interface;
    InterfaceRef<SLPlayItf_> play_interface;
    InterfaceRef<SLVolumeItf_> volume_interface;
    InterfaceRef<SLBufferQueueItf_> buffer_queue_interface;
    std::mutex mutex;
    std::mutex callback_mutex;
    std::atomic<bool> destroying{false};
    bool realized = false;
    bool preemptable = true;
    SLint32 priority = 0;
    RequestedPlayerInterfaces requested_interfaces;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> audio_queue;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t bits_per_sample = 0;
    size_t max_buffers = 0;
    SLuint32 play_state = SL_PLAYSTATE_STOPPED;
    SLmillibel volume_level = 0;
    bool muted = false;
    bool stereo_position_enabled = false;
    SLpermille stereo_position = 0;
    slPlayCallback play_callback = nullptr;
    void *play_callback_context = nullptr;
    SLuint32 play_callback_events = 0;
    SLmillisecond marker_position = SL_TIME_UNKNOWN;
    SLmillisecond update_period = 0;
    slBufferQueueCallback buffer_queue_callback = nullptr;
    void *buffer_queue_callback_context = nullptr;
    slObjectCallback object_callback = nullptr;
    void *object_callback_context = nullptr;
};

Object::Object(ObjectKind object_kind) : kind(object_kind)
{
    object_interface = {&g_object_vtable, this};
    engine_interface = {&g_engine_vtable, this};
    play_interface = {&g_play_vtable, this};
    volume_interface = {&g_volume_vtable, this};
    buffer_queue_interface = {&g_buffer_queue_vtable, this};
}

std::mutex g_objects_mutex;
std::unordered_map<Object *, std::shared_ptr<Object>> g_objects;
std::unordered_map<void *, std::shared_ptr<Object>> g_handles;
bool g_shutting_down = false;
std::atomic<unsigned int> g_trace_events{0};

void trace_opensles(const char *format, ...)
{
    if (g_trace_events.fetch_add(1, std::memory_order_relaxed) >= 96)
        return;
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    trace("OpenCitadel OpenSLES: %s", message);
}

template <typename Handle>
void *handle_storage(Handle handle)
{
    return const_cast<void *>(reinterpret_cast<const void *>(handle));
}

template <typename Handle, typename VTable>
InterfaceRef<VTable> *interface_ref(Handle handle)
{
    return handle ? static_cast<InterfaceRef<VTable> *>(handle_storage(handle))
                  : nullptr;
}

template <typename Handle, typename VTable>
std::shared_ptr<Object> find_owner(Handle handle)
{
    if (!handle)
        return {};
    void *key = handle_storage(handle);
    std::lock_guard<std::mutex> lock(g_objects_mutex);
    const auto found = g_handles.find(key);
    return found == g_handles.end() ? std::shared_ptr<Object>{}
                                    : found->second;
}

std::shared_ptr<Object> make_object(ObjectKind kind)
{
    std::shared_ptr<Object> object;
    try {
        object = std::make_shared<Object>(kind);
    } catch (...) {
        return {};
    }
    std::lock_guard<std::mutex> lock(g_objects_mutex);
    if (g_shutting_down)
        return {};
    try {
        g_objects.emplace(object.get(), object);
        g_handles.emplace(&object->object_interface.vtable, object);
        g_handles.emplace(&object->engine_interface.vtable, object);
        g_handles.emplace(&object->play_interface.vtable, object);
        g_handles.emplace(&object->volume_interface.vtable, object);
        g_handles.emplace(&object->buffer_queue_interface.vtable, object);
    } catch (...) {
        g_objects.erase(object.get());
        g_handles.erase(&object->object_interface.vtable);
        g_handles.erase(&object->engine_interface.vtable);
        g_handles.erase(&object->play_interface.vtable);
        g_handles.erase(&object->volume_interface.vtable);
        g_handles.erase(&object->buffer_queue_interface.vtable);
        return {};
    }
    return object;
}

SLObjectItf object_handle(Object *object)
{
    return object ? reinterpret_cast<SLObjectItf>(
                        &object->object_interface.vtable)
                  : nullptr;
}

SLEngineItf engine_handle(Object *object)
{
    return object ? reinterpret_cast<SLEngineItf>(
                        &object->engine_interface.vtable)
                  : nullptr;
}

SLPlayItf play_handle(Object *object)
{
    return object ? reinterpret_cast<SLPlayItf>(&object->play_interface.vtable)
                  : nullptr;
}

SLVolumeItf volume_handle(Object *object)
{
    return object ? reinterpret_cast<SLVolumeItf>(
                        &object->volume_interface.vtable)
                  : nullptr;
}

SLBufferQueueItf buffer_queue_handle(Object *object)
{
    return object ? reinterpret_cast<SLBufferQueueItf>(
                        &object->buffer_queue_interface.vtable)
                  : nullptr;
}

bool interface_requested(const RequestedPlayerInterfaces &requested,
                         SLInterfaceID iid)
{
    if (iid == SL_IID_PLAY)
        return requested.play;
    if (iid == SL_IID_VOLUME)
        return requested.volume;
    if (iid == SL_IID_BUFFERQUEUE)
        return requested.buffer_queue;
    return false;
}

float effective_gain(const Object &object)
{
    if (object.muted || object.volume_level <= SL_MILLIBEL_MIN)
        return 0.0f;
    return std::clamp(std::pow(10.0f,
                               static_cast<float>(object.volume_level) /
                                   2000.0f),
                      0.0f, 4.0f);
}

void update_audio_gain(const std::shared_ptr<Object> &object)
{
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    float gain = 0.0f;
    int pan = 0;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        queue = object->audio_queue;
        gain = effective_gain(*object);
        pan = object->stereo_position_enabled ? object->stereo_position : 0;
    }
    if (queue) {
        open_citadel::audio::set_open_sl_buffer_queue_gain(queue, gain);
        open_citadel::audio::set_open_sl_buffer_queue_pan(queue, pan);
    }
}

void opensles_buffer_completed(void *context)
{
    auto *object = static_cast<Object *>(context);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return;
    slBufferQueueCallback callback = nullptr;
    void *callback_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(object->callback_mutex);
        callback = object->buffer_queue_callback;
        callback_context = object->buffer_queue_callback_context;
    }
    if (callback)
        callback(buffer_queue_handle(object), callback_context);
}

void destroy_object(const std::shared_ptr<Object> &object);

SLresult create_output_mix(SLObjectItf *mix, SLuint32 count,
                           const SLInterfaceID *interface_ids,
                           const SLboolean *required);

bool validate_interfaces(SLuint32 count, const SLInterfaceID *interface_ids,
                         const SLboolean *required,
                         RequestedPlayerInterfaces *requested)
{
    if (count && !interface_ids)
        return false;
    for (SLuint32 i = 0; i < count; ++i) {
        const SLInterfaceID iid = interface_ids[i];
        if (!iid)
            return false;
        const bool supported = iid == SL_IID_PLAY || iid == SL_IID_VOLUME ||
                               iid == SL_IID_BUFFERQUEUE ||
                               iid == SL_IID_ENGINE;
        if (!supported && required && required[i])
            return false;
        if (!requested)
            continue;
        if (iid == SL_IID_PLAY)
            requested->play = true;
        else if (iid == SL_IID_VOLUME)
            requested->volume = true;
        else if (iid == SL_IID_BUFFERQUEUE)
            requested->buffer_queue = true;
    }
    return true;
}

bool object_is_realized(const std::shared_ptr<Object> &object)
{
    if (!object || object->destroying.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(object->mutex);
    return object->realized &&
           !object->destroying.load(std::memory_order_relaxed);
}

SLresult object_realize(SLObjectItf self, SLboolean async)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    slObjectCallback callback = nullptr;
    void *context = nullptr;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->realized = true;
        callback = object->object_callback;
        context = object->object_callback_context;
    }
    if (async && callback)
        callback(object_handle(object.get()), context,
                 SL_OBJECT_EVENT_ASYNC_TERMINATION, SL_RESULT_SUCCESS, 0,
                 nullptr);
    return SL_RESULT_SUCCESS;
}

SLresult object_resume(SLObjectItf self, SLboolean async)
{
    return object_realize(self, async);
}

SLresult object_get_state(SLObjectItf self, SLuint32 *state)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || !state || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *state = object->realized ? SL_OBJECT_STATE_REALIZED
                              : SL_OBJECT_STATE_UNREALIZED;
    return SL_RESULT_SUCCESS;
}

SLresult object_get_interface(SLObjectItf self, SLInterfaceID iid,
                              void *interface_pointer)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || !iid || !interface_pointer ||
        object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PARAMETER_INVALID;

    SLresult result = SL_RESULT_FEATURE_UNSUPPORTED;
    std::lock_guard<std::mutex> lock(object->mutex);
    if (object->kind == ObjectKind::Engine && iid == SL_IID_ENGINE) {
        *static_cast<SLEngineItf *>(interface_pointer) =
            engine_handle(object.get());
        result = SL_RESULT_SUCCESS;
    } else if (object->kind == ObjectKind::AudioPlayer &&
               interface_requested(object->requested_interfaces, iid)) {
        if (iid == SL_IID_PLAY) {
            *static_cast<SLPlayItf *>(interface_pointer) =
                play_handle(object.get());
            result = SL_RESULT_SUCCESS;
        } else if (iid == SL_IID_VOLUME) {
            *static_cast<SLVolumeItf *>(interface_pointer) =
                volume_handle(object.get());
            result = SL_RESULT_SUCCESS;
        } else if (iid == SL_IID_BUFFERQUEUE) {
            *static_cast<SLBufferQueueItf *>(interface_pointer) =
                buffer_queue_handle(object.get());
            result = SL_RESULT_SUCCESS;
        }
    }
    return result;
}

SLresult object_register_callback(SLObjectItf self,
                                  slObjectCallback callback, void *context)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->object_callback = callback;
    object->object_callback_context = context;
    return SL_RESULT_SUCCESS;
}

void object_abort_async(SLObjectItf)
{
}

void destroy_object(const std::shared_ptr<Object> &object)
{
    if (!object || object->destroying.exchange(true, std::memory_order_acq_rel))
        return;

    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->realized = false;
        object->play_state = SL_PLAYSTATE_STOPPED;
        object->object_callback = nullptr;
        object->object_callback_context = nullptr;
        object->play_callback = nullptr;
        object->play_callback_context = nullptr;
        queue = std::move(object->audio_queue);
    }
    {
        std::lock_guard<std::mutex> lock(object->callback_mutex);
        object->buffer_queue_callback = nullptr;
        object->buffer_queue_callback_context = nullptr;
    }
    if (queue)
        open_citadel::audio::destroy_open_sl_buffer_queue(&queue);

    {
        std::lock_guard<std::mutex> lock(g_objects_mutex);
        g_handles.erase(&object->object_interface.vtable);
        g_handles.erase(&object->engine_interface.vtable);
        g_handles.erase(&object->play_interface.vtable);
        g_handles.erase(&object->volume_interface.vtable);
        g_handles.erase(&object->buffer_queue_interface.vtable);
        g_objects.erase(object.get());
    }
    trace_opensles("object destroyed (%u)",
                   static_cast<unsigned int>(object->kind));
}

void object_destroy(SLObjectItf self)
{
    destroy_object(find_owner<SLObjectItf, SLObjectItf_>(self));
}

SLresult object_set_priority(SLObjectItf self, SLint32 priority,
                             SLboolean preemptable)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->priority = priority;
    object->preemptable = preemptable != SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult object_get_priority(SLObjectItf self, SLint32 *priority,
                             SLboolean *preemptable)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || !priority || !preemptable ||
        object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *priority = object->priority;
    *preemptable = object->preemptable ? SL_BOOLEAN_TRUE : SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult object_set_loss_of_control(SLObjectItf self, SLint16 count,
                                    SLInterfaceID *interface_ids,
                                    SLboolean)
{
    const std::shared_ptr<Object> object = find_owner<SLObjectItf,
                                                       SLObjectItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    if (count < 0 || (count && !interface_ids))
        return SL_RESULT_PARAMETER_INVALID;
    return SL_RESULT_SUCCESS;
}

SLresult engine_create_led(SLEngineItf, SLObjectItf *device, SLuint32,
                           SLuint32, const SLInterfaceID *, const SLboolean *)
{
    if (device)
        *device = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_vibra(SLEngineItf, SLObjectItf *device, SLuint32,
                             SLuint32, const SLInterfaceID *,
                             const SLboolean *)
{
    if (device)
        *device = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_recorder(SLEngineItf, SLObjectItf *recorder,
                                SLDataSource *, SLDataSink *, SLuint32,
                                const SLInterfaceID *, const SLboolean *)
{
    if (recorder)
        *recorder = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_midi_player(
    SLEngineItf, SLObjectItf *player, SLDataSource *, SLDataSource *,
    SLDataSink *, SLDataSink *, SLDataSink *, SLuint32,
    const SLInterfaceID *, const SLboolean *)
{
    if (player)
        *player = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_listener(SLEngineItf, SLObjectItf *listener,
                                SLuint32, const SLInterfaceID *,
                                const SLboolean *)
{
    if (listener)
        *listener = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_3d_group(SLEngineItf, SLObjectItf *group, SLuint32,
                                const SLInterfaceID *, const SLboolean *)
{
    if (group)
        *group = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_output_mix(SLEngineItf self, SLObjectItf *mix,
                                  SLuint32 count,
                                  const SLInterfaceID *interface_ids,
                                  const SLboolean *required)
{
    const std::shared_ptr<Object> engine = find_owner<SLEngineItf,
                                                       SLEngineItf_>(self);
    if (!mix)
        return SL_RESULT_PARAMETER_INVALID;
    *mix = nullptr;
    if (!engine || engine->kind != ObjectKind::Engine ||
        !object_is_realized(engine))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    return create_output_mix(mix, count, interface_ids, required);
}

SLresult engine_create_metadata_extractor(
    SLEngineItf, SLObjectItf *extractor, SLDataSource *, SLuint32,
    const SLInterfaceID *, const SLboolean *)
{
    if (extractor)
        *extractor = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_create_extension_object(
    SLEngineItf, SLObjectItf *object, void *, SLuint32, SLuint32,
    const SLInterfaceID *, const SLboolean *)
{
    if (object)
        *object = nullptr;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult engine_query_num_interfaces(SLEngineItf self, SLuint32 object_id,
                                     SLuint32 *count)
{
    if (!find_owner<SLEngineItf, SLEngineItf_>(self) || !count)
        return SL_RESULT_PARAMETER_INVALID;
    *count = object_id == 0 ? 1 : 0;
    return SL_RESULT_SUCCESS;
}

SLresult engine_query_interfaces(SLEngineItf self, SLuint32 object_id,
                                 SLuint32 index, SLInterfaceID *iid)
{
    if (!find_owner<SLEngineItf, SLEngineItf_>(self) || !iid)
        return SL_RESULT_PARAMETER_INVALID;
    if (object_id != 0 || index != 0)
        return SL_RESULT_PARAMETER_INVALID;
    *iid = SL_IID_ENGINE;
    return SL_RESULT_SUCCESS;
}

SLresult engine_query_num_extensions(SLEngineItf self, SLuint32 *count)
{
    if (!find_owner<SLEngineItf, SLEngineItf_>(self) || !count)
        return SL_RESULT_PARAMETER_INVALID;
    *count = 0;
    return SL_RESULT_SUCCESS;
}

SLresult engine_query_extension(SLEngineItf self, SLuint32, uint8_t *,
                                SLint16 *)
{
    return find_owner<SLEngineItf, SLEngineItf_>(self)
        ? SL_RESULT_FEATURE_UNSUPPORTED
        : SL_RESULT_PRECONDITIONS_VIOLATED;
}

SLresult engine_is_extension_supported(SLEngineItf self, const uint8_t *,
                                       SLboolean *supported)
{
    if (!find_owner<SLEngineItf, SLEngineItf_>(self) || !supported)
        return SL_RESULT_PARAMETER_INVALID;
    *supported = SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult create_output_mix(SLObjectItf *mix, SLuint32 count,
                           const SLInterfaceID *interface_ids,
                           const SLboolean *required)
{
    if (!mix)
        return SL_RESULT_PARAMETER_INVALID;
    *mix = nullptr;
    if (!validate_interfaces(count, interface_ids, required, nullptr))
        return count && !interface_ids ? SL_RESULT_PARAMETER_INVALID
                                       : SL_RESULT_FEATURE_UNSUPPORTED;
    const std::shared_ptr<Object> object = make_object(ObjectKind::OutputMix);
    if (!object)
        return SL_RESULT_MEMORY_FAILURE;
    *mix = object_handle(object.get());
    trace_opensles("output mix created");
    return SL_RESULT_SUCCESS;
}

SLresult engine_create_player(SLEngineItf self, SLObjectItf *player,
                              SLDataSource *source, SLDataSink *sink,
                              SLuint32 count,
                              const SLInterfaceID *interface_ids,
                              const SLboolean *required)
{
    const std::shared_ptr<Object> engine = find_owner<SLEngineItf,
                                                       SLEngineItf_>(self);
    if (!player || !source || !sink || !source->pLocator ||
        !source->pFormat || !sink->pLocator)
        return SL_RESULT_PARAMETER_INVALID;
    *player = nullptr;
    if (!engine || engine->kind != ObjectKind::Engine ||
        !object_is_realized(engine))
        return SL_RESULT_PRECONDITIONS_VIOLATED;

    RequestedPlayerInterfaces requested;
    if (!validate_interfaces(count, interface_ids, required, &requested))
        return count && !interface_ids ? SL_RESULT_PARAMETER_INVALID
                                       : SL_RESULT_FEATURE_UNSUPPORTED;

    const auto *queue_locator = static_cast<const
        SLDataLocator_AndroidSimpleBufferQueue *>(source->pLocator);
    const auto *format = static_cast<const SLDataFormat_PCM *>(source->pFormat);
    const auto *mix_locator = static_cast<const SLDataLocator_OutputMix *>(
        sink->pLocator);
    if (queue_locator->locatorType !=
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE ||
        format->formatType != SL_DATAFORMAT_PCM ||
        (format->numChannels != 1 && format->numChannels != 2) ||
        format->samplesPerSec < 1000 ||
        format->bitsPerSample != SL_PCMSAMPLEFORMAT_FIXED_16 ||
        format->containerSize != SL_PCMSAMPLEFORMAT_FIXED_16 ||
        format->endianness != SL_BYTEORDER_LITTLEENDIAN ||
        !queue_locator->numBuffers ||
        mix_locator->locatorType != SL_DATALOCATOR_OUTPUTMIX ||
        sink->pFormat)
        return SL_RESULT_CONTENT_UNSUPPORTED;

    const uint32_t sample_rate =
        static_cast<uint32_t>((format->samplesPerSec + 500u) / 1000u);
    if (!sample_rate)
        return SL_RESULT_PARAMETER_INVALID;
    const std::shared_ptr<Object> output_mix =
        find_owner<SLObjectItf, SLObjectItf_>(mix_locator->outputMix);
    if (!output_mix || output_mix->kind != ObjectKind::OutputMix ||
        !object_is_realized(output_mix))
        return SL_RESULT_PRECONDITIONS_VIOLATED;

    const std::shared_ptr<Object> object = make_object(ObjectKind::AudioPlayer);
    if (!object)
        return SL_RESULT_MEMORY_FAILURE;
    object->requested_interfaces = requested;
    object->sample_rate = sample_rate;
    object->channels = format->numChannels;
    object->bits_per_sample = format->bitsPerSample;
    object->max_buffers = std::min<size_t>(
        queue_locator->numBuffers, 32);
    object->audio_queue = open_citadel::audio::create_open_sl_buffer_queue(
        object->sample_rate, object->channels, object->bits_per_sample,
        object->max_buffers, &opensles_buffer_completed, object.get(),
        std::static_pointer_cast<void>(object));
    if (!object->audio_queue) {
        destroy_object(object);
        return SL_RESULT_RESOURCE_ERROR;
    }
    *player = object_handle(object.get());
    trace_opensles("player created (%u Hz, %u channels, %zu buffers)",
                   object->sample_rate, object->channels,
                   object->max_buffers);
    return SL_RESULT_SUCCESS;
}

SLresult play_set_state(SLPlayItf self, SLuint32 state)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || object->kind != ObjectKind::AudioPlayer ||
        object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    if (state != SL_PLAYSTATE_STOPPED && state != SL_PLAYSTATE_PAUSED &&
        state != SL_PLAYSTATE_PLAYING)
        return SL_RESULT_PARAMETER_INVALID;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        if (!object->realized)
            return SL_RESULT_PRECONDITIONS_VIOLATED;
        object->play_state = state;
        queue = object->audio_queue;
    }
    if (queue)
        open_citadel::audio::set_open_sl_buffer_queue_playing(
            queue, state == SL_PLAYSTATE_PLAYING);
    return SL_RESULT_SUCCESS;
}

SLresult play_get_state(SLPlayItf self, SLuint32 *state)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || !state || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *state = object->play_state;
    return SL_RESULT_SUCCESS;
}

SLresult play_get_duration(SLPlayItf self, SLmillisecond *milliseconds)
{
    if (!find_owner<SLPlayItf, SLPlayItf_>(self) || !milliseconds)
        return SL_RESULT_PARAMETER_INVALID;
    *milliseconds = SL_TIME_UNKNOWN;
    return SL_RESULT_SUCCESS;
}

SLresult play_get_position(SLPlayItf self, SLmillisecond *milliseconds)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || !milliseconds)
        return SL_RESULT_PARAMETER_INVALID;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        queue = object->audio_queue;
    }
    const uint64_t position =
        open_citadel::audio::get_open_sl_buffer_queue_position_ms(queue);
    *milliseconds = static_cast<SLmillisecond>(std::min<uint64_t>(
        position, std::numeric_limits<SLmillisecond>::max() - 1u));
    return SL_RESULT_SUCCESS;
}

SLresult play_register_callback(SLPlayItf self, slPlayCallback callback,
                                void *context)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->play_callback = callback;
    object->play_callback_context = context;
    return SL_RESULT_SUCCESS;
}

SLresult play_set_callback_events(SLPlayItf self, SLuint32 event_flags)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->play_callback_events = event_flags;
    return SL_RESULT_SUCCESS;
}

SLresult play_get_callback_events(SLPlayItf self, SLuint32 *event_flags)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || !event_flags)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *event_flags = object->play_callback_events;
    return SL_RESULT_SUCCESS;
}

SLresult play_set_marker(SLPlayItf self, SLmillisecond milliseconds)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->marker_position = milliseconds;
    return SL_RESULT_SUCCESS;
}

SLresult play_clear_marker(SLPlayItf self)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->marker_position = SL_TIME_UNKNOWN;
    return SL_RESULT_SUCCESS;
}

SLresult play_get_marker(SLPlayItf self, SLmillisecond *milliseconds)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || !milliseconds)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *milliseconds = object->marker_position;
    return SL_RESULT_SUCCESS;
}

SLresult play_set_update_period(SLPlayItf self, SLmillisecond milliseconds)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->update_period = milliseconds;
    return SL_RESULT_SUCCESS;
}

SLresult play_get_update_period(SLPlayItf self, SLmillisecond *milliseconds)
{
    const std::shared_ptr<Object> object = find_owner<SLPlayItf,
                                                       SLPlayItf_>(self);
    if (!object || !milliseconds)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *milliseconds = object->update_period;
    return SL_RESULT_SUCCESS;
}

SLresult volume_set_level(SLVolumeItf self, SLmillibel level)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object || object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->volume_level = std::clamp<SLmillibel>(level, SL_MILLIBEL_MIN, 0);
    }
    update_audio_gain(object);
    return SL_RESULT_SUCCESS;
}

SLresult volume_get_level(SLVolumeItf self, SLmillibel *level)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object || !level)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *level = object->volume_level;
    return SL_RESULT_SUCCESS;
}

SLresult volume_get_max_level(SLVolumeItf self, SLmillibel *level)
{
    if (!find_owner<SLVolumeItf, SLVolumeItf_>(self) || !level)
        return SL_RESULT_PARAMETER_INVALID;
    *level = 0;
    return SL_RESULT_SUCCESS;
}

SLresult volume_set_mute(SLVolumeItf self, SLboolean mute)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->muted = mute != SL_BOOLEAN_FALSE;
    }
    update_audio_gain(object);
    return SL_RESULT_SUCCESS;
}

SLresult volume_get_mute(SLVolumeItf self, SLboolean *mute)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object || !mute)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *mute = object->muted ? SL_BOOLEAN_TRUE : SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult volume_enable_stereo(SLVolumeItf self, SLboolean enable)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->stereo_position_enabled = enable != SL_BOOLEAN_FALSE;
    }
    update_audio_gain(object);
    return SL_RESULT_SUCCESS;
}

SLresult volume_get_stereo_enabled(SLVolumeItf self, SLboolean *enabled)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object || !enabled)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *enabled = object->stereo_position_enabled
        ? SL_BOOLEAN_TRUE : SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult volume_set_stereo_position(SLVolumeItf self, SLpermille position)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->stereo_position =
            std::clamp<SLpermille>(position, -1000, 1000);
    }
    update_audio_gain(object);
    return SL_RESULT_SUCCESS;
}

SLresult volume_get_stereo_position(SLVolumeItf self, SLpermille *position)
{
    const std::shared_ptr<Object> object = find_owner<SLVolumeItf,
                                                       SLVolumeItf_>(self);
    if (!object || !position)
        return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    *position = object->stereo_position;
    return SL_RESULT_SUCCESS;
}

SLresult buffer_queue_enqueue(SLBufferQueueItf self, const void *buffer,
                              SLuint32 size)
{
    const std::shared_ptr<Object> object = find_owner<SLBufferQueueItf,
                                                       SLBufferQueueItf_>(self);
    if (!object || object->kind != ObjectKind::AudioPlayer ||
        object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    if (!buffer || !size)
        return SL_RESULT_PARAMETER_INVALID;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        if (!object->realized)
            return SL_RESULT_PRECONDITIONS_VIOLATED;
        queue = object->audio_queue;
    }
    if (!open_citadel::audio::enqueue_open_sl_buffer(queue, buffer, size))
        return SL_RESULT_RESOURCE_ERROR;
    return SL_RESULT_SUCCESS;
}

SLresult buffer_queue_clear(SLBufferQueueItf self)
{
    const std::shared_ptr<Object> object = find_owner<SLBufferQueueItf,
                                                       SLBufferQueueItf_>(self);
    if (!object || object->kind != ObjectKind::AudioPlayer)
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        queue = object->audio_queue;
    }
    open_citadel::audio::clear_open_sl_buffer_queue(queue);
    return SL_RESULT_SUCCESS;
}

SLresult buffer_queue_get_state(SLBufferQueueItf self,
                                SLBufferQueueState *state)
{
    const std::shared_ptr<Object> object = find_owner<SLBufferQueueItf,
                                                       SLBufferQueueItf_>(self);
    if (!object || !state || object->kind != ObjectKind::AudioPlayer)
        return SL_RESULT_PARAMETER_INVALID;
    std::shared_ptr<open_citadel::audio::OpenSlBufferQueue> queue;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        queue = object->audio_queue;
    }
    state->count = open_citadel::audio::get_open_sl_buffer_queue_count(queue);
    state->playIndex = open_citadel::audio::get_open_sl_buffer_queue_index(queue);
    return SL_RESULT_SUCCESS;
}

SLresult buffer_queue_register_callback(SLBufferQueueItf self,
                                        slBufferQueueCallback callback,
                                        void *context)
{
    const std::shared_ptr<Object> object = find_owner<SLBufferQueueItf,
                                                       SLBufferQueueItf_>(self);
    if (!object || object->kind != ObjectKind::AudioPlayer ||
        object->destroying.load(std::memory_order_acquire))
        return SL_RESULT_PRECONDITIONS_VIOLATED;
    std::lock_guard<std::mutex> lock(object->callback_mutex);
    object->buffer_queue_callback = callback;
    object->buffer_queue_callback_context = context;
    return SL_RESULT_SUCCESS;
}

} // namespace

void shutdown()
{
    std::vector<std::shared_ptr<Object>> objects;
    {
        std::lock_guard<std::mutex> lock(g_objects_mutex);
        g_shutting_down = true;
        try {
            objects.reserve(g_objects.size());
            for (const auto &entry : g_objects)
                objects.push_back(entry.second);
        } catch (...) {
            trace_opensles("could not allocate shutdown snapshot");
            return;
        }
    }
    for (const auto &object : objects)
        destroy_object(object);
    {
        std::lock_guard<std::mutex> lock(g_objects_mutex);
        g_handles.clear();
        g_objects.clear();
    }
    trace_opensles("compatibility layer shut down");
}

} // namespace open_citadel::opensles

extern "C" SLresult OPENSL_CALL slCreateEngine(
    SLObjectItf *engine, SLuint32 option_count, const SLEngineOption *options,
    SLuint32 interface_count, const SLInterfaceID *interface_ids,
    const SLboolean *required)
{
    (void)option_count;
    (void)options;
    if (!engine)
        return SL_RESULT_PARAMETER_INVALID;
    *engine = nullptr;
    if (interface_count && !interface_ids)
        return SL_RESULT_PARAMETER_INVALID;
    for (SLuint32 i = 0; i < interface_count; ++i) {
        if (!interface_ids[i])
            return SL_RESULT_PARAMETER_INVALID;
        if (interface_ids[i] != SL_IID_ENGINE && required && required[i])
            return SL_RESULT_FEATURE_UNSUPPORTED;
    }

    const std::shared_ptr<open_citadel::opensles::Object> object =
        open_citadel::opensles::make_object(
            open_citadel::opensles::ObjectKind::Engine);
    if (!object)
        return SL_RESULT_MEMORY_FAILURE;
    *engine = open_citadel::opensles::object_handle(object.get());
    open_citadel::opensles::trace_opensles("engine created");
    return SL_RESULT_SUCCESS;
}

DynLibFunction symtable_open_citadel_opensles[] = {
    NO_THUNK("slCreateEngine", (uintptr_t)&slCreateEngine),
    NO_THUNK("SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE),
    NO_THUNK("SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY),
    NO_THUNK("SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME),
    NO_THUNK("SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE),
    {nullptr, 0},
};
