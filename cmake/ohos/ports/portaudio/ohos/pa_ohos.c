/*
 * PortAudio host API for HarmonyOS NEXT / OpenHarmony (OHAudio).
 *
 * Output-only, callback interface. The OHAudio renderer's write callback
 * drives PortAudio's buffer processor, which in turn calls the stream
 * callback provided by the application.
 *
 * Modelled on PortAudio's src/hostapi/skeleton/pa_hostapi_skeleton.c.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ohaudio/native_audio_common.h>
#include <ohaudio/native_audio_device_base.h>
#include <ohaudio/native_audio_manager.h>
#include <ohaudio/native_audio_routing_manager.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostream_base.h>
#include <ohaudio/native_audiostreambuilder.h>
#include <hilog/log.h>

#include "portaudio.h"

#include "pa_util.h"
#include "pa_allocation.h"
#include "pa_hostapi.h"
#include "pa_stream.h"
#include "pa_cpuload.h"
#include "pa_process.h"
#include "pa_debugprint.h"

#define PA_OHOS_LOG_DOMAIN 0x0000
#define PA_OHOS_LOG_TAG "PortAudioOHOS"
#define PA_OHOS_LOG(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, PA_OHOS_LOG_DOMAIN, PA_OHOS_LOG_TAG, fmt, ##__VA_ARGS__)

#define PA_OHOS_MAX_DEVICES 8
#define PA_OHOS_DEFAULT_SAMPLE_RATE 48000
#define PA_OHOS_DEFAULT_CHANNELS 2
#define PA_OHOS_DEFAULT_FRAMES_PER_BUFFER 512

typedef struct PaOhosHostApiRepresentation PaOhosHostApiRepresentation;

typedef struct PaOhosStream
{
    PaOhosHostApiRepresentation* hostApi;
    PaUtilStreamRepresentation streamRepresentation;
    PaUtilCpuLoadMeasurer cpuLoadMeasurer;
    PaUtilBufferProcessor bufferProcessor;

    OH_AudioStreamBuilder* builder;
    OH_AudioRenderer* renderer;

    volatile sig_atomic_t isActive;
    volatile sig_atomic_t isStopped;
    volatile sig_atomic_t aborting;
    volatile sig_atomic_t callbackResult;

    unsigned long framesPerHostBuffer;
    int bytesPerHostFrame;
    int outputChannelCount;
    PaSampleFormat hostOutputSampleFormat;
    double sampleRate;
    double startTime;
    unsigned long long framesPlayed;
} PaOhosStream;

struct PaOhosHostApiRepresentation
{
    PaUtilHostApiRepresentation inheritedHostApiRep;
    PaUtilStreamInterface callbackStreamInterface;
    PaUtilStreamInterface blockingStreamInterface;
    PaUtilAllocationGroup* allocations;

    OH_AudioRoutingManager* routingManager;
    int deviceCount;
    char* deviceNames[PA_OHOS_MAX_DEVICES];
    int deviceChannels[PA_OHOS_MAX_DEVICES];
    double deviceSampleRates[PA_OHOS_MAX_DEVICES];
};

static PaError Terminate(PaUtilHostApiRepresentation* hostApi);
static PaError IsFormatSupported(PaUtilHostApiRepresentation* hostApi,
        const PaStreamParameters* inputParameters,
        const PaStreamParameters* outputParameters,
        double sampleRate);
static PaError OpenStream(PaUtilHostApiRepresentation* hostApi,
        PaStream** s,
        const PaStreamParameters* inputParameters,
        const PaStreamParameters* outputParameters,
        double sampleRate,
        unsigned long framesPerBuffer,
        PaStreamFlags streamFlags,
        PaStreamCallback* streamCallback,
        void* userData);
static PaError CloseStream(PaStream* stream);
static PaError StartStream(PaStream* stream);
static PaError StopStream(PaStream* stream);
static PaError AbortStream(PaStream* stream);
static PaError IsStreamStopped(PaStream* s);
static PaError IsStreamActive(PaStream* stream);
static PaTime GetStreamTime(PaStream* stream);
static double GetStreamCpuLoad(PaStream* stream);
static PaError ReadStream(PaStream* stream, void* buffer, unsigned long frames);
static PaError WriteStream(PaStream* stream, const void* buffer, unsigned long frames);

static void resetStreamState(PaOhosStream* stream)
{
    stream->isActive = 0;
    stream->isStopped = 1;
    stream->aborting = 0;
    stream->callbackResult = paContinue;
    stream->framesPlayed = 0;
    stream->startTime = 0.0;
}

/* ------------------------------------------------------------------ */
/* OHAudio write callback: pull audio from PortAudio's callback.       */
/* ------------------------------------------------------------------ */
static int32_t ohosRendererOnWriteData(
        OH_AudioRenderer* renderer, void* userData, void* buffer, int32_t length)
{
    PaOhosStream* stream = (PaOhosStream*)userData;
    if (!stream || !buffer || length <= 0 || stream->bytesPerHostFrame <= 0) {
        if (buffer && length > 0) {
            memset(buffer, 0, (size_t)length);
        }
        return 0;
    }

    if (stream->aborting || !stream->isActive || stream->callbackResult != paContinue) {
        memset(buffer, 0, (size_t)length);
        return 0;
    }

    const unsigned long frames = (unsigned long)length / (unsigned long)stream->bytesPerHostFrame;

    PaUtil_BeginCpuLoadMeasurement(&stream->cpuLoadMeasurer);

    PaStreamCallbackTimeInfo timeInfo;
    timeInfo.currentTime = PaUtil_GetTime();
    timeInfo.inputBufferAdcTime = 0.0;
    timeInfo.outputBufferDacTime = stream->startTime + (double)stream->framesPlayed / stream->sampleRate;

    int callbackResult = paContinue;
    PaUtil_BeginBufferProcessing(&stream->bufferProcessor, &timeInfo, 0);
    PaUtil_SetNoInput(&stream->bufferProcessor);
    PaUtil_SetOutputFrameCount(&stream->bufferProcessor, (unsigned int)frames);
    PaUtil_SetInterleavedOutputChannels(
            &stream->bufferProcessor, 0, buffer, stream->outputChannelCount);
    PaUtil_EndBufferProcessing(&stream->bufferProcessor, &callbackResult);

    PaUtil_EndCpuLoadMeasurement(&stream->cpuLoadMeasurer, frames);
    stream->framesPlayed += frames;

    if (callbackResult != paContinue) {
        /* Do not stop the renderer from inside its own callback; remember
         * the result and keep feeding silence until the app thread reacts. */
        stream->callbackResult = callbackResult;
        stream->isActive = 0;
    }

    return 0;
}

