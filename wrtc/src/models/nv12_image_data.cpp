#include <cstring>

#include <wrtc/models/nv12_image_data.hpp>

namespace wrtc {
    nv12ImageData::nv12ImageData(const uint16_t width, const uint16_t height,
                                 const uint8_t* yPlane, const size_t ySize, const int yStride,
                                 const uint8_t* uvPlane, const size_t uvSize, const int uvStride)
        : width(width),
          height(height),
          yStride(yStride),
          uvStride(uvStride),
          yPlane(yPlane),
          ySize(ySize),
          uvPlane(uvPlane),
          uvSize(uvSize) {}

    webrtc::scoped_refptr<webrtc::NV12Buffer> nv12ImageData::buffer() const {
        auto buffer = webrtc::NV12Buffer::Create(width, height, yStride, uvStride);
        const size_t expectedY = static_cast<size_t>(yStride * height);
        const size_t expectedUV = static_cast<size_t>(uvStride * (height / 2));
        if (!yPlane || !uvPlane || ySize < expectedY || uvSize < expectedUV) {
            buffer->InitializeData();
            return buffer;
        }

        for (int row = 0; row < height; ++row) {
            memcpy(buffer->MutableDataY() + (row * yStride), yPlane + (row * yStride), static_cast<size_t>(yStride));
        }
        for (int row = 0; row < (height / 2); ++row) {
            memcpy(buffer->MutableDataUV() + (row * uvStride), uvPlane + (row * uvStride), static_cast<size_t>(uvStride));
        }
        return buffer;
    }
} // namespace wrtc
