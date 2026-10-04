// Minimal PNG writer (RGBA 8-bit, uncompressed deflate). Header-only.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

// ---- tiny PNG writer (RGBA, uncompressed deflate) ----
inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
    static uint32_t T[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t v = i; for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; T[i] = v; } init = true; }
    c = ~c; for (size_t i = 0; i < n; ++i) c = T[(c ^ d[i]) & 255] ^ (c >> 8); return ~c;
}
inline void be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
inline void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
}
inline bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;                       // filter byte 0 + row
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4); }
    std::vector<uint8_t> z = {0x78, 0x01};          // zlib header, stored blocks
    for (size_t p = 0; p < raw.size() || p == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - p); const bool last = p + n >= raw.size();
        z.push_back(last ? 1 : 0); z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + p, raw.begin() + p + n); p += n; if (last) break;
    }
    uint32_t a = 1, b = 0; for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}, ihdr;
    be32(ihdr, w); be32(ihdr, h); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", ihdr); chunk(png, "IDAT", z); chunk(png, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    std::fwrite(png.data(), 1, png.size(), f); std::fclose(f); return true;
}