static int32_t ohosRendererOnStreamEvent(
        OH_AudioRenderer* renderer, void* userData, OH_AudioStream_Event event)
{
    (void)renderer;
    (void)userData;
    (void)event;
    return 0;
}

static int32_t ohosRendererOnInterruptEvent(OH_AudioRenderer* renderer,
        void* userData,
        OH_AudioInterrupt_ForceType type,
        OH_AudioInterrupt_Hint hint)
{
    /* Called when another app takes audio focus. Keep the stream alive; the
     * audio server stops and restarts the renderer itself. */
    (void)renderer;
    (void)userData;
    PA_OHOS_LOG("renderer interrupted: forceType=%d hint=%d", (int)type, (int)hint);
    return 0;
}

static int32_t ohosRendererOnError(
        OH_AudioRenderer* renderer, void* userData, OH_AudioStream_Result error)
{
    (void)renderer;
    (void)userData;
    PA_OHOS_LOG("renderer error: %d", (int)error);
    return 0;
}

static PaError buildDeviceListFromRoutingManager(PaOhosHostApiRepresentation* ohosHostApi)
{
    OH_AudioDeviceDescriptorArray* descriptors = NULL;
    OH_AudioCommon_Result res = OH_AudioRoutingManager_GetDevices(
            ohosHostApi->routingManager, AUDIO_DEVICE_FLAG_OUTPUT, &descriptors);
    if (res != AUDIOCOMMON_RESULT_SUCCESS || !descriptors) {
        PA_OHOS_LOG("GetDevices failed: %d", (int)res);
        return paDeviceUnavailable;
    }

    for (uint32_t i = 0; i < descriptors->size && ohosHostApi->deviceCount < PA_OHOS_MAX_DEVICES; i++) {
        OH_AudioDeviceDescriptor* desc = descriptors->descriptors[i];
        if (!desc) {
            continue;
        }

        char* name = NULL;
        if (OH_AudioDeviceDescriptor_GetDeviceName(desc, &name) != AUDIOCOMMON_RESULT_SUCCESS ||
                !name) {
            name = NULL;
        }

        uint32_t* channelCounts = NULL;
        uint32_t channelCountSize = 0;
        int channels = PA_OHOS_DEFAULT_CHANNELS;
        if (OH_AudioDeviceDescriptor_GetDeviceChannelCounts(
                    desc, &channelCounts, &channelCountSize) == AUDIOCOMMON_RESULT_SUCCESS &&
                channelCounts && channelCountSize > 0) {
            channels = (int)channelCounts[0];
            for (uint32_t c = 1; c < channelCountSize; c++) {
                if ((int)channelCounts[c] > channels) {
                    channels = (int)channelCounts[c];
                }
            }
        }

        uint32_t* sampleRates = NULL;
        uint32_t sampleRateSize = 0;
        double rate = PA_OHOS_DEFAULT_SAMPLE_RATE;
        if (OH_AudioDeviceDescriptor_GetDeviceSampleRates(
                    desc, &sampleRates, &sampleRateSize) == AUDIOCOMMON_RESULT_SUCCESS &&
                sampleRates && sampleRateSize > 0) {
            rate = (double)sampleRates[0];
            for (uint32_t r = 0; r < sampleRateSize; r++) {
                /* prefer the usual music rates */
                if (sampleRates[r] == 48000 || sampleRates[r] == 44100) {
                    rate = (double)sampleRates[r];
                    if (sampleRates[r] == 48000) {
                        break;
                    }
                }
            }
        }

        const int idx = ohosHostApi->deviceCount;
        const size_t nameLen = name ? strlen(name) : 0;
        char* nameCopy = (char*)PaUtil_GroupAllocateMemory(
                ohosHostApi->allocations, (nameLen > 0 ? nameLen : 8) + 1);
        if (!nameCopy) {
            continue;
        }
        if (nameLen > 0) {
            memcpy(nameCopy, name, nameLen + 1);
        } else {
            strcpy(nameCopy, "Speaker");
        }
        ohosHostApi->deviceNames[idx] = nameCopy;
        ohosHostApi->deviceChannels[idx] = channels;
        ohosHostApi->deviceSampleRates[idx] = rate;
        ohosHostApi->deviceCount++;

        PA_OHOS_LOG("output device %d: %s channels=%d rate=%d", idx, nameCopy, channels, (int)rate);
    }

    OH_AudioRoutingManager_ReleaseDevices(ohosHostApi->routingManager, descriptors);

    if (ohosHostApi->deviceCount == 0) {
        /* Fall back to a synthetic default device so audio can still start. */
        char* nameCopy = (char*)PaUtil_GroupAllocateMemory(ohosHostApi->allocations, 32);
        if (nameCopy) {
            strcpy(nameCopy, "OHAudio Default Output");
            ohosHostApi->deviceNames[0] = nameCopy;
            ohosHostApi->deviceChannels[0] = PA_OHOS_DEFAULT_CHANNELS;
            ohosHostApi->deviceSampleRates[0] = PA_OHOS_DEFAULT_SAMPLE_RATE;
            ohosHostApi->deviceCount = 1;
            PA_OHOS_LOG("using synthetic default output device");
        }
    }

    return paNoError;
}

