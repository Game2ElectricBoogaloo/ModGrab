#pragma once

#include "gfx.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include <string>

namespace widgets {

constexpr int GridLeft = (gfx::Width - theme::TileColumns * theme::TileSize - (theme::TileColumns - 1) * theme::TileGap) / 2;
constexpr int RowWidth = gfx::Width - 2 * theme::Margin - 24;
constexpr int RowPitch = theme::RowHeight + theme::RowGap;

int rowTop(int slot);
int slotAt(const ui::Point& at, int left, int width, int top, int pitch, int slots);
int tappedRow(ui::Input& input, ui::ListCursor& cursor, int count, u64& keys);
void selectionOutline(int x, int y, int w, int h, int radius);
void badge(int right, int y, const std::string& label, ui::Tone tone);
void placeholder(int x, int y, int w, int h, const std::string& name);
void drawRow(int slot, bool selected, const gfx::Image* thumb, const std::string& title, const std::string& detail, const std::string& tag, ui::Tone tagTone);
void rowScrollBar(const ui::ListCursor& cursor, int count);

}
