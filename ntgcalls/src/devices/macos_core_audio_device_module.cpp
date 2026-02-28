//
// Native macOS audio input/output module backed by AudioQueue.
//

#ifdef IS_MACOS

#include <algorithm>
#include <cstring>
#include <cstdio>

#include <rtc_base/logging.h>

#include <ntgcalls/devices/macos_core_audio_device_module.hpp>
#include <ntgcalls/exceptions.hpp>

namespace ntgcalls {
    MacOSCoreAudioDeviceModule::MacOSCoreAudioDeviceModule(const AudioDescription* desc, const bool isCapture, BaseSink* sink):
        BaseIO(sink),
        BaseDeviceModule(desc, isCapture),
        BaseReader(sink),
        AudioMixer(sink)
    {
        try {
            deviceId = deviceMetadata["id"];
        } catch (...) {
            throw MediaDeviceError("Invalid device metadata");
        }
        RTC_LOG(LS_INFO) << "MacOSCoreAudioDeviceModule init isCapture=" << isCapture << " deviceId=" << deviceId;
        std::fprintf(
            stderr,
            "[ntgcalls] MacOSCoreAudioDeviceModule init isCapture=%d deviceId=%s\n",
            isCapture ? 1 : 0,
            deviceId.c_str()
        );
        std::fflush(stderr);

        frameSize = static_cast<size_t>(sink->frameSize());
        std::fprintf(
            stderr,
            "[ntgcalls] MacOSCoreAudioDeviceModule stream config isCapture=%d rate=%d channels=%d frameSize=%zu\n",
            isCapture ? 1 : 0,
            rate,
            channels,
            frameSize
        );
        std::fflush(stderr);

        streamFormat.mSampleRate = static_cast<Float64>(rate);
        streamFormat.mFormatID = kAudioFormatLinearPCM;
        streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mChannelsPerFrame = channels;
        streamFormat.mBitsPerChannel = 16;
        streamFormat.mBytesPerFrame = streamFormat.mChannelsPerFrame * sizeof(int16_t);
        streamFormat.mBytesPerPacket = streamFormat.mFramesPerPacket * streamFormat.mBytesPerFrame;

        const auto callbackStatus = isCapture
            ? AudioQueueNewInput(
                &streamFormat,
                &MacOSCoreAudioDeviceModule::InputCallback,
                this,
                nullptr,
                nullptr,
                0,
                &queue
            )
            : AudioQueueNewOutput(
                &streamFormat,
                &MacOSCoreAudioDeviceModule::OutputCallback,
                this,
                nullptr,
                nullptr,
                0,
                &queue
            );

        if (callbackStatus != noErr || !queue) {
            RTC_LOG(LS_ERROR) << "AudioQueue create failed status=" << callbackStatus;
            std::fprintf(stderr, "[ntgcalls] AudioQueue create failed status=%d\n", static_cast<int>(callbackStatus));
            std::fflush(stderr);
            throw MediaDeviceError("Failed to create macOS audio queue");
        }
        std::fprintf(stderr, "[ntgcalls] AudioQueue create ok status=%d queue=%p\n", static_cast<int>(callbackStatus), static_cast<void*>(queue));
        std::fflush(stderr);

        applyDevice();
        enqueueInitialBuffers();
    }

    MacOSCoreAudioDeviceModule::~MacOSCoreAudioDeviceModule() {
        if (!queue) {
            return;
        }
        AudioQueueStop(queue, true);
        AudioQueueDispose(queue, true);
        queue = nullptr;
    }

    bool MacOSCoreAudioDeviceModule::isSupported() {
        return true;
    }

    void MacOSCoreAudioDeviceModule::open() {
        if (opened || !queue) {
            return;
        }
        const auto status = AudioQueueStart(queue, nullptr);
        std::fprintf(stderr, "[ntgcalls] AudioQueueStart status=%d isCapture=%d\n", static_cast<int>(status), isCapture ? 1 : 0);
        std::fflush(stderr);
        if (status != noErr) {
            throw MediaDeviceError("Failed to start macOS audio queue");
        }
        opened = true;
    }

