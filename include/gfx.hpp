#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gfx {

constexpr int Width = 1280;
constexpr int Height = 720;

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16 | static_cast<uint32_t>(a) << 24;
}

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    bool empty() const { return pixels.empty(); }
};

std::string init();
void shutdown();

void begin();
void end();

void clear(uint32_t color);
void fillRect(int x, int y, int w, int h, uint32_t color);
void roundRect(int x, int y, int w, int h, int radius, uint32_t color);
void drawImage(const Image& image, int x, int y, int radius);

int drawText(int x, int y, const std::string& text, int size, uint32_t color);
int textWidth(const std::string& text, int size);
std::string ellipsize(const std::string& text, int size, int maxWidth);
std::vector<std::string> wrap(const std::string& text, int size, int maxWidth, int maxLines);

Image decodeImage(const void* bytes, size_t size, int width, int height);

}