PaError PaOhos_Initialize(PaUtilHostApiRepresentation** hostApi, PaHostApiIndex hostApiIndex)
{
    PaError result = paNoError;
    PaOhosHostApiRepresentation* ohosHostApi = NULL;
    PaDeviceInfo* deviceInfoArray = NULL;
    int i;

    ohosHostApi = (PaOhosHostApiRepresentation*)PaUtil_AllocateMemory(
            sizeof(PaOhosHostApiRepresentation));
    if (!ohosHostApi) {
        return paInsufficientMemory;
    }
    memset(ohosHostApi, 0, sizeof(PaOhosHostApiRepresentation));

    ohosHostApi->allocations = PaUtil_CreateAllocationGroup();
    if (!ohosHostApi->allocations) {
        PaUtil_FreeMemory(ohosHostApi);
        return paInsufficientMemory;
    }

    if (OH_AudioManager_GetAudioRoutingManager(&ohosHostApi->routingManager) !=
                    AUDIOCOMMON_RESULT_SUCCESS ||
            !ohosHostApi->routingManager) {
        PA_OHOS_LOG("audio manager unavailable, using synthetic device");
        ohosHostApi->routingManager = NULL;
    }

    if (ohosHostApi->routingManager) {
        const PaError deviceResult = buildDeviceListFromRoutingManager(ohosHostApi);
        if (deviceResult != paNoError) {
            PA_OHOS_LOG("device enumeration failed (%d), using synthetic device", (int)deviceResult);
            char* nameCopy = (char*)PaUtil_GroupAllocateMemory(ohosHostApi->allocations, 32);
            if (nameCopy) {
                strcpy(nameCopy, "OHAudio Default Output");
                ohosHostApi->deviceNames[0] = nameCopy;
                ohosHostApi->deviceChannels[0] = PA_OHOS_DEFAULT_CHANNELS;
                ohosHostApi->deviceSampleRates[0] = PA_OHOS_DEFAULT_SAMPLE_RATE;
                ohosHostApi->deviceCount = 1;
            }
        }
    }

    *hostApi = &ohosHostApi->inheritedHostApiRep;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paOHOS;
    (*hostApi)->info.name = "OHOS OHAudio";
    (*hostApi)->info.defaultInputDevice = paNoDevice;
    (*hostApi)->info.defaultOutputDevice = paNoDevice;

    if (ohosHostApi->deviceCount > 0) {
        (*hostApi)->deviceInfos = (PaDeviceInfo**)PaUtil_GroupAllocateMemory(
                ohosHostApi->allocations, sizeof(PaDeviceInfo*) * ohosHostApi->deviceCount);
        deviceInfoArray = (PaDeviceInfo*)PaUtil_GroupAllocateMemory(
                ohosHostApi->allocations, sizeof(PaDeviceInfo) * ohosHostApi->deviceCount);
        if (!(*hostApi)->deviceInfos || !deviceInfoArray) {
            result = paInsufficientMemory;
            goto error;
        }

        for (i = 0; i < ohosHostApi->deviceCount; i++) {
            PaDeviceInfo* deviceInfo = &deviceInfoArray[i];
            deviceInfo->structVersion = 2;
            deviceInfo->hostApi = hostApiIndex;
            deviceInfo->name = ohosHostApi->deviceNames[i];
            deviceInfo->maxInputChannels = 0;
            deviceInfo->maxOutputChannels = ohosHostApi->deviceChannels[i];
            deviceInfo->defaultLowInputLatency = 0.0;
            deviceInfo->defaultLowOutputLatency = 0.02;
            deviceInfo->defaultHighInputLatency = 0.0;
            deviceInfo->defaultHighOutputLatency = 0.08;
            deviceInfo->defaultSampleRate = ohosHostApi->deviceSampleRates[i];
            (*hostApi)->deviceInfos[i] = deviceInfo;
        }
        (*hostApi)->info.deviceCount = ohosHostApi->deviceCount;
        (*hostApi)->info.defaultOutputDevice = 0;
    }

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    PaUtil_InitializeStreamInterface(&ohosHostApi->callbackStreamInterface, CloseStream, StartStream,
            StopStream, AbortStream, IsStreamStopped, IsStreamActive, GetStreamTime,
            GetStreamCpuLoad, PaUtil_DummyRead, PaUtil_DummyWrite, PaUtil_DummyGetReadAvailable,
            PaUtil_DummyGetWriteAvailable);

    PaUtil_InitializeStreamInterface(&ohosHostApi->blockingStreamInterface, CloseStream, StartStream,
            StopStream, AbortStream, IsStreamStopped, IsStreamActive, GetStreamTime,
            GetStreamCpuLoad, ReadStream, WriteStream, PaUtil_DummyGetReadAvailable,
            PaUtil_DummyGetWriteAvailable);

    return result;

error:
    if (ohosHostApi->allocations) {
        PaUtil_FreeAllAllocations(ohosHostApi->allocations);
        PaUtil_DestroyAllocationGroup(ohosHostApi->allocations);
    }
    PaUtil_FreeMemory(ohosHostApi);
    return result;
}