    void MacOSCoreAudioDeviceModule::applyDevice() {
        if (deviceId.empty() || !queue) {
            return;
        }
        auto* cfDeviceId = CFStringCreateWithCString(kCFAllocatorDefault, deviceId.c_str(), kCFStringEncodingUTF8);
        if (!cfDeviceId) {
            return;
        }
        const auto status = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &cfDeviceId, sizeof(cfDeviceId));
        if (status != noErr) {
            RTC_LOG(LS_WARNING) << "Failed to select macOS audio device uid=" << deviceId << " status=" << status;
            std::fprintf(stderr, "[ntgcalls] Failed to select macOS audio device uid=%s status=%d\n", deviceId.c_str(), static_cast<int>(status));
            std::fflush(stderr);
        } else {
            RTC_LOG(LS_INFO) << "Selected macOS audio device uid=" << deviceId;
            std::fprintf(stderr, "[ntgcalls] Selected macOS audio device uid=%s\n", deviceId.c_str());
            std::fflush(stderr);
        }
        CFRelease(cfDeviceId);
    }

    void MacOSCoreAudioDeviceModule::enqueueInitialBuffers() {
        if (!queue) {
            return;
        }
        const UInt32 queueBufferSize = static_cast<UInt32>(std::max<size_t>(frameSize * 2, 4096));
        for (size_t i = 0; i < kQueueBufferCount; ++i) {
            AudioQueueBufferRef bufferRef = nullptr;
            const auto allocStatus = AudioQueueAllocateBuffer(queue, queueBufferSize, &bufferRef);
            if (allocStatus != noErr || !bufferRef) {
                std::fprintf(
                    stderr,
                    "[ntgcalls] AudioQueueAllocateBuffer failed status=%d index=%zu size=%u\n",
                    static_cast<int>(allocStatus),
                    i,
                    queueBufferSize
                );
                std::fflush(stderr);
                throw MediaDeviceError("Failed to allocate macOS audio queue buffer");
            }
            queueBuffers[i] = bufferRef;
            if (isCapture) {
                bufferRef->mAudioDataByteSize = queueBufferSize;
                const auto enqueueStatus = AudioQueueEnqueueBuffer(queue, bufferRef, 0, nullptr);
                if (enqueueStatus != noErr) {
                    std::fprintf(
                        stderr,
                        "[ntgcalls] AudioQueueEnqueueBuffer(input) failed status=%d index=%zu\n",
                        static_cast<int>(enqueueStatus),
                        i
                    );
                    std::fflush(stderr);
                    throw MediaDeviceError("Failed to enqueue macOS input buffer");
                }
            } else {
                memset(bufferRef->mAudioData, 0, bufferRef->mAudioDataBytesCapacity);
                bufferRef->mAudioDataByteSize = bufferRef->mAudioDataBytesCapacity;
                const auto enqueueStatus = AudioQueueEnqueueBuffer(queue, bufferRef, 0, nullptr);
                if (enqueueStatus != noErr) {
                    std::fprintf(
                        stderr,
                        "[ntgcalls] AudioQueueEnqueueBuffer(output) failed status=%d index=%zu\n",
                        static_cast<int>(enqueueStatus),
                        i
                    );
                    std::fflush(stderr);
                    throw MediaDeviceError("Failed to enqueue macOS output buffer");
                }
            }
        }
    }

    void MacOSCoreAudioDeviceModule::InputCallback(
        void* userData,
        const AudioQueueRef inAQ,
        const AudioQueueBufferRef inBuffer,
        const AudioTimeStamp*,
        const UInt32,
        const AudioStreamPacketDescription*
    ) {
        auto* self = static_cast<MacOSCoreAudioDeviceModule*>(userData);
        if (!self || !self->running || !self->status || !inBuffer || inBuffer->mAudioDataByteSize == 0) {
            if (inAQ && inBuffer) {
                AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
            }
            return;
        }

        {
            std::lock_guard lock(self->bufferMutex);
            auto* src = static_cast<uint8_t*>(inBuffer->mAudioData);
            self->buffer.insert(self->buffer.end(), src, src + inBuffer->mAudioDataByteSize);
            while (self->buffer.size() >= self->frameSize) {
                auto chunk = bytes::make_unique_binary(self->frameSize);
                memcpy(chunk.get(), self->buffer.data(), self->frameSize);
                self->buffer.erase(self->buffer.begin(), self->buffer.begin() + static_cast<std::ptrdiff_t>(self->frameSize));
                self->dataCallback(std::move(chunk), {});
            }
        }

        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
    }

    void MacOSCoreAudioDeviceModule::OutputCallback(void* userData, const AudioQueueRef inAQ, const AudioQueueBufferRef inBuffer) {
        auto* self = static_cast<MacOSCoreAudioDeviceModule*>(userData);
        if (!self || !self->running || !inBuffer) {
            if (inAQ && inBuffer) {
                AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
            }
            return;
        }

        std::lock_guard lock(self->bufferMutex);
        const auto capacity = static_cast<size_t>(inBuffer->mAudioDataBytesCapacity);
        auto* dst = static_cast<uint8_t*>(inBuffer->mAudioData);
        const auto bytesToCopy = std::min(capacity, self->buffer.size());
        if (bytesToCopy > 0) {
            memcpy(dst, self->buffer.data(), bytesToCopy);
            self->buffer.erase(self->buffer.begin(), self->buffer.begin() + static_cast<std::ptrdiff_t>(bytesToCopy));
        }
        if (bytesToCopy < capacity) {
            memset(dst + bytesToCopy, 0, capacity - bytesToCopy);
        }
        inBuffer->mAudioDataByteSize = static_cast<UInt32>(capacity);
        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
    }

    void MacOSCoreAudioDeviceModule::onData(const bytes::unique_binary data) {
        std::lock_guard lock(bufferMutex);
        const auto* src = reinterpret_cast<const uint8_t*>(data.get());
        buffer.insert(buffer.end(), src, src + frameSize);
    }
} // ntgcalls

#endif
