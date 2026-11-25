#include "ui.hpp"

#include "gfx.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace ui {

namespace {

constexpr u64 DirectionKeys = HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
constexpr int RepeatDelay = 18;
constexpr int RepeatEvery = 4;
constexpr int TapSlop = 24;
constexpr u64 FrameNanoseconds = 1'000'000'000ULL / 60;
constexpr int DialogWidth = 820;
constexpr int ChoiceRowHeight = 56;
constexpr int VisibleChoices = 7;

struct HintArea {
    int left;
    int right;
    u64 button;
};

std::vector<HintArea> hintAreas;

const char* glyphFor(u64 button) {
    switch (button) {
    case HidNpadButton_A: return "";
    case HidNpadButton_B: return "";
    case HidNpadButton_X: return "";
    case HidNpadButton_Y: return "";
    case HidNpadButton_L: return "";
    case HidNpadButton_R: return "";
    case HidNpadButton_Plus: return "";
    case HidNpadButton_Minus: return "";
    }
    return "";
}

void drawHints(const std::vector<Hint>& hints) {
    hintAreas.clear();
    int x = gfx::Width - theme::Margin;
    int textY = theme::FooterTop + (gfx::Height - theme::FooterTop - theme::BodySize) / 2;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        std::string glyph = glyphFor(it->button);
        int width = gfx::textWidth(glyph, theme::HeadingSize) + 10 + gfx::textWidth(it->label, theme::BodySize);
        int left = x - width;
        int glyphWidth = gfx::drawText(left, textY - 3, glyph, theme::HeadingSize, theme::Text);
        gfx::drawText(left + glyphWidth + 10, textY, it->label, theme::BodySize, theme::Text);
        hintAreas.push_back({left - 12, x + 12, it->button});
        x = left - 40;
    }
}

void dialogFrame(const std::string& title, int height, const std::vector<Hint>& hints) {
    gfx::begin();
    gfx::clear(theme::Background);
    gfx::fillRect(0, 0, gfx::Width, gfx::Height, theme::Shade);
    gfx::fillRect(0, theme::FooterTop, gfx::Width, 1, theme::Divider);
    drawHints(hints);
    int x = (gfx::Width - DialogWidth) / 2;
    int y = std::max(theme::Margin, (theme::FooterTop - height) / 2);
    gfx::roundRect(x, y, DialogWidth, height, theme::Radius * 2, theme::Surface);
    gfx::drawText(x + 40, y + 32, gfx::ellipsize(title, theme::HeadingSize, DialogWidth - 80), theme::HeadingSize, theme::Text);
}

std::vector<std::string> wrapAll(const std::vector<std::string>& lines, int width) {
    std::vector<std::string> wrapped;
    for (const std::string& line : lines) {
        if (line.empty()) {
            wrapped.push_back("");
            continue;
        }
        for (std::string& piece : gfx::wrap(line, theme::BodySize, width, 9))
            wrapped.push_back(std::move(piece));
    }
    return wrapped;
}

void drawDialog(const std::string& title, const std::vector<std::string>& lines, const std::vector<Hint>& hints, Tone tone) {
    std::vector<std::string> wrapped = wrapAll(lines, DialogWidth - 80);
    int lineHeight = theme::BodySize + 12;
    int height = 120 + static_cast<int>(wrapped.size()) * lineHeight;
    dialogFrame(title, height, hints);
    int x = (gfx::Width - DialogWidth) / 2;
    int y = std::max(theme::Margin, (theme::FooterTop - height) / 2) + 92;
    for (const std::string& line : wrapped) {
        gfx::drawText(x + 40, y, line, theme::BodySize, toneColor(tone));
        y += lineHeight;
    }
    gfx::end();
}

void waitRelease(Input& input) {
    while (input.update() && (input.pressed() || input.tap()))
        ;
}

}

Input::Input() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
}

bool Input::update(bool pace) {
    if (pace)
        svcSleepThread(FrameNanoseconds);
    if (!appletMainLoop())
        return false;
    padUpdate(&pad);
    down = padGetButtonsDown(&pad);
    u64 held = padGetButtons(&pad) & DirectionKeys;
    repeated = 0;
    if (held && !(down & DirectionKeys)) {
        heldFrames++;
        if (heldFrames > RepeatDelay && (heldFrames - RepeatDelay) % RepeatEvery == 0)
            repeated = held;
    } else {
        heldFrames = 0;
    }

    tapped.reset();
    HidTouchScreenState touch {};
    bool pressing = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;
    if (pressing) {
        Point at {static_cast<int>(touch.touches[0].x), static_cast<int>(touch.touches[0].y)};
        if (!touching)
            touchStart = at;
        touchLast = at;
    } else if (touching && std::abs(touchLast.x - touchStart.x) < TapSlop && std::abs(touchLast.y - touchStart.y) < TapSlop) {
        tapped = touchLast;
        if (touchLast.y >= theme::FooterTop) {
            for (const HintArea& area : hintAreas)
                if (touchLast.x >= area.left && touchLast.x <= area.right)
                    down |= area.button;
            tapped.reset();
        }
    }
    touching = pressing;
    return true;
}

