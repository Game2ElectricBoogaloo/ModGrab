#pragma once

#include <switch.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ui {

enum class Tone { Normal, Dim, Accent, Good, Warn, Bad };

struct Hint {
    u64 button;
    std::string label;
};

struct Point {
    int x = 0;
    int y = 0;
};

class Input {
public:
    Input();
    bool update(bool pace = true);
    u64 pressed() const { return down; }
    u64 navigation() const { return down | repeated; }
    std::optional<Point> tap() const { return tapped; }

private:
    PadState pad{};
    u64 down = 0;
    u64 repeated = 0;
    int heldFrames = 0;
    bool touching = false;
    Point touchStart;
    Point touchLast;
    std::optional<Point> tapped;
};

struct ListCursor {
    int index = 0;
    int top = 0;
    bool move(u64 keys, int count, int columns, int visibleRows);
    void clamp(int count, int columns, int visibleRows);
};

uint32_t toneColor(Tone tone);

void beginFrame(const std::string& title, const std::string& corner, const std::vector<Hint>& hints);
void endFrame();
void status(const std::string& text, Tone tone);
void emptyMessage(const std::string& text);
void scrollBar(int top, int visible, int count, int x, int y, int height);

void notice(Input& input, const std::string& title, const std::vector<std::string>& lines);
bool confirm(Input& input, const std::string& title, const std::vector<std::string>& lines, Tone tone = Tone::Normal);
std::optional<int> choose(Input& input, const std::string& title, const std::vector<std::string>& options);
void busy(const std::string& title, const std::string& detail);
void progress(const std::string& title, const std::string& detail, int64_t done, int64_t total, const std::string& cancelHint);
std::optional<std::string> keyboard(const std::string& guide, const std::string& initial);

std::string formatSize(int64_t bytes);
std::string formatDate(int64_t timestamp);

}
