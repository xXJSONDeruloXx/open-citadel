/*
 * Copyright (c) 2007-2009 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and/or associated documentation files (the "Materials"),
 * to deal in the Materials without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Materials, and to permit persons to whom the
 * Materials are furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER
 * DEALINGS IN THE MATERIALS.
 */
#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#define SLAPIENTRY __cdecl
#else
#define SLAPIENTRY
#endif

using SLint16 = int16_t;
using SLuint16 = uint16_t;
using SLint32 = int32_t;
using SLuint32 = uint32_t;
using SLboolean = SLuint32;
using SLmillibel = SLint16;
using SLmillisecond = SLuint32;
using SLpermille = SLint16;
using SLresult = SLuint32;

constexpr SLboolean SL_BOOLEAN_FALSE = 0;
constexpr SLboolean SL_BOOLEAN_TRUE = 1;
constexpr SLresult SL_RESULT_SUCCESS = 0;
constexpr SLresult SL_RESULT_PRECONDITIONS_VIOLATED = 1;
constexpr SLresult SL_RESULT_PARAMETER_INVALID = 2;
constexpr SLresult SL_RESULT_MEMORY_FAILURE = 3;
constexpr SLresult SL_RESULT_RESOURCE_ERROR = 4;
constexpr SLresult SL_RESULT_CONTENT_UNSUPPORTED = 9;
constexpr SLresult SL_RESULT_FEATURE_UNSUPPORTED = 12;
constexpr SLuint32 SL_OBJECT_STATE_UNREALIZED = 1;
constexpr SLuint32 SL_OBJECT_STATE_REALIZED = 2;
constexpr SLuint32 SL_OBJECT_EVENT_ASYNC_TERMINATION = 2;
constexpr SLuint32 SL_PLAYSTATE_STOPPED = 1;
constexpr SLuint32 SL_PLAYSTATE_PAUSED = 2;
constexpr SLuint32 SL_PLAYSTATE_PLAYING = 3;
constexpr SLmillisecond SL_TIME_UNKNOWN = 0xffffffffu;
constexpr SLuint32 SL_DATALOCATOR_OUTPUTMIX = 4;
constexpr SLuint32 SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE = 0x800007bd;
constexpr SLuint32 SL_DATAFORMAT_PCM = 2;
constexpr SLuint32 SL_PCMSAMPLEFORMAT_FIXED_16 = 16;
constexpr SLuint32 SL_BYTEORDER_LITTLEENDIAN = 2;
constexpr SLuint32 SL_SPEAKER_FRONT_LEFT = 0x1;
constexpr SLuint32 SL_SPEAKER_FRONT_RIGHT = 0x2;
constexpr SLuint32 SL_SPEAKER_FRONT_CENTER = 0x4;
constexpr SLmillibel SL_MILLIBEL_MIN = -32768;

struct SLInterfaceID_ {
    SLuint32 time_low;
    SLuint16 time_mid;
    SLuint16 time_hi_and_version;
    SLuint16 clock_seq;
    uint8_t node[6];
};
using SLInterfaceID = const SLInterfaceID_ *;

struct SLObjectItf_;
struct SLEngineItf_;
struct SLPlayItf_;
struct SLVolumeItf_;
struct SLBufferQueueItf_;

using SLObjectItf = const SLObjectItf_ * const *;
using SLEngineItf = const SLEngineItf_ * const *;
using SLPlayItf = const SLPlayItf_ * const *;
using SLVolumeItf = const SLVolumeItf_ * const *;
using SLBufferQueueItf = const SLBufferQueueItf_ * const *;

using slObjectCallback = void (SLAPIENTRY *)(
    SLObjectItf caller, const void *context, SLuint32 event,
    SLresult result, SLuint32 parameter, void *interface_pointer);
using slPlayCallback = void (SLAPIENTRY *)(
    SLPlayItf caller, void *context, SLuint32 event);