static PaError Terminate(PaUtilHostApiRepresentation* hostApi)
{
    PaOhosHostApiRepresentation* ohosHostApi = (PaOhosHostApiRepresentation*)hostApi;
    if (ohosHostApi->allocations) {
        PaUtil_FreeAllAllocations(ohosHostApi->allocations);
        PaUtil_DestroyAllocationGroup(ohosHostApi->allocations);
    }
    PaUtil_FreeMemory(ohosHostApi);
    return paNoError;
}

static PaError IsFormatSupported(PaUtilHostApiRepresentation* hostApi,
        const PaStreamParameters* inputParameters,
        const PaStreamParameters* outputParameters,
        double sampleRate)
{
    (void)hostApi;

    if (inputParameters && inputParameters->channelCount > 0) {
        return paInvalidChannelCount; /* capture is not implemented yet */
    }
    if (!outputParameters || outputParameters->channelCount <= 0) {
        return paInvalidChannelCount;
    }
    if (outputParameters->device != paUseHostApiSpecificDeviceSpecification &&
            (outputParameters->device < 0 || outputParameters->device >= Pa_GetDeviceCount())) {
        return paInvalidDevice;
    }
    if (sampleRate <= 0.0) {
        return paInvalidSampleRate;
    }
    return paNoError;
}

