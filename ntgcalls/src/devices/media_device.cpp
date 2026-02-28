//
// Created by Laky64 on 17/09/24.
//

#include <ntgcalls/devices/media_device.hpp>
#include <ntgcalls/devices/desktop_capturer_module.hpp>
#include <ntgcalls/exceptions.hpp>
#include <ntgcalls/devices/camera_capturer_module.hpp>

#ifdef IS_LINUX
#include <ntgcalls/devices/alsa_device_module.hpp>
#include <ntgcalls/devices/pulse_device_module.hpp>
#elif IS_WINDOWS
#include <ntgcalls/devices/win_core_device_module.hpp>
#elif IS_MACOS
#include <ntgcalls/devices/macos_core_audio_device_module.hpp>
#elif IS_ANDROID
#include <ntgcalls/devices/oboe_device_module.hpp>
#include <ntgcalls/devices/java_video_capturer_module.hpp>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace ntgcalls {
#ifdef IS_MACOS
    namespace {
        std::string CFStringToStd(CFStringRef value) {
            if (!value) {
                return {};
            }
            char buffer[512];
            if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                return buffer;
            }
            return {};
        }

        bool HasScopeChannels(const AudioObjectID deviceId, const AudioObjectPropertyScope scope) {
            AudioObjectPropertyAddress address{
                kAudioDevicePropertyStreamConfiguration,
                scope,
                kAudioObjectPropertyElementMain
            };
            UInt32 size = 0;
            if (AudioObjectGetPropertyDataSize(deviceId, &address, 0, nullptr, &size) != noErr || size == 0) {
                return false;
            }
            auto buffer = std::make_unique<uint8_t[]>(size);
            auto* streamConfig = reinterpret_cast<AudioBufferList*>(buffer.get());
            if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, streamConfig) != noErr) {
                return false;
            }
            UInt32 channels = 0;
            for (UInt32 i = 0; i < streamConfig->mNumberBuffers; ++i) {
                channels += streamConfig->mBuffers[i].mNumberChannels;
            }
            return channels > 0;
        }
    } // namespace
