#pragma once

#include <cstddef>
#include <cstdint>

#include <api/scoped_refptr.h>
#include <api/video/nv12_buffer.h>

namespace wrtc {
    class nv12ImageData {
        uint16_t width, height;
        int yStride, uvStride;
        const uint8_t* yPlane;
        size_t ySize;
        const uint8_t* uvPlane;
        size_t uvSize;

    public:
        nv12ImageData(uint16_t width, uint16_t height,
                      const uint8_t* yPlane, size_t ySize, int yStride,
                      const uint8_t* uvPlane, size_t uvSize, int uvStride);

        [[nodiscard]] webrtc::scoped_refptr<webrtc::NV12Buffer> buffer() const;
    };
} // namespace wrtc
