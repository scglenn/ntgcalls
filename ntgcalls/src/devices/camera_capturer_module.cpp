//
// Created by Laky64 on 17/10/24.
//

#if !defined(IS_ANDROID)
#include <cmath>
#include <limits>

#include <libyuv/scale.h>
#include <ntgcalls/devices/camera_capturer_module.hpp>
#include <ntgcalls/exceptions.hpp>

#ifdef IS_LINUX
#include <modules/video_capture/video_capture_options.h>
#endif

namespace ntgcalls {
    namespace {
        webrtc::VideoCaptureCapability BuildFallbackCapability(
            const std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo>& info,
            const char* deviceName,
            const VideoDescription& desc
        ) {
            auto best = webrtc::VideoCaptureCapability();
            auto bestScore = std::numeric_limits<double>::max();
            const int capabilityCount = info ? info->NumberOfCapabilities(deviceName) : 0;
            for (int i = 0; i < capabilityCount; ++i) {
                webrtc::VideoCaptureCapability candidate;
                if (info->GetCapability(deviceName, i, candidate) != 0) {
                    continue;
                }
                if (candidate.width <= 0 || candidate.height <= 0 || candidate.maxFPS <= 0) {
                    continue;
                }
                const auto widthDiff = static_cast<double>(candidate.width - desc.width);
                const auto heightDiff = static_cast<double>(candidate.height - desc.height);
                const auto fpsDiff = static_cast<double>(candidate.maxFPS - desc.fps);
                const auto score = std::abs(widthDiff) + std::abs(heightDiff) + std::abs(fpsDiff) * 2.0;
                if (score < bestScore) {
                    bestScore = score;
                    best = candidate;
                }
            }
            if (best.width <= 0 || best.height <= 0 || best.maxFPS <= 0) {
                best.width = desc.width;
                best.height = desc.height;
                best.maxFPS = desc.fps;
            }
            return best;
        }
    } // namespace

    CameraCapturerModule::CameraCapturerModule(const VideoDescription& desc, BaseSink* sink): BaseIO(sink), BaseReader(sink), desc(desc) {
        std::string deviceId;
        try {
            auto sourceMetadata = json::parse(desc.input);
            deviceId = sourceMetadata["id"].get<std::string>();
        } catch (...) {
            throw MediaDeviceError("Invalid device metadata");
        }
#ifdef IS_LINUX
        auto options = webrtc::VideoCaptureOptions();
        options.set_allow_v4l2(true);
        capturer = webrtc::VideoCaptureFactory::Create(&options, deviceId.c_str());
#else
        capturer = webrtc::VideoCaptureFactory::Create(deviceId.c_str());
#endif
        if (!capturer) {
            throw MediaDeviceError("Failed to create video capturer");
        }
        capturer->RegisterCaptureDataCallback(this);
        const auto info = CreateDeviceInfo();
        if (!info) {
            throw MediaDeviceError("Failed to create camera device info");
        }
        auto requested = webrtc::VideoCaptureCapability();
        requested.videoType = webrtc::VideoType::kI420;
        requested.width = desc.width;
        requested.height = desc.height;
        requested.maxFPS = desc.fps;
        if (info->GetBestMatchedCapability(capturer->CurrentDeviceName(), requested, capability) != 0) {
            capability = BuildFallbackCapability(info, capturer->CurrentDeviceName(), desc);
        }
        if (!capability.width || !capability.height || !capability.maxFPS) {
            capability = BuildFallbackCapability(info, capturer->CurrentDeviceName(), desc);
        }
#ifndef IS_WINDOWS
        capability.videoType = webrtc::VideoType::kI420;
#endif
    }

    CameraCapturerModule::~CameraCapturerModule() {
        destroy();
    }

    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> CameraCapturerModule::CreateDeviceInfo() {
#ifdef IS_LINUX
        auto options = webrtc::VideoCaptureOptions();
        options.set_allow_v4l2(true);
        return std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo>(webrtc::VideoCaptureFactory::CreateDeviceInfo(&options));
#else
        return std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo>(webrtc::VideoCaptureFactory::CreateDeviceInfo());
#endif
    }

    void CameraCapturerModule::destroy() {
        if (!capturer) {
            return;
        }
        capturer->StopCapture();
        capturer->DeRegisterCaptureDataCallback();
        capturer = nullptr;
    }

    std::vector<DeviceInfo> CameraCapturerModule::GetSources() {
        const auto info = CreateDeviceInfo();
        if (!info) {
            return {};
        }
        const auto count = info->NumberOfDevices();
        if (count <= 0) {
            return {};
        }
        std::vector<DeviceInfo> result;
        for (int i = 0; i < count; i++) {
            char id[256];
            if (char name[256]; info->GetDeviceName(i, name, sizeof(name), id, sizeof(id)) != -1) {
                const json metadata{
                    {"id", id},
                };
                result.emplace_back(name, metadata.dump());
            }
        }
        return result;
    }

    void CameraCapturerModule::OnFrame(const webrtc::VideoFrame& frame) {
        const auto yScaledSize = desc.width * desc.height;
        const auto uvScaledSize = yScaledSize / 4;
        auto yuv = bytes::make_unique_binary(yScaledSize + uvScaledSize * 2);
        const auto buffer = frame.video_frame_buffer()->ToI420();

        const auto width = buffer->width();
        const auto height = buffer->height();
        const auto yScaledPlane = std::make_unique<uint8_t[]>(yScaledSize);
        const auto uScaledPlane = std::make_unique<uint8_t[]>(uvScaledSize);
        const auto vScaledPlane = std::make_unique<uint8_t[]>(uvScaledSize);

        I420Scale(
            buffer->DataY(), buffer->StrideY(),
            buffer->DataU(), buffer->StrideU(),
            buffer->DataV(), buffer->StrideV(),
            width, height,
            yScaledPlane.get(), desc.width,
            uScaledPlane.get(), desc.width / 2,
            vScaledPlane.get(), desc.width / 2,
            desc.width, desc.height,
            libyuv::kFilterBox
        );
        memcpy(yuv.get(), yScaledPlane.get(), yScaledSize);
        memcpy(yuv.get() + yScaledSize, uScaledPlane.get(), uvScaledSize);
        memcpy(yuv.get() + yScaledSize + uvScaledSize, vScaledPlane.get(), uvScaledSize);

        (void) dataCallback(std::move(yuv), {
            0,
            frame.rotation(),
            static_cast<uint16_t>(desc.width),
            static_cast<uint16_t>(desc.height),
        });
    }

    void CameraCapturerModule::open() {
        if (capturer->StartCapture(capability) != 0) {
            destroy();
            throw MediaDeviceError("Failed to start camera capture");
        }
    }
} // ntgcalls

#endif
