//
// Created by Laky64 on 12/08/2023.
//

#include <ntgcalls/media/video_streamer.hpp>

namespace ntgcalls {
    VideoStreamer::VideoStreamer() {
        video = std::make_unique<wrtc::RTCVideoSource>();
    }

    VideoStreamer::~VideoStreamer() {
        video = nullptr;
    }

    webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> VideoStreamer::createTrack() {
        return video->createTrack();
    }

    void VideoStreamer::sendData(uint8_t* sample, const size_t size, wrtc::FrameData additionalData) {
        frames++;
        if (additionalData.width == 0) {
            additionalData.width = description->width;
        }
        if (additionalData.height == 0) {
            additionalData.height = description->height;
        }
        if (additionalData.width == 0 || additionalData.height == 0 || size == 0) {
            return;
        }
        video->OnFrame(
            wrtc::i420ImageData(
                additionalData.width,
                additionalData.height,
                sample,
                size
            ),
            additionalData
        );
    }

    void VideoStreamer::sendDataNV12(const uint8_t* yPlane, const size_t ySize, const int yStride,
                                     const uint8_t* uvPlane, const size_t uvSize, const int uvStride,
                                     wrtc::FrameData additionalData) {
        frames++;
        if (additionalData.width == 0) {
            additionalData.width = description->width;
        }
        if (additionalData.height == 0) {
            additionalData.height = description->height;
        }
        if (additionalData.width == 0 || additionalData.height == 0 || ySize == 0 || uvSize == 0) {
            return;
        }
        video->OnFrameNV12(
            wrtc::nv12ImageData(
                additionalData.width,
                additionalData.height,
                yPlane,
                ySize,
                yStride,
                uvPlane,
                uvSize,
                uvStride
            ),
            additionalData
        );
    }
}