#endif

    std::vector<DeviceInfo> MediaDevice::GetAudioDevices() {
#ifdef IS_LINUX
        if (PulseDeviceModule::isSupported()) {
            return PulseDeviceModule::getDevices();
        }
        if (AlsaDeviceModule::isSupported()) {
            return AlsaDeviceModule::getDevices();
        }
#elif IS_WINDOWS
        if (WinCoreDeviceModule::isSupported()) {
            return WinCoreDeviceModule::getDevices();
        }
#elif IS_ANDROID
        auto appendDevices = [](std::vector<DeviceInfo>& devices, const std::string& name, const bool& isCapture) {
            const json data = {
                {"is_microphone", isCapture},
            };
            devices.emplace_back(name, data.dump());
        };
        std::vector<DeviceInfo> devices;
        appendDevices(devices, "default", true);
        appendDevices(devices, "default", false);
        return devices;
#elif IS_MACOS
        std::vector<DeviceInfo> devices;
        AudioObjectPropertyAddress allDevicesAddress{
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &allDevicesAddress, 0, nullptr, &size) != noErr || size == 0) {
            return {};
        }

        const auto deviceCount = static_cast<size_t>(size / sizeof(AudioObjectID));
        auto ids = std::make_unique<AudioObjectID[]>(deviceCount);
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &allDevicesAddress, 0, nullptr, &size, ids.get()) != noErr) {
            return {};
        }

        for (size_t i = 0; i < deviceCount; ++i) {
            const auto id = ids[i];
            AudioObjectPropertyAddress nameAddress{
                kAudioObjectPropertyName,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            AudioObjectPropertyAddress uidAddress{
                kAudioDevicePropertyDeviceUID,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };

            CFStringRef nameRef = nullptr;
            CFStringRef uidRef = nullptr;
            UInt32 nameSize = sizeof(nameRef);
            UInt32 uidSize = sizeof(uidRef);
            if (AudioObjectGetPropertyData(id, &nameAddress, 0, nullptr, &nameSize, &nameRef) != noErr ||
                AudioObjectGetPropertyData(id, &uidAddress, 0, nullptr, &uidSize, &uidRef) != noErr) {
                if (nameRef) CFRelease(nameRef);
                if (uidRef) CFRelease(uidRef);
                continue;
            }

            const auto name = CFStringToStd(nameRef);
            const auto uid = CFStringToStd(uidRef);
            CFRelease(nameRef);
            CFRelease(uidRef);
            if (uid.empty()) {
                continue;
            }

            if (HasScopeChannels(id, kAudioObjectPropertyScopeInput)) {
                const json metadata = {
                    {"is_microphone", true},
                    {"id", uid}
                };
                devices.emplace_back(name.empty() ? "Microphone" : name, metadata.dump());
            }
            if (HasScopeChannels(id, kAudioObjectPropertyScopeOutput)) {
                const json metadata = {
                    {"is_microphone", false},
                    {"id", uid}
                };
                devices.emplace_back(name.empty() ? "Speaker" : name, metadata.dump());
            }
        }
        return devices;
#endif
        return {};
    }

    std::vector<DeviceInfo> MediaDevice::GetScreenDevices() {
#if !defined(IS_ANDROID)
        if (DesktopCapturerModule::IsSupported()) {
            return DesktopCapturerModule::GetSources();
        }
#elif IS_ANDROID
        if (JavaVideoCapturerModule::IsSupported(true)) {
            const json metadata = {
                {"id", "screen"},
            };
            return {DeviceInfo("Device Screen", metadata.dump())};
        }
#endif
        return {};
    }

    std::vector<DeviceInfo> MediaDevice::GetCameraDevices() {
#if !defined(IS_ANDROID)
        return CameraCapturerModule::GetSources();
#elif IS_ANDROID
        if (JavaVideoCapturerModule::IsSupported(false)) {
            return JavaVideoCapturerModule::getDevices();
        }
        return {};
#else
        return {};
#endif
    }

    std::unique_ptr<BaseReader> MediaDevice::CreateDesktopCapture(const VideoDescription& desc, BaseSink* sink) {
#if !defined(IS_ANDROID)
        if (DesktopCapturerModule::IsSupported()) {
            RTC_LOG(LS_INFO) << "Using DesktopCapturer module for input";
            return std::make_unique<DesktopCapturerModule>(desc, sink);
        }
#elif IS_ANDROID
        if (JavaVideoCapturerModule::IsSupported(true)) {
            RTC_LOG(LS_INFO) << "Using AndroidVideoCapturer module for input";
            return std::make_unique<JavaVideoCapturerModule>(true, desc, sink);
        }
#endif
        throw MediaDeviceError("Unsupported platform for desktop capture");
    }

    std::unique_ptr<BaseReader> MediaDevice::CreateCameraCapture(const VideoDescription& desc, BaseSink* sink) {
#if !defined(IS_ANDROID)
        RTC_LOG(LS_INFO) << "Using CameraCapturer module for input";
        return std::make_unique<CameraCapturerModule>(desc, sink);
#elif IS_ANDROID
        if (JavaVideoCapturerModule::IsSupported(false)) {
            RTC_LOG(LS_INFO) << "Using AndroidVideoCapturer module for input";
            return std::make_unique<JavaVideoCapturerModule>(false, desc, sink);
        }
        throw MediaDeviceError("Unsupported platform for camera capture");
#else
        throw MediaDeviceError("Unsupported platform for camera capture");
#endif
    }

    std::unique_ptr<BaseIO> MediaDevice::CreateAudioDevice(const AudioDescription* desc, BaseSink *sink, const bool isCapture) {
#ifdef IS_LINUX
        if (PulseDeviceModule::isSupported()) {
            RTC_LOG(LS_INFO) << "Using PulseAudio module for input";
            return std::make_unique<PulseDeviceModule>(desc, isCapture, sink);
        }
        if (AlsaDeviceModule::isSupported()) {
            RTC_LOG(LS_INFO) << "Using ALSA module for input";
            return std::make_unique<AlsaDeviceModule>(desc, isCapture, sink);
        }
#elif IS_WINDOWS
        if (WinCoreDeviceModule::isSupported()) {
            RTC_LOG(LS_INFO) << "Using Windows Core Audio module for input";
            return std::make_unique<WinCoreDeviceModule>(desc, isCapture, sink);
        }
#elif IS_ANDROID
        RTC_LOG(LS_INFO) << "Using Oboe module for input";
        return std::make_unique<OboeDeviceModule>(desc, isCapture, sink);
#elif IS_MACOS
        if (MacOSCoreAudioDeviceModule::isSupported()) {
            RTC_LOG(LS_INFO) << "Using macOS CoreAudio module for input";
            return std::make_unique<MacOSCoreAudioDeviceModule>(desc, isCapture, sink);
        }
#endif
        throw MediaDeviceError("Unsupported platform for audio device");
    }
} // ntgcalls
