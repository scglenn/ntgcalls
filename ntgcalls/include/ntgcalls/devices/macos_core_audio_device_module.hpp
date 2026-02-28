//
// Native macOS audio input/output module backed by AudioQueue.
//

#pragma once

#ifdef IS_MACOS

#include <array>
#include <mutex>
#include <string>
#include <vector>

#include <AudioToolbox/AudioToolbox.h>

#include <ntgcalls/devices/base_device_module.hpp>
#include <ntgcalls/io/audio_mixer.hpp>
#include <ntgcalls/io/base_reader.hpp>

namespace ntgcalls {

    class MacOSCoreAudioDeviceModule final: public BaseDeviceModule, public BaseReader, public AudioMixer {
        static constexpr size_t kQueueBufferCount = 3;

        AudioQueueRef queue = nullptr;
        AudioStreamBasicDescription streamFormat{};
        std::array<AudioQueueBufferRef, kQueueBufferCount> queueBuffers{};

        std::mutex bufferMutex;
        std::vector<uint8_t> buffer;
        size_t frameSize = 0;
        std::string deviceId;
        bool opened = false;

        static void InputCallback(
            void* userData,
            AudioQueueRef inAQ,
            AudioQueueBufferRef inBuffer,
            const AudioTimeStamp* inStartTime,
            UInt32 inNumberPacketDescriptions,
            const AudioStreamPacketDescription* inPacketDescs
        );

        static void OutputCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);

        void applyDevice();
        void enqueueInitialBuffers();

        void onData(bytes::unique_binary data) override;

    public:
        MacOSCoreAudioDeviceModule(const AudioDescription* desc, bool isCapture, BaseSink* sink);

        ~MacOSCoreAudioDeviceModule() override;

        static bool isSupported();

        void open() override;
    };

} // ntgcalls

#endif

