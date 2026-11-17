#pragma once

#include "input.hpp"

#include <algorithm>
#include <string_view>

constexpr unsigned ink(unsigned rgb)
{
    return 0xff000000u | ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 0xffu);
}

namespace color
{
constexpr auto background = ink(0x000000);
constexpr auto panel = background;
constexpr auto editor = ink(0x000000);
constexpr auto board = ink(0x000000);
constexpr auto grid = ink(0x262626);
constexpr auto rule = ink(0xdadada);
constexpr auto type = ink(0x989898);
constexpr auto muted = ink(0xb6b6b6);
constexpr auto text = ink(0xd5d5d5);
constexpr auto lime = ink(0xffffff);
constexpr auto limeDark = ink(0x242424);
constexpr auto mint = ink(0x55c6cd);
constexpr auto mintDark = ink(0x102729);
constexpr auto gold = ink(0xf1be58);
constexpr auto goldDark = ink(0x33250d);
constexpr auto lavender = ink(0xc88cf2);
constexpr auto lavenderDark = ink(0x261c2d);
constexpr auto error = ink(0xff6666);
constexpr auto errorDark = ink(0x2e1010);
constexpr auto selection = ink(0x3a3a3a);
constexpr auto white = ink(0xf8f8f8);
}

struct Rect
{
    int x;
    int y;
    int w;
    int h;

    bool contains(Point p) const
    {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr int middle() const { return x + w / 2; }
};

inline constexpr int terminalCellWidth = 8;
inline constexpr int terminalCellHeight = 19;

class Painter
{
public:
    virtual ~Painter() = default;
    virtual bool terminal() const { return false; }

    virtual void rect(Rect r, unsigned tint) = 0;
    virtual void frame(Rect r, unsigned fill, unsigned border) = 0;
    virtual void line(int x1, int y1, int x2, int y2, unsigned tint) = 0;
    virtual void triangle(Point a, Point b, Point c, unsigned tint) = 0;
    virtual void dotGrid(Rect r) = 0;
    virtual void caret(Rect r, unsigned tint) { rect(r, tint); }
    virtual int textWidth(std::string_view contents, int size, bool mono = false) const = 0;
    virtual void text(int x, int y, std::string_view contents, unsigned tint, int size = 16,
                      bool mono = false, bool bold = false, int maxWidth = 32768) = 0;
};

inline Rect inset(Rect area, int side, int top, int bottom)
{
    return {area.x + side, area.y + top, std::max(0, area.w - 2 * side),
            std::max(0, area.h - top - bottom)};
}

inline Rect horizontalPart(Rect area, int index, int count, int gap = 6)
{
    const int usable = std::max(0, area.w - (count - 1) * gap);
    const int first = usable * index / count;
    const int next = usable * (index + 1) / count;
    return {area.x + first + index * gap, area.y, next - first, area.h};
}

inline void paintButton(Painter &draw, Rect r, Point hover, std::string_view label,
                        std::string_view key, unsigned accent, bool primary = false, bool enabled = true)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    const bool over = enabled && r.contains(hover);
    const auto fill = primary && enabled ? (over ? color::white : accent)
        : over ? color::selection : color::background;
    const auto edge = enabled ? accent : color::type;
    const auto textColor = primary && enabled ? color::background : enabled ? color::text : color::type;
    if (draw.terminal())
    {
        constexpr int cell = terminalCellWidth;
        if (r.w < 3 * cell)
            return;
        const int y = r.y + (r.h - 1) / 2;
        const int room = r.w - 2 * cell;
        const int keyWidth = draw.textWidth(key, 11, true);
        const bool fitsKey = !key.empty() && draw.textWidth(label, 15) + cell + keyWidth <= room;
        draw.rect(r, fill);
        draw.text(r.x, y, "[", primary && enabled ? textColor : edge);
        draw.text(r.x + cell, y, label, textColor, 15, false, primary,
                  room - (fitsKey ? keyWidth + cell : 0));
        if (fitsKey)
            draw.text(r.right() - cell - keyWidth, y, key, primary ? textColor : color::muted);
        draw.text(r.right() - cell, y, "]", primary && enabled ? textColor : edge);
        return;
    }
    draw.frame(r, fill, edge);
    const int pad = r.w < 105 ? 6 : 10;
    const int font = r.w < 105 ? 14 : 15;
    const int room = r.w - 2 * pad;
    const int keyWidth = draw.textWidth(key, 11, true);
    const bool fitsKey = !key.empty() && draw.textWidth(label, font) + keyWidth + 12 <= room;
    draw.text(r.x + pad, r.y + (r.h - 19) / 2, label, textColor, font, false, false,
              room - (fitsKey ? keyWidth + 10 : 0));
    if (fitsKey)
        draw.text(r.right() - pad - keyWidth, r.y + (r.h - 15) / 2, key,
                  primary && enabled ? color::limeDark : color::type, 11, true);
}