bool ListCursor::move(u64 keys, int count, int columns, int visibleRows) {
    if (count <= 0)
        return false;
    int before = index;
    if (columns == 1) {
        if (keys & HidNpadButton_AnyUp)
            index = index > 0 ? index - 1 : count - 1;
        else if (keys & HidNpadButton_AnyDown)
            index = index < count - 1 ? index + 1 : 0;
        else if (keys & HidNpadButton_AnyLeft)
            index -= visibleRows;
        else if (keys & HidNpadButton_AnyRight)
            index += visibleRows;
    } else {
        if (keys & HidNpadButton_AnyUp)
            index -= columns;
        else if (keys & HidNpadButton_AnyDown)
            index = index + columns < count ? index + columns : (index / columns < (count - 1) / columns ? count - 1 : index);
        else if (keys & HidNpadButton_AnyLeft)
            index--;
        else if (keys & HidNpadButton_AnyRight)
            index++;
    }
    clamp(count, columns, visibleRows);
    return index != before;
}

void ListCursor::clamp(int count, int columns, int visibleRows) {
    index = std::clamp(index, 0, std::max(0, count - 1));
    int row = index / columns;
    int topRow = top / columns;
    if (row < topRow)
        topRow = row;
    if (row >= topRow + visibleRows)
        topRow = row - visibleRows + 1;
    top = topRow * columns;
}

uint32_t toneColor(Tone tone) {
    switch (tone) {
    case Tone::Dim: return theme::TextDim;
    case Tone::Accent: return theme::Accent;
    case Tone::Good: return theme::Good;
    case Tone::Warn: return theme::Warn;
    case Tone::Bad: return theme::Bad;
    case Tone::Normal: break;
    }
    return theme::Text;
}

void beginFrame(const std::string& title, const std::string& corner, const std::vector<Hint>& hints) {
    gfx::begin();
    gfx::clear(theme::Background);
    int cornerWidth = corner.empty() ? 0 : gfx::textWidth(corner, theme::BodySize);
    gfx::drawText(theme::Margin, 26, gfx::ellipsize(title, theme::TitleSize, gfx::Width - 2 * theme::Margin - cornerWidth - 40), theme::TitleSize, theme::Text);
    if (!corner.empty())
        gfx::drawText(gfx::Width - theme::Margin - cornerWidth, 34, corner, theme::BodySize, theme::TextDim);
    gfx::fillRect(theme::Margin / 2, theme::HeaderHeight, gfx::Width - theme::Margin, 1, theme::Divider);
    gfx::fillRect(theme::Margin / 2, theme::FooterTop, gfx::Width - theme::Margin, 1, theme::Divider);
    drawHints(hints);
}

void endFrame() {
    gfx::end();
}

void status(const std::string& text, Tone tone) {
    gfx::drawText(theme::Margin, theme::StatusTop, gfx::ellipsize(text, theme::SmallSize, gfx::Width - 2 * theme::Margin), theme::SmallSize, toneColor(tone));
}

void emptyMessage(const std::string& text) {
    int width = gfx::textWidth(text, theme::BodySize);
    gfx::drawText((gfx::Width - width) / 2, (theme::HeaderHeight + theme::FooterTop) / 2 - theme::BodySize, text, theme::BodySize, theme::TextDim);
}

void scrollBar(int top, int visible, int count, int x, int y, int height) {
    if (count <= visible)
        return;
    gfx::roundRect(x, y, 6, height, 3, theme::Surface);
    int thumb = std::max(40, height * visible / count);
    int offset = (height - thumb) * top / std::max(1, count - visible);
    gfx::roundRect(x, y + offset, 6, thumb, 3, theme::TextDim);
}

void notice(Input& input, const std::string& title, const std::vector<std::string>& lines) {
    waitRelease(input);
    drawDialog(title, lines, {{HidNpadButton_A, "OK"}}, Tone::Normal);
    while (input.update())
        if (input.pressed() & (HidNpadButton_A | HidNpadButton_B) || input.tap())
            return;
}

bool confirm(Input& input, const std::string& title, const std::vector<std::string>& lines, Tone tone) {
    waitRelease(input);
    drawDialog(title, lines, {{HidNpadButton_B, "Cancel"}, {HidNpadButton_A, "Continue"}}, tone);
    while (input.update()) {
        if (input.pressed() & HidNpadButton_A)
            return true;
        if (input.pressed() & HidNpadButton_B)
            return false;
    }
    return false;
}

