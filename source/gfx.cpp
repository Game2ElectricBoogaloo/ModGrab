#include "gfx.hpp"

#include <switch.h>

#include <stb/stb_image.h>
#include <stb/stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace gfx {

namespace {

struct Glyph {
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    int advance = 0;
    std::vector<uint8_t> coverage;
};

constexpr PlSharedFontType FontOrder[] = {PlSharedFontType_Standard, PlSharedFontType_NintendoExt, PlSharedFontType_ChineseSimplified, PlSharedFontType_KO};

Framebuffer framebuffer;
uint32_t* target = nullptr;
int pitch = 0;
std::vector<stbtt_fontinfo> fonts;
std::unordered_map<uint64_t, Glyph> glyphCache;

void blend(uint32_t& dst, uint32_t color, unsigned coverage) {
    unsigned alpha = ((color >> 24) * coverage + 127) / 255;
    if (alpha == 0)
        return;
    if (alpha >= 255) {
        dst = color | 0xFF000000u;
        return;
    }
    unsigned inverse = 255 - alpha;
    uint32_t r = ((color & 0xFF) * alpha + (dst & 0xFF) * inverse) / 255;
    uint32_t g = (((color >> 8) & 0xFF) * alpha + ((dst >> 8) & 0xFF) * inverse) / 255;
    uint32_t b = (((color >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * inverse) / 255;
    dst = r | g << 8 | b << 16 | 0xFF000000u;
}

unsigned cornerCoverage(int px, int py, int x, int y, int w, int h, int radius) {
    if (radius <= 0)
        return 255;
    float cx;
    float cy;
    if (px < x + radius)
        cx = x + radius - 0.5f;
    else if (px >= x + w - radius)
        cx = x + w - radius - 0.5f;
    else
        return 255;
    if (py < y + radius)
        cy = y + radius - 0.5f;
    else if (py >= y + h - radius)
        cy = y + h - radius - 0.5f;
    else
        return 255;
    float distance = std::hypot(px - cx, py - cy);
    float edge = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
    return static_cast<unsigned>(edge * 255.0f);
}

uint32_t nextCodepoint(const std::string& text, size_t& pos) {
    unsigned char lead = text[pos++];
    if (lead < 0x80)
        return lead;
    int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    uint32_t codepoint = lead & (0x3F >> extra);
    for (int i = 0; i < extra && pos < text.size(); i++)
        codepoint = codepoint << 6 | (static_cast<unsigned char>(text[pos++]) & 0x3F);
    return extra ? codepoint : '?';
}

const Glyph& glyphFor(uint32_t codepoint, int size) {
    uint64_t key = static_cast<uint64_t>(size) << 32 | codepoint;
    auto cached = glyphCache.find(key);
    if (cached != glyphCache.end())
        return cached->second;

    Glyph glyph;
    for (stbtt_fontinfo& font : fonts) {
        int index = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
        if (!index)
            continue;
        float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(size));
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&font, index, &advance, &bearing);
        glyph.advance = static_cast<int>(std::lround(advance * scale));
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&font, index, scale, scale, &x0, &y0, &x1, &y1);
        glyph.width = x1 - x0;
        glyph.height = y1 - y0;
        glyph.offsetX = x0;
        glyph.offsetY = y0;
        if (glyph.width > 0 && glyph.height > 0) {
            glyph.coverage.resize(static_cast<size_t>(glyph.width) * glyph.height);
            stbtt_MakeGlyphBitmap(&font, glyph.coverage.data(), glyph.width, glyph.height, glyph.width, scale, scale, index);
        }
        break;
    }
    if (!glyph.advance && codepoint != '?')
        return glyphCache.emplace(key, glyphFor('?', size)).first->second;
    return glyphCache.emplace(key, std::move(glyph)).first->second;
}

int ascentFor(int size) {
    if (fonts.empty())
        return size;
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&fonts.front(), &ascent, &descent, &gap);
    return static_cast<int>(std::lround(ascent * stbtt_ScaleForPixelHeight(&fonts.front(), static_cast<float>(size))));
}

}

std::string init() {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc))
        return "couldn't load the system font (pl error " + std::to_string(rc) + ")";
    for (PlSharedFontType type : FontOrder) {
        PlFontData data;
        if (R_FAILED(plGetSharedFontByType(&data, type)))
            continue;
        stbtt_fontinfo font;
        const unsigned char* bytes = static_cast<const unsigned char*>(data.address);
        if (stbtt_InitFont(&font, bytes, stbtt_GetFontOffsetForIndex(bytes, 0)))
            fonts.push_back(font);
    }
    if (fonts.empty())
        return "the system font couldn't be read";

    rc = framebufferCreate(&framebuffer, nwindowGetDefault(), Width, Height, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc))
        return "couldn't create the framebuffer";
    framebufferMakeLinear(&framebuffer);
    return {};
}

void shutdown() {
    framebufferClose(&framebuffer);
    glyphCache.clear();
    fonts.clear();
    plExit();
}

void begin() {
    u32 stride = 0;
    target = static_cast<uint32_t*>(framebufferBegin(&framebuffer, &stride));
    pitch = static_cast<int>(stride / sizeof(uint32_t));
}

void end() {
    framebufferEnd(&framebuffer);
    target = nullptr;
}

void clear(uint32_t color) {
    for (int y = 0; y < Height; y++)
        std::fill_n(target + y * pitch, Width, color | 0xFF000000u);
}