static PaError OpenStream(PaUtilHostApiRepresentation* hostApi, PaStream** s,
        const PaStreamParameters* inputParameters, const PaStreamParameters* outputParameters,
        double sampleRate, unsigned long framesPerBuffer, PaStreamFlags streamFlags,
        PaStreamCallback* streamCallback, void* userData)
{
    PaError result = paNoError;
    PaOhosHostApiRepresentation* ohosHostApi = (PaOhosHostApiRepresentation*)hostApi;
    PaOhosStream* stream = NULL;
    unsigned long framesPerHostBuffer;

    if (!streamCallback) {
        /* Blocking read/write streams are not supported by this host API. */
        return paUnanticipatedHostError;
    }

    result = IsFormatSupported(hostApi, inputParameters, outputParameters, sampleRate);
    if (result != paNoError) {
        return result;
    }

    stream = (PaOhosStream*)PaUtil_GroupAllocateMemory(
            ohosHostApi->allocations, sizeof(PaOhosStream));
    if (!stream) {
        return paInsufficientMemory;
    }
    memset(stream, 0, sizeof(PaOhosStream));
    stream->hostApi = ohosHostApi;

    framesPerHostBuffer = (framesPerBuffer == paFramesPerBufferUnspecified)
            ? PA_OHOS_DEFAULT_FRAMES_PER_BUFFER
            : framesPerBuffer;

    PaUtil_InitializeStreamRepresentation(&stream->streamRepresentation,
            ((streamCallback) ? &ohosHostApi->callbackStreamInterface
                              : &ohosHostApi->blockingStreamInterface),
            streamCallback, userData);
    PaUtil_InitializeCpuLoadMeasurer(&stream->cpuLoadMeasurer, sampleRate);

    stream->outputChannelCount = outputParameters->channelCount;
    stream->hostOutputSampleFormat = paFloat32;
    stream->sampleRate = sampleRate;
    stream->framesPerHostBuffer = framesPerHostBuffer;
    stream->bytesPerHostFrame =
            Pa_GetSampleSize(stream->hostOutputSampleFormat) * stream->outputChannelCount;
    resetStreamState(stream);

    result = PaUtil_InitializeBufferProcessor(&stream->bufferProcessor,
            0, /* numInputChannels */
            paInt16, /* inputSampleFormat */
            paInt16, /* hostInputSampleFormat */
            outputParameters->channelCount, outputParameters->sampleFormat,
            stream->hostOutputSampleFormat, sampleRate, streamFlags, framesPerBuffer,
            framesPerHostBuffer, paUtilFixedHostBufferSize, streamCallback, userData);
    if (result != paNoError) {
        goto error;
    }

    /* Create the OHAudio renderer. */
    OH_AudioStream_Result audioResult =
            OH_AudioStreamBuilder_Create(&stream->builder, AUDIOSTREAM_TYPE_RENDERER);
    if (audioResult != AUDIOSTREAM_SUCCESS || !stream->builder) {
        PA_OHOS_LOG("OH_AudioStreamBuilder_Create failed: %d", (int)audioResult);
        result = paUnanticipatedHostError;
        goto error;
    }

    OH_AudioStreamBuilder_SetRendererInfo(stream->builder, AUDIOSTREAM_USAGE_MUSIC);
    OH_AudioStreamBuilder_SetSamplingRate(stream->builder, (int32_t)sampleRate);
    OH_AudioStreamBuilder_SetChannelCount(stream->builder, outputParameters->channelCount);
    OH_AudioStreamBuilder_SetSampleFormat(stream->builder, AUDIOSTREAM_SAMPLE_F32LE);
    OH_AudioStreamBuilder_SetLatencyMode(stream->builder, AUDIOSTREAM_LATENCY_MODE_NORMAL);

    OH_AudioRenderer_Callbacks callbacks;
    callbacks.OH_AudioRenderer_OnWriteData = ohosRendererOnWriteData;
    callbacks.OH_AudioRenderer_OnStreamEvent = ohosRendererOnStreamEvent;
    callbacks.OH_AudioRenderer_OnInterruptEvent = ohosRendererOnInterruptEvent;
    callbacks.OH_AudioRenderer_OnError = ohosRendererOnError;
    OH_AudioStreamBuilder_SetRendererCallback(stream->builder, callbacks, stream);

    audioResult = OH_AudioStreamBuilder_GenerateRenderer(stream->builder, &stream->renderer);
    if (audioResult != AUDIOSTREAM_SUCCESS || !stream->renderer) {
        PA_OHOS_LOG("OH_AudioStreamBuilder_GenerateRenderer failed: %d", (int)audioResult);
        result = paUnanticipatedHostError;
        goto error;
    }

    PA_OHOS_LOG("stream opened: %d Hz, %d ch, %lu frames/host buffer", (int)sampleRate,
            outputParameters->channelCount, framesPerHostBuffer);

    *s = (PaStream*)stream;
    return result;

error:
    if (stream) {
        if (stream->renderer) {
            OH_AudioRenderer_Release(stream->renderer);
        }
        if (stream->builder) {
            OH_AudioStreamBuilder_Destroy(stream->builder);
        }
        PaUtil_TerminateBufferProcessor(&stream->bufferProcessor);
        PaUtil_TerminateStreamRepresentation(&stream->streamRepresentation);
        PaUtil_GroupFreeMemory(ohosHostApi->allocations, stream);
    }
    return result;
}