std::optional<int> choose(Input& input, const std::string& title, const std::vector<std::string>& options) {
    waitRelease(input);
    int count = static_cast<int>(options.size());
    int shown = std::min(count, VisibleChoices);
    int height = 120 + shown * ChoiceRowHeight;
    ListCursor cursor;
    bool dirty = true;
    while (input.update()) {
        int x = (gfx::Width - DialogWidth) / 2;
        int listTop = std::max(theme::Margin, (theme::FooterTop - height) / 2) + 92;
        if (input.pressed() & HidNpadButton_A)
            return cursor.index;
        if (input.pressed() & HidNpadButton_B)
            return std::nullopt;
        if (auto at = input.tap()) {
            int row = (at->y - listTop) / ChoiceRowHeight;
            if (at->x >= x && at->x < x + DialogWidth && at->y >= listTop && row < shown && cursor.top + row < count) {
                if (cursor.top + row == cursor.index)
                    return cursor.index;
                cursor.index = cursor.top + row;
                dirty = true;
            }
        }
        dirty |= cursor.move(input.navigation(), count, 1, VisibleChoices);
        if (!dirty)
            continue;
        dialogFrame(title, height, {{HidNpadButton_B, "Cancel"}, {HidNpadButton_A, "Pick"}});
        for (int row = 0; row < shown; row++) {
            int item = cursor.top + row;
            int y = listTop + row * ChoiceRowHeight;
            bool selected = item == cursor.index;
            if (selected) {
                gfx::roundRect(x + 24, y - 4, DialogWidth - 48, ChoiceRowHeight - 4, theme::Radius, theme::Accent);
                gfx::roundRect(x + 28, y, DialogWidth - 56, ChoiceRowHeight - 12, theme::Radius - 3, theme::SurfaceRaised);
            }
            gfx::drawText(x + 44, y + 9, gfx::ellipsize(options[item], theme::BodySize, DialogWidth - 100), theme::BodySize, theme::Text);
        }
        scrollBar(cursor.top, VisibleChoices, count, x + DialogWidth - 18, listTop, shown * ChoiceRowHeight);
        gfx::end();
        dirty = false;
    }
    return std::nullopt;
}

void busy(const std::string& title, const std::string& detail) {
    drawDialog(title, {detail}, {}, Tone::Dim);
}

void progress(const std::string& title, const std::string& detail, int64_t done, int64_t total, const std::string& cancelHint) {
    constexpr int BarWidth = DialogWidth - 80;
    int height = 230;
    std::vector<Hint> hints;
    if (!cancelHint.empty())
        hints.push_back({HidNpadButton_B, cancelHint});
    dialogFrame(title, height, hints);
    int x = (gfx::Width - DialogWidth) / 2 + 40;
    int y = std::max(theme::Margin, (theme::FooterTop - height) / 2) + 92;
    gfx::drawText(x, y, gfx::ellipsize(detail, theme::BodySize, BarWidth), theme::BodySize, theme::TextDim);
    gfx::roundRect(x, y + 48, BarWidth, 14, 7, theme::SurfaceRaised);
    if (total > 0) {
        int filled = static_cast<int>(std::min<int64_t>(BarWidth, done * BarWidth / total));
        if (filled > 0)
            gfx::roundRect(x, y + 48, std::max(filled, 14), 14, 7, theme::Accent);
        std::string amount = formatSize(done) + " / " + formatSize(total);
        gfx::drawText(x + BarWidth - gfx::textWidth(amount, theme::SmallSize), y + 76, amount, theme::SmallSize, theme::TextDim);
    }
    gfx::end();
}

std::optional<std::string> keyboard(const std::string& guide, const std::string& initial) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return std::nullopt;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide.c_str());
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    char entered[256] = {};
    Result rc = swkbdShow(&kbd, entered, sizeof(entered));
    swkbdClose(&kbd);
    if (R_FAILED(rc))
        return std::nullopt;
    return std::string(entered);
}

std::string formatSize(int64_t bytes) {
    char buffer[32];
    if (bytes >= 1024LL * 1024 * 1024)
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024));
    else
        std::snprintf(buffer, sizeof(buffer), "%lld KB", static_cast<long long>((bytes + 1023) / 1024));
    return buffer;
}

std::string formatDate(int64_t timestamp) {
    if (timestamp <= 0)
        return "unknown date";
    time_t seconds = static_cast<time_t>(timestamp);
    char buffer[24];
    std::strftime(buffer, sizeof(buffer), "%b %d, %Y", std::gmtime(&seconds));
    return buffer;
}

}
