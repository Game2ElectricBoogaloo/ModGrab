#include "widgets.hpp"

#include <algorithm>

namespace widgets {

int rowTop(int slot) {
    return theme::ContentTop + slot * RowPitch;
}

int slotAt(const ui::Point& at, int left, int width, int top, int pitch, int slots) {
    if (at.x < left || at.x >= left + width || at.y < top)
        return -1;
    int slot = (at.y - top) / pitch;
    return slot < slots ? slot : -1;
}

void selectionOutline(int x, int y, int w, int h, int radius) {
    gfx::roundRect(x - theme::Outline, y - theme::Outline, w + 2 * theme::Outline, h + 2 * theme::Outline, radius + theme::Outline, theme::Accent);
}

void badge(int right, int y, const std::string& label, ui::Tone tone) {
    int width = gfx::textWidth(label, theme::SmallSize) + 24;
    gfx::roundRect(right - width, y, width, 32, 16, ui::toneColor(tone));
    gfx::drawText(right - width + 12, y + 5, label, theme::SmallSize, theme::Background);
}

void placeholder(int x, int y, int w, int h, const std::string& name) {
    gfx::roundRect(x, y, w, h, theme::Radius, theme::SurfaceRaised);
    std::string initial = name.empty() ? "?" : name.substr(0, 1);
    int size = std::min(w, h) / 2;
    gfx::drawText(x + (w - gfx::textWidth(initial, size)) / 2, y + (h - size) / 2, initial, size, theme::TextDim);
}

void drawRow(int slot, bool selected, const gfx::Image* thumb, const std::string& title, const std::string& detail, const std::string& tag, ui::Tone tagTone) {
    int x = theme::Margin;
    int y = rowTop(slot);
    if (selected)
        selectionOutline(x, y, RowWidth, theme::RowHeight, theme::Radius);
    gfx::roundRect(x, y, RowWidth, theme::RowHeight, theme::Radius, selected ? theme::SurfaceRaised : theme::Surface);
    int thumbY = y + (theme::RowHeight - theme::ThumbHeight) / 2;
    if (thumb)
        gfx::drawImage(*thumb, x + 8, thumbY, 8);
    else
        placeholder(x + 8, thumbY, theme::ThumbWidth, theme::ThumbHeight, title);
    int textLeft = x + theme::ThumbWidth + 28;
    int tagWidth = tag.empty() ? 0 : gfx::textWidth(tag, theme::SmallSize) + 48;
    int textWidth = RowWidth - (textLeft - x) - tagWidth - 16;
    gfx::drawText(textLeft, y + 22, gfx::ellipsize(title, theme::BodySize, textWidth), theme::BodySize, theme::Text);
    gfx::drawText(textLeft, y + 58, gfx::ellipsize(detail, theme::SmallSize, textWidth), theme::SmallSize, theme::TextDim);
    if (!tag.empty())
        badge(x + RowWidth - 16, y + (theme::RowHeight - 32) / 2, tag, tagTone);
}

int tappedRow(ui::Input& input, ui::ListCursor& cursor, int count, u64& keys) {
    auto at = input.tap();
    if (!at)
        return -1;
    int slot = slotAt(*at, theme::Margin, RowWidth, theme::ContentTop, RowPitch, theme::VisibleRows);
    if (slot < 0 || cursor.top + slot >= count)
        return -1;
    if (cursor.top + slot == cursor.index)
        keys |= HidNpadButton_A;
    cursor.index = cursor.top + slot;
    return cursor.index;
}

void rowScrollBar(const ui::ListCursor& cursor, int count) {
    ui::scrollBar(cursor.top, theme::VisibleRows, count, gfx::Width - 24, theme::ContentTop, theme::VisibleRows * RowPitch - theme::RowGap);
}

}
