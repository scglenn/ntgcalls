//
// Created by Laky64 on 17/10/24.
//

#if !defined(IS_ANDROID)
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <cstdio>
#include <unordered_set>
#include <utility>
#include <vector>

#include <libyuv/scale.h>
#include <ntgcalls/devices/camera_capturer_module.hpp>
#include <ntgcalls/exceptions.hpp>
#include <rtc_base/logging.h>

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
        std::string deviceNameHint;
        try {
            const auto sourceMetadata = json::parse(desc.input);
            if (sourceMetadata.contains("id")) {
                deviceId = sourceMetadata["id"].get<std::string>();
            }
            if (sourceMetadata.contains("name")) {
                deviceNameHint = sourceMetadata["name"].get<std::string>();
            }
        } catch (...) {
            // Backward compatibility: allow plain device id or name as input.
            deviceId = desc.input;
            deviceNameHint = desc.input;
        }

        const auto info = CreateDeviceInfo();
        if (!info) {
            std::fprintf(stderr, "[ntgcalls] CameraCapturerModule DeviceInfo unavailable, using direct-id fallback\n");
            std::fflush(stderr);
        }

        const auto count = info ? info->NumberOfDevices() : 0;
        if (count > 0) {
            std::vector<std::pair<std::string, std::string>> devices;
            devices.reserve(count);
            for (int i = 0; i < count; ++i) {
                char id[256] = {0};
                char name[256] = {0};
                if (info->GetDeviceName(i, name, sizeof(name), id, sizeof(id)) == -1) {
                    continue;
                }
                devices.emplace_back(std::string(name), std::string(id));
            }

            auto matchByName = [&](const std::string& wanted) -> std::optional<std::string> {
                if (wanted.empty()) {
                    return std::nullopt;
                }
                for (const auto& [name, id] : devices) {
                    if (name == wanted) {
                        return id;
                    }
                }
                for (const auto& [name, id] : devices) {
                    if (name.find(wanted) != std::string::npos) {
                        return id;
                    }
                }
                return std::nullopt;
            };

            auto hasId = [&](const std::string& candidate) {
                return std::any_of(devices.begin(), devices.end(), [&](const auto& entry) {
                    return entry.second == candidate;
                });
            };

            if (!deviceId.empty() && !hasId(deviceId)) {
                if (const auto byName = matchByName(deviceId)) {
                    deviceId = *byName;
                }
            }
            if (!deviceId.empty() && !hasId(deviceId)) {
                if (const auto byName = matchByName(deviceNameHint)) {
                    deviceId = *byName;
                }
            }
            if (deviceId.empty()) {
                if (const auto byName = matchByName(deviceNameHint)) {
                    deviceId = *byName;
                }
            }
            if (deviceId.empty() && !devices.empty()) {
                deviceId = devices.front().second;
            }
        }

        if (deviceId.empty()) {
            throw MediaDeviceError("No camera device id could be resolved");
        }

        std::vector<std::string> candidates;
        if (!deviceId.empty()) {
            candidates.push_back(deviceId);
        }
        if (count > 0) {
            for (int i = 0; i < count; ++i) {
                char id[256] = {0};
                char name[256] = {0};
                if (info->GetDeviceName(i, name, sizeof(name), id, sizeof(id)) == -1) {
                    continue;
                }
                if (id[0] != '\0') {
                    candidates.emplace_back(id);
                }
            }
        }

        std::unordered_set<std::string> tried;
        for (const auto& candidateId : candidates) {
            if (candidateId.empty() || tried.contains(candidateId)) {
                continue;
            }
            tried.insert(candidateId);
            RTC_LOG(LS_INFO) << "CameraCapturerModule trying capture deviceId=" << candidateId
                             << " requested=" << desc.input;
            std::fprintf(
                stderr,
                "[ntgcalls] CameraCapturerModule trying capture deviceId=%s requested=%s\n",
                candidateId.c_str(),
                desc.input.c_str()
            );
            std::fflush(stderr);
#ifdef IS_LINUX
            auto options = webrtc::VideoCaptureOptions();
            options.set_allow_v4l2(true);
            capturer = webrtc::VideoCaptureFactory::Create(&options, candidateId.c_str());
#else
            capturer = webrtc::VideoCaptureFactory::Create(candidateId.c_str());
#endif
            if (capturer) {
                deviceId = candidateId;
                break;
            }
        }
        if (!capturer) {
            std::fprintf(stderr, "[ntgcalls] CameraCapturerModule failed to create video capturer\n");
            std::fflush(stderr);
            throw MediaDeviceError("Failed to create video capturer");
        }
        capturer->RegisterCaptureDataCallback(this);
        auto requested = webrtc::VideoCaptureCapability();
        requested.videoType = webrtc::VideoType::kI420;
        requested.width = desc.width;
        requested.height = desc.height;
        requested.maxFPS = desc.fps;
        if (info) {
            if (info->GetBestMatchedCapability(capturer->CurrentDeviceName(), requested, capability) != 0) {
                capability = BuildFallbackCapability(info, capturer->CurrentDeviceName(), desc);
            }
            if (!capability.width || !capability.height || !capability.maxFPS) {
                capability = BuildFallbackCapability(info, capturer->CurrentDeviceName(), desc);
            }
        } else {
            capability.width = desc.width;
            capability.height = desc.height;
            capability.maxFPS = desc.fps;
        }
#ifndef IS_WINDOWS
        capability.videoType = webrtc::VideoType::kI420;
#endif
        RTC_LOG(LS_INFO) << "CameraCapturerModule selected capability width=" << capability.width
                         << " height=" << capability.height
                         << " fps=" << capability.maxFPS;
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
        const auto rc = capturer->StartCapture(capability);
        if (rc != 0) {
            destroy();
            RTC_LOG(LS_ERROR) << "CameraCapturerModule StartCapture failed rc=" << rc;
            std::fprintf(stderr, "[ntgcalls] CameraCapturerModule StartCapture failed rc=%d\n", rc);
            std::fflush(stderr);
            throw MediaDeviceError("Failed to start camera capture");
        }
    }
} // ntgcalls

#endif
