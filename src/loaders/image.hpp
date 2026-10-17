// image.hpp - RGBA image + blit, shared by render.hpp (legacy) and render_content.hpp. No ROM knowledge.
// In render.hpp, delete the Image struct and blit() and add:  #include "image.hpp"
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gp {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), rgba(size_t(w_) * h_ * 4, 0) {}
};

inline void blit(Image& dst, const uint8_t* src, int sw, int sh, int x0, int y0) {   // alpha 0/255, no blending
    for (int y = 0; y < sh; ++y) {
        const int dy = y0 + y; if (dy < 0 || dy >= dst.h) continue;
        for (int x = 0; x < sw; ++x) {
            const int dx = x0 + x; if (dx < 0 || dx >= dst.w) continue;
            const uint8_t* s = &src[(size_t(y) * sw + x) * 4];
            if (!s[3]) continue;
            uint8_t* d = &dst.rgba[(size_t(dy) * dst.w + dx) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
}

} // namespace gp