#pragma once

#include "gfx.hpp"

namespace theme {

inline constexpr uint32_t Background = gfx::rgba(26, 28, 34);
inline constexpr uint32_t Surface = gfx::rgba(38, 41, 50);
inline constexpr uint32_t SurfaceRaised = gfx::rgba(50, 54, 66);
inline constexpr uint32_t Divider = gfx::rgba(64, 68, 80);
inline constexpr uint32_t Accent = gfx::rgba(40, 190, 255);
inline constexpr uint32_t Text = gfx::rgba(236, 239, 245);
inline constexpr uint32_t TextDim = gfx::rgba(150, 156, 172);
inline constexpr uint32_t Good = gfx::rgba(84, 214, 126);
inline constexpr uint32_t Warn = gfx::rgba(255, 192, 64);
inline constexpr uint32_t Bad = gfx::rgba(255, 96, 96);
inline constexpr uint32_t Shade = gfx::rgba(0, 0, 0, 150);

inline constexpr int Margin = 48;
inline constexpr int HeaderHeight = 88;
inline constexpr int FooterTop = 648;
inline constexpr int ContentTop = HeaderHeight + 20;
inline constexpr int StatusTop = FooterTop - 38;
inline constexpr int Radius = 12;
inline constexpr int Outline = 4;

inline constexpr int TitleSize = 34;
inline constexpr int HeadingSize = 28;
inline constexpr int BodySize = 24;
inline constexpr int SmallSize = 20;

inline constexpr int TileSize = 176;
inline constexpr int TileGap = 28;
inline constexpr int TileColumns = 6;
inline constexpr int TileRowHeight = TileSize + 64;
inline constexpr int TileRows = 2;

inline constexpr int RowHeight = 104;
inline constexpr int RowGap = 10;
inline constexpr int VisibleRows = 4;
inline constexpr int ThumbWidth = 160;
inline constexpr int ThumbHeight = 90;

inline constexpr int PreviewWidth = 480;
inline constexpr int PreviewHeight = 270;
inline constexpr int FileRowHeight = 64;
inline constexpr int VisibleFileRows = 6;

}