void fillRect(int x, int y, int w, int h, uint32_t color) {
    int left = std::max(x, 0);
    int right = std::min(x + w, Width);
    int top = std::max(y, 0);
    int bottom = std::min(y + h, Height);
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++)
            blend(target[py * pitch + px], color, 255);
}

void roundRect(int x, int y, int w, int h, int radius, uint32_t color) {
    radius = std::min({radius, w / 2, h / 2});
    int left = std::max(x, 0);
    int right = std::min(x + w, Width);
    int top = std::max(y, 0);
    int bottom = std::min(y + h, Height);
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++)
            blend(target[py * pitch + px], color, cornerCoverage(px, py, x, y, w, h, radius));
}

void drawImage(const Image& image, int x, int y, int radius) {
    radius = std::min({radius, image.width / 2, image.height / 2});
    for (int iy = 0; iy < image.height; iy++) {
        int py = y + iy;
        if (py < 0 || py >= Height)
            continue;
        for (int ix = 0; ix < image.width; ix++) {
            int px = x + ix;
            if (px < 0 || px >= Width)
                continue;
            blend(target[py * pitch + px], image.pixels[iy * image.width + ix], cornerCoverage(px, py, x, y, image.width, image.height, radius));
        }
    }
}

int drawText(int x, int y, const std::string& text, int size, uint32_t color) {
    int baseline = y + ascentFor(size);
    int penX = x;
    size_t pos = 0;
    while (pos < text.size()) {
        const Glyph& glyph = glyphFor(nextCodepoint(text, pos), size);
        for (int gy = 0; gy < glyph.height; gy++) {
            int py = baseline + glyph.offsetY + gy;
            if (py < 0 || py >= Height)
                continue;
            for (int gx = 0; gx < glyph.width; gx++) {
                int px = penX + glyph.offsetX + gx;
                if (px < 0 || px >= Width)
                    continue;
                uint8_t coverage = glyph.coverage[gy * glyph.width + gx];
                if (coverage)
                    blend(target[py * pitch + px], color, coverage);
            }
        }
        penX += glyph.advance;
    }
    return penX - x;
}

int textWidth(const std::string& text, int size) {
    int width = 0;
    size_t pos = 0;
    while (pos < text.size())
        width += glyphFor(nextCodepoint(text, pos), size).advance;
    return width;
}

std::string ellipsize(const std::string& text, int size, int maxWidth) {
    if (textWidth(text, size) <= maxWidth)
        return text;
    int budget = maxWidth - textWidth("...", size);
    int width = 0;
    size_t pos = 0;
    size_t cut = 0;
    while (pos < text.size()) {
        width += glyphFor(nextCodepoint(text, pos), size).advance;
        if (width > budget)
            break;
        cut = pos;
    }
    return text.substr(0, cut) + "...";
}

std::vector<std::string> wrap(const std::string& text, int size, int maxWidth, int maxLines) {
    std::vector<std::string> lines;
    std::string line;
    bool truncated = false;
    auto flush = [&]() {
        if (static_cast<int>(lines.size()) < maxLines)
            lines.push_back(line);
        else
            truncated = true;
        line.clear();
    };
    size_t pos = 0;
    while (pos < text.size() && !truncated) {
        size_t start = pos;
        uint32_t codepoint = nextCodepoint(text, pos);
        if (codepoint == '\n') {
            flush();
            continue;
        }
        std::string piece = text.substr(start, pos - start);
        if (textWidth(line + piece, size) <= maxWidth) {
            line += piece;
            continue;
        }
        size_t space = line.rfind(' ');
        std::string carry;
        if (space != std::string::npos && codepoint != ' ') {
            carry = line.substr(space + 1);
            line.erase(space);
        }
        flush();
        line = codepoint == ' ' ? std::string() : carry + piece;
    }
    if (!line.empty() && !truncated)
        flush();
    if (truncated && !lines.empty())
        lines.back() = ellipsize(lines.back() + "...", size, maxWidth);
    return lines;
}

Image decodeImage(const void* bytes, size_t size, int width, int height) {
    Image image;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int channels = 0;
    stbi_uc* source = stbi_load_from_memory(static_cast<const stbi_uc*>(bytes), static_cast<int>(size), &sourceWidth, &sourceHeight, &channels, 4);
    if (!source)
        return image;

    float scale = std::max(static_cast<float>(width) / sourceWidth, static_cast<float>(height) / sourceHeight);
    float cropX = (sourceWidth - width / scale) / 2.0f;
    float cropY = (sourceHeight - height / scale) / 2.0f;
    int footprint = std::max(1, static_cast<int>(1.0f / scale));

    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx = std::clamp(static_cast<int>(cropX + x / scale), 0, sourceWidth - 1);
            int sy = std::clamp(static_cast<int>(cropY + y / scale), 0, sourceHeight - 1);
            unsigned sum[4] = {};
            int count = 0;
            for (int fy = sy; fy < std::min(sy + footprint, sourceHeight); fy++) {
                for (int fx = sx; fx < std::min(sx + footprint, sourceWidth); fx++) {
                    const stbi_uc* pixel = source + (static_cast<size_t>(fy) * sourceWidth + fx) * 4;
                    for (int c = 0; c < 4; c++)
                        sum[c] += pixel[c];
                    count++;
                }
            }
            image.pixels[y * width + x] = rgba(sum[0] / count, sum[1] / count, sum[2] / count, sum[3] / count);
        }
    }
    stbi_image_free(source);
    return image;
}

}
