#pragma once

#include "font_atlas.hpp"
#include "utf8.hpp"

#include <lapin.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

constexpr unsigned ink(unsigned rgb)
{
    return COLOR(255u, (rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u);
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

    bool contains(t_bunny_position p) const
    {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr int middle() const { return x + w / 2; }
};

struct PictureDeleter
{
    void operator()(t_bunny_picture *picture) const { bunny_delete_clipable(picture); }
};

class Painter
{
public:
    explicit Painter(const std::filesystem::path &assets)
        : mono_(loadAtlas(assets / "atlas-mono.png"))
        , sans_(loadAtlas(assets / "atlas-sans.png"))
        , bold_(loadAtlas(assets / "atlas-mono-bold.png"))
    {
    }

    Painter(const Painter &) = delete;
    Painter &operator=(const Painter &) = delete;

    void target(t_bunny_buffer &buffer) { buffer_ = &buffer; }

    void rect(Rect r, unsigned tint)
    {
        if (r.w <= 0 || r.h <= 0)
            return;
        const std::array<t_bunny_vertex, 4> points{
            vertex(r.x, r.y, tint), vertex(r.right(), r.y, tint),
            vertex(r.right(), r.bottom(), tint), vertex(r.x, r.bottom(), tint)
        };
        shape(BGY_QUADS, points.data(), points.size());
    }

    void frame(Rect r, unsigned fill, unsigned border)
    {
        rect(r, border);
        rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, fill);
    }

    void line(int x1, int y1, int x2, int y2, unsigned tint)
    {
        const std::array<t_bunny_vertex, 2> points{vertex(x1, y1, tint), vertex(x2, y2, tint)};
        shape(BGY_LINES, points.data(), points.size());
    }

    void triangle(t_bunny_position a, t_bunny_position b, t_bunny_position c, unsigned tint)
    {
        const std::array<t_bunny_vertex, 3> points{
            vertex(a.x, a.y, tint), vertex(b.x, b.y, tint), vertex(c.x, c.y, tint)
        };
        shape(BGY_TRIANGLES, points.data(), points.size());
    }

    void dotGrid(Rect r)
    {
        std::vector<t_bunny_vertex> points;
        for (int y = r.y + 10; y < r.bottom() - 3; y += 16)
            for (int x = r.x + 10; x < r.right() - 3; x += 16)
                points.push_back(vertex(x, y, color::grid));
        shape(BGY_PIXELS, points.data(), points.size());
    }

    static int textWidth(std::string_view contents, int size, bool mono = false)
    {
        double width = 0;
        for (const char32_t character : decodeUtf8(contents))
            width += advance(atlas::index(character), size, mono);
        return static_cast<int>(std::ceil(width));
    }

    void text(int x, int y, std::string_view contents, unsigned tint, int size = 16,
              bool mono = false, bool bold = false, int maxWidth = 32768)
    {
        auto &picture = mono ? (bold ? *bold_ : *mono_) : *sans_;
        const double scale = double(size) / atlas::size;
        picture.scale = {scale, scale};
        picture.color_mask.full = tint;
        const int padding = static_cast<int>(std::lround(atlas::leftPadding * scale));

        double cursor = x;
        for (const char32_t codepoint : decodeUtf8(contents))
        {
            if (codepoint == U'\n')
                break;
            const auto glyph = atlas::index(codepoint);
            const double next = cursor + advance(glyph, size, mono);
            if (next > x + maxWidth)
                break;
            if (codepoint != U' ' && codepoint != 0xA0)
            {
                picture.clip_x_position = (glyph % atlas::columns) * atlas::cellWidth;
                picture.clip_y_position = (glyph / atlas::columns) * atlas::cellHeight;
                const t_bunny_position pos{static_cast<int>(std::lround(cursor)) - padding, y};
                bunny_blit(buffer_, &picture, &pos);
            }
            cursor = next;
        }
    }

private:
    using Picture = std::unique_ptr<t_bunny_picture, PictureDeleter>;

    t_bunny_buffer *buffer_ = nullptr;
    Picture mono_;
    Picture sans_;
    Picture bold_;

    static double advance(std::size_t glyph, int size, bool mono)
    {
        const double factor = size / (4.0 * atlas::size);
        return mono ? std::lround(atlas::monoAdvance * factor) : atlas::sansAdvance[glyph] * factor;
    }

    static Picture loadAtlas(const std::filesystem::path &file)
    {
        Picture atlasPicture(bunny_load_picture(file.c_str()));
        constexpr int rows = (atlas::glyphCount + atlas::columns - 1) / atlas::columns;
        if (!atlasPicture || atlasPicture->buffer.width != atlas::columns * atlas::cellWidth
            || atlasPicture->buffer.height != rows * atlas::cellHeight)
            throw std::runtime_error("Atlas de glyphes introuvable ou invalide : " + file.string());

        atlasPicture->clip_width = atlas::cellWidth;
        atlasPicture->clip_height = atlas::cellHeight;
        atlasPicture->smooth = true;
        return atlasPicture;
    }

    static t_bunny_vertex vertex(int x, int y, unsigned tint)
    {
        return {{static_cast<double>(x), static_cast<double>(y)}, {0, 0}, tint};
    }

    void shape(t_bunny_geometry kind, const t_bunny_vertex *points, std::size_t n)
    {
        if (!n)
            return;
        // LibLapin's vertex array stores its vertices immediately after its header.
        auto *array = static_cast<t_bunny_vertex_array *>(
            bunny_alloca(sizeof(t_bunny_vertex_array) + n * sizeof(t_bunny_vertex)));
        array->length = n;
        std::copy_n(points, n, array->vertex);
        bunny_set_geometry(buffer_, kind, array, nullptr);
    }
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

inline void paintButton(Painter &draw, Rect r, t_bunny_position hover, std::string_view label,
                        std::string_view key, unsigned accent, bool primary = false, bool enabled = true)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    const bool over = enabled && r.contains(hover);
    const auto fill = primary && enabled ? (over ? color::white : accent)
        : over ? color::selection : color::background;
    const auto edge = enabled ? accent : color::type;
    const auto textColor = primary && enabled ? color::background : enabled ? color::text : color::type;
    draw.frame(r, fill, edge);
    const int pad = r.w < 105 ? 6 : 10;
    const int font = r.w < 105 ? 14 : 15;
    const int room = r.w - 2 * pad;
    const int keyWidth = Painter::textWidth(key, 11, true);
    const bool fitsKey = !key.empty() && Painter::textWidth(label, font) + keyWidth + 12 <= room;
    draw.text(r.x + pad, r.y + (r.h - 19) / 2, label, textColor, font, false, false,
              room - (fitsKey ? keyWidth + 10 : 0));
    if (fitsKey)
        draw.text(r.right() - pad - keyWidth, r.y + (r.h - 15) / 2, key,
                  primary && enabled ? color::limeDark : color::type, 11, true);
}