static PaError CloseStream(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    PaOhosHostApiRepresentation* ohosHostApi = stream->hostApi;

    if (stream->renderer) {
        OH_AudioRenderer_Release(stream->renderer);
        stream->renderer = NULL;
    }
    if (stream->builder) {
        OH_AudioStreamBuilder_Destroy(stream->builder);
        stream->builder = NULL;
    }

    PaUtil_TerminateBufferProcessor(&stream->bufferProcessor);
    PaUtil_TerminateStreamRepresentation(&stream->streamRepresentation);
    PaUtil_GroupFreeMemory(ohosHostApi->allocations, stream);
    return paNoError;
}

static PaError StartStream(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;

    PaUtil_ResetBufferProcessor(&stream->bufferProcessor);
    stream->aborting = 0;
    stream->callbackResult = paContinue;

    const OH_AudioStream_Result res = OH_AudioRenderer_Start(stream->renderer);
    if (res != AUDIOSTREAM_SUCCESS) {
        PA_OHOS_LOG("OH_AudioRenderer_Start failed: %d", (int)res);
        return paUnanticipatedHostError;
    }

    stream->startTime = PaUtil_GetTime();
    stream->framesPlayed = 0;
    stream->isActive = 1;
    stream->isStopped = 0;
    PA_OHOS_LOG("stream started");
    return paNoError;
}

static PaError StopStream(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    if (!stream->isActive) {
        stream->isStopped = 1;
        return paNoError;
    }

    OH_AudioRenderer_Stop(stream->renderer);
    OH_AudioRenderer_Flush(stream->renderer);
    stream->isActive = 0;
    stream->isStopped = 1;
    PA_OHOS_LOG("stream stopped");
    return paNoError;
}

static PaError AbortStream(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    stream->aborting = 1;
    if (stream->renderer) {
        OH_AudioRenderer_Stop(stream->renderer);
        OH_AudioRenderer_Flush(stream->renderer);
    }
    stream->isActive = 0;
    stream->isStopped = 1;
    PA_OHOS_LOG("stream aborted");
    return paNoError;
}

static PaError IsStreamStopped(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    return stream->isStopped ? 1 : 0;
}

static PaError IsStreamActive(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    return stream->isActive ? 1 : 0;
}

static PaTime GetStreamTime(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    if (!stream->isActive) {
        return stream->startTime;
    }
    return PaUtil_GetTime() - stream->startTime;
}

static double GetStreamCpuLoad(PaStream* s)
{
    PaOhosStream* stream = (PaOhosStream*)s;
    return PaUtil_GetCpuLoad(&stream->cpuLoadMeasurer);
}

static PaError ReadStream(PaStream* s, void* buffer, unsigned long frames)
{
    (void)s;
    (void)buffer;
    (void)frames;
    return paUnanticipatedHostError;
}

static PaError WriteStream(PaStream* s, const void* buffer, unsigned long frames)
{
    (void)s;
    (void)buffer;
    (void)frames;
    return paUnanticipatedHostError;
}
