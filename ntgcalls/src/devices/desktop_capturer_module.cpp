//
// Created by Laky64 on 15/10/24.
//

#if !defined(IS_ANDROID)
#include <set>
#include <utility>

#include <ntgcalls/exceptions.hpp>
#include <third_party/libyuv/include/libyuv.h>
#include <ntgcalls/utils/g_lib_loop_manager.hpp>
#include <ntgcalls/devices/desktop_capturer_module.hpp>
#include <modules/desktop_capture/desktop_capturer_differ_wrapper.h>

namespace ntgcalls {
    namespace {
        using SourceKey = std::pair<std::string, webrtc::DesktopCapturer::SourceId>;

        void AppendSourcesFromCapturer(
            std::vector<DeviceInfo>& devices,
            std::set<SourceKey>& seen,
            const std::unique_ptr<webrtc::DesktopCapturer>& capturer,
            const std::string& sourceType
        ) {
            if (!capturer) {
                return;
            }
            webrtc::DesktopCapturer::SourceList sources;
            if (!capturer->GetSourceList(&sources)) {
                return;
            }
            for (const auto& [id, title, display_id] : sources) {
                if (!seen.emplace(sourceType, id).second) {
                    continue;
                }
                const json metadata{
                    {"id", id},
                    {"display_id", display_id},
                    {"source_type", sourceType}
                };
                devices.emplace_back(title.empty() ? "Screen" : title, metadata.dump());
            }
        }
    } // namespace

    DesktopCapturerModule::DesktopCapturerModule(const VideoDescription& desc, BaseSink* sink): BaseIO(sink), BaseReader(sink), SyncHelper(sink->frameTime()), desc(desc) {
        std::string sourceType = "auto";
        webrtc::DesktopCapturer::SourceId sourceId = 0;
        try {
            auto sourceMetadata = json::parse(desc.input);
            sourceId = sourceMetadata["id"].get<webrtc::DesktopCapturer::SourceId>();
            if (sourceMetadata.contains("source_type")) {
                sourceType = sourceMetadata["source_type"].get<std::string>();
            }
        } catch (...) {
            throw MediaDeviceError("Invalid device metadata");
        }
        capturer = CreateCapturer(sourceType);
        if (!capturer) {
            throw MediaDeviceError("Failed to create desktop capturer");
        }
        if (!capturer->SelectSource(sourceId)) {
            throw MediaDeviceError("Failed to select desktop source");
        }
        capturer->SetMaxFrameRate(desc.fps);
    }

    DesktopCapturerModule::~DesktopCapturerModule() {
        running = false;
        thread.Finalize();
        GLibLoopManager::RemoveInstance();
    }

    webrtc::DesktopCaptureOptions DesktopCapturerModule::BuildCaptureOptions() {
        auto options = webrtc::DesktopCaptureOptions::CreateDefault();
        options.set_detect_updated_region(true);
#ifdef IS_WINDOWS
        options.set_allow_directx_capturer(true);
#elif IS_MACOS
        options.set_allow_iosurface(true);
        options.set_allow_sck_capturer(true);
        options.set_allow_sck_system_picker(false);
#elif IS_LINUX
        options.set_allow_pipewire(true);
#endif
        return options;
    }

    std::unique_ptr<webrtc::DesktopCapturer> DesktopCapturerModule::CreateCapturer(const std::string& sourceType) {
        const auto options = BuildCaptureOptions();
        if (sourceType == "window") {
            return webrtc::DesktopCapturer::CreateWindowCapturer(options);
        }
        if (sourceType == "screen") {
            return webrtc::DesktopCapturer::CreateScreenCapturer(options);
        }
        return webrtc::DesktopCapturer::CreateGenericCapturer(options);
    }

    void DesktopCapturerModule::OnCaptureResult(const webrtc::DesktopCapturer::Result result, const std::unique_ptr<webrtc::DesktopFrame> frame) {
        if (!status) return;
        if (result == webrtc::DesktopCapturer::Result::SUCCESS) {
            const int width = frame->size().width();
            const int height = frame->size().height();

            const auto ySize = width * height;
            const auto uvSize = ySize / 4;
            const auto yPlane = std::make_unique<uint8_t[]>(ySize);
            const auto uPlane = std::make_unique<uint8_t[]>(uvSize);
            const auto vPlane = std::make_unique<uint8_t[]>(uvSize);
            libyuv::ARGBToI420(
                frame->data(), frame->stride(),
                yPlane.get(), width,
                uPlane.get(), width / 2,
                vPlane.get(), width / 2,
                width, height
            );

            const auto yScaledSize = desc.width * desc.height;
            const auto uvScaledSize = yScaledSize / 4;
            auto yuv = bytes::make_unique_binary(yScaledSize + uvScaledSize * 2);

            if (desc.width == width && desc.height == height) {
                memcpy(yuv.get(), yPlane.get(), ySize);
                memcpy(yuv.get() + ySize, uPlane.get(), uvSize);
                memcpy(yuv.get() + ySize + uvSize, vPlane.get(), uvSize);
            } else {
                const auto yScaledPlane = std::make_unique<uint8_t[]>(yScaledSize);
                const auto uScaledPlane = std::make_unique<uint8_t[]>(uvScaledSize);
                const auto vScaledPlane = std::make_unique<uint8_t[]>(uvScaledSize);

                I420Scale(
                    yPlane.get(), width,
                    uPlane.get(), width / 2,
                    vPlane.get(), width / 2,
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
            }

            (void) dataCallback(std::move(yuv), {
                0,
                webrtc::kVideoRotation_0,
                static_cast<uint16_t>(desc.width),
                static_cast<uint16_t>(desc.height),
            });
        } else if (result == webrtc::DesktopCapturer::Result::ERROR_PERMANENT) {
            (void) eofCallback();
        }
    }


    std::vector<DeviceInfo> DesktopCapturerModule::GetSources() {
        std::vector<DeviceInfo> devices;
        std::set<SourceKey> seen;
        auto windowCapturer = CreateCapturer("window");
        auto screenCapturer = CreateCapturer("screen");
        auto genericCapturer = CreateCapturer("auto");

        AppendSourcesFromCapturer(devices, seen, windowCapturer, "window");
        AppendSourcesFromCapturer(devices, seen, screenCapturer, "screen");
        AppendSourcesFromCapturer(devices, seen, genericCapturer, "auto");
        return devices;
    }

    void DesktopCapturerModule::open() {
        if (running) return;
        running = true;
        GLibLoopManager::AddInstance();
        capturer->Start(this);
        capturer->CaptureFrame();
        thread = webrtc::PlatformThread::SpawnJoinable(
            [this] {
                while (running) {
                    waitNextFrame();
                    capturer->CaptureFrame();
                }
            },
            "DesktopCapturerModule",
            webrtc::ThreadAttributes().SetPriority(webrtc::ThreadPriority::kRealtime)
        );
    }

    bool DesktopCapturerModule::IsSupported() {
        return CreateCapturer("auto") != nullptr;
    }
} // ntgcalls

#endif