using slBufferQueueCallback = void (SLAPIENTRY *)(
    SLBufferQueueItf caller, void *context);

struct SLDataLocator_OutputMix {
    SLuint32 locatorType;
    SLObjectItf outputMix;
};

struct SLDataLocator_AndroidSimpleBufferQueue {
    SLuint32 locatorType;
    SLuint32 numBuffers;
};

struct SLDataFormat_PCM {
    SLuint32 formatType;
    SLuint32 numChannels;
    SLuint32 samplesPerSec;
    SLuint32 bitsPerSample;
    SLuint32 containerSize;
    SLuint32 channelMask;
    SLuint32 endianness;
};

struct SLDataSource {
    void *pLocator;
    void *pFormat;
};

struct SLDataSink {
    void *pLocator;
    void *pFormat;
};

struct SLEngineOption {
    SLuint32 feature;
    SLuint32 data;
};

struct SLBufferQueueState {
    SLuint32 count;
    SLuint32 playIndex;
};

struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf self, SLboolean async);
    SLresult (*Resume)(SLObjectItf self, SLboolean async);
    SLresult (*GetState)(SLObjectItf self, SLuint32 *state);
    SLresult (*GetInterface)(SLObjectItf self, SLInterfaceID iid,
                             void *interface_pointer);
    SLresult (*RegisterCallback)(SLObjectItf self,
                                 slObjectCallback callback, void *context);
    void (*AbortAsyncOperation)(SLObjectItf self);
    void (*Destroy)(SLObjectItf self);
    SLresult (*SetPriority)(SLObjectItf self, SLint32 priority,
                            SLboolean preemptable);
    SLresult (*GetPriority)(SLObjectItf self, SLint32 *priority,
                            SLboolean *preemptable);
    SLresult (*SetLossOfControlInterfaces)(
        SLObjectItf self, SLint16 count, SLInterfaceID *interface_ids,
        SLboolean enabled);
};

struct SLEngineItf_ {
    SLresult (*CreateLEDDevice)(SLEngineItf self, SLObjectItf *device,
                                SLuint32 device_id, SLuint32 count,
                                const SLInterfaceID *interface_ids,
                                const SLboolean *required);
    SLresult (*CreateVibraDevice)(SLEngineItf self, SLObjectItf *device,
                                  SLuint32 device_id, SLuint32 count,
                                  const SLInterfaceID *interface_ids,
                                  const SLboolean *required);
    SLresult (*CreateAudioPlayer)(SLEngineItf self, SLObjectItf *player,
                                  SLDataSource *source, SLDataSink *sink,
                                  SLuint32 count,
                                  const SLInterfaceID *interface_ids,
                                  const SLboolean *required);
    SLresult (*CreateAudioRecorder)(SLEngineItf self, SLObjectItf *recorder,
                                    SLDataSource *source, SLDataSink *sink,
                                    SLuint32 count,
                                    const SLInterfaceID *interface_ids,
                                    const SLboolean *required);
    SLresult (*CreateMidiPlayer)(
        SLEngineItf self, SLObjectItf *player, SLDataSource *midi_source,
        SLDataSource *bank_source, SLDataSink *audio_output,
        SLDataSink *vibra, SLDataSink *led_array, SLuint32 count,
        const SLInterfaceID *interface_ids, const SLboolean *required);
    SLresult (*CreateListener)(SLEngineItf self, SLObjectItf *listener,
                               SLuint32 count,
                               const SLInterfaceID *interface_ids,
                               const SLboolean *required);
    SLresult (*Create3DGroup)(SLEngineItf self, SLObjectItf *group,
                              SLuint32 count,
                              const SLInterfaceID *interface_ids,
                              const SLboolean *required);
    SLresult (*CreateOutputMix)(SLEngineItf self, SLObjectItf *mix,
                                SLuint32 count,
                                const SLInterfaceID *interface_ids,
                                const SLboolean *required);
    SLresult (*CreateMetadataExtractor)(
        SLEngineItf self, SLObjectItf *extractor, SLDataSource *source,
        SLuint32 count, const SLInterfaceID *interface_ids,
        const SLboolean *required);
    SLresult (*CreateExtensionObject)(
        SLEngineItf self, SLObjectItf *object, void *parameters,
        SLuint32 object_id, SLuint32 count,
        const SLInterfaceID *interface_ids, const SLboolean *required);
    SLresult (*QueryNumSupportedInterfaces)(SLEngineItf self,
                                             SLuint32 object_id,
                                             SLuint32 *count);
    SLresult (*QuerySupportedInterfaces)(SLEngineItf self,
                                          SLuint32 object_id,
                                          SLuint32 index,
                                          SLInterfaceID *iid);
    SLresult (*QueryNumSupportedExtensions)(SLEngineItf self,
                                             SLuint32 *count);
    SLresult (*QuerySupportedExtension)(SLEngineItf self, SLuint32 index,
                                        uint8_t *name, SLint16 *name_length);
    SLresult (*IsExtensionSupported)(SLEngineItf self, const uint8_t *name,
                                     SLboolean *supported);
};

struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf self, SLuint32 state);
    SLresult (*GetPlayState)(SLPlayItf self, SLuint32 *state);
    SLresult (*GetDuration)(SLPlayItf self, SLmillisecond *milliseconds);
    SLresult (*GetPosition)(SLPlayItf self, SLmillisecond *milliseconds);
    SLresult (*RegisterCallback)(SLPlayItf self, slPlayCallback callback,
                                 void *context);
    SLresult (*SetCallbackEventsMask)(SLPlayItf self, SLuint32 event_flags);
    SLresult (*GetCallbackEventsMask)(SLPlayItf self, SLuint32 *event_flags);
    SLresult (*SetMarkerPosition)(SLPlayItf self, SLmillisecond milliseconds);
    SLresult (*ClearMarkerPosition)(SLPlayItf self);
    SLresult (*GetMarkerPosition)(SLPlayItf self, SLmillisecond *milliseconds);
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf self,
                                        SLmillisecond milliseconds);
    SLresult (*GetPositionUpdatePeriod)(SLPlayItf self,
                                        SLmillisecond *milliseconds);
};

struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf self, SLmillibel level);
    SLresult (*GetVolumeLevel)(SLVolumeItf self, SLmillibel *level);
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf self, SLmillibel *level);
    SLresult (*SetMute)(SLVolumeItf self, SLboolean mute);
    SLresult (*GetMute)(SLVolumeItf self, SLboolean *mute);
    SLresult (*EnableStereoPosition)(SLVolumeItf self, SLboolean enable);
    SLresult (*IsEnabledStereoPosition)(SLVolumeItf self, SLboolean *enabled);
    SLresult (*SetStereoPosition)(SLVolumeItf self, SLpermille position);
    SLresult (*GetStereoPosition)(SLVolumeItf self, SLpermille *position);
};

struct SLBufferQueueItf_ {
    SLresult (*Enqueue)(SLBufferQueueItf self, const void *buffer,
                        SLuint32 size);
    SLresult (*Clear)(SLBufferQueueItf self);
    SLresult (*GetState)(SLBufferQueueItf self, SLBufferQueueState *state);
    SLresult (*RegisterCallback)(SLBufferQueueItf self,
                                 slBufferQueueCallback callback,
                                 void *context);
};

extern "C" {
extern const SLInterfaceID SL_IID_ENGINE;
extern const SLInterfaceID SL_IID_PLAY;
extern const SLInterfaceID SL_IID_VOLUME;
extern const SLInterfaceID SL_IID_BUFFERQUEUE;

SLresult SLAPIENTRY slCreateEngine(
    SLObjectItf *engine, SLuint32 option_count,
    const SLEngineOption *options, SLuint32 interface_count,
    const SLInterfaceID *interface_ids, const SLboolean *required);
}

#undef SLAPIENTRY
