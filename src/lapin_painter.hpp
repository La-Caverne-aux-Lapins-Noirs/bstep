#pragma once

#include "draw.hpp"
#include "font_atlas.hpp"
#include "paths.hpp"
#include "utf8.hpp"

#include <lapin.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static_assert(ink(0x123456) == static_cast<unsigned>(COLOR(255u, 0x12u, 0x34u, 0x56u)));

struct PictureDeleter
{
    void operator()(t_bunny_picture *picture) const { bunny_delete_clipable(picture); }
};

class LapinPainter final : public Painter
{
public:
    explicit LapinPainter(const BstepPaths &paths)
        : mono_(loadAtlas(paths.asset("atlas-mono.png")))
        , sans_(loadAtlas(paths.asset("atlas-sans.png")))
        , bold_(loadAtlas(paths.asset("atlas-mono-bold.png")))
    {
    }

    LapinPainter(const LapinPainter &) = delete;
    LapinPainter &operator=(const LapinPainter &) = delete;

    void target(t_bunny_buffer &buffer) { buffer_ = &buffer; }

    void rect(Rect r, unsigned tint) override
    {
        if (r.w <= 0 || r.h <= 0)
            return;
        const std::array<t_bunny_vertex, 4> points{
            vertex(r.x, r.y, tint), vertex(r.right(), r.y, tint),
            vertex(r.right(), r.bottom(), tint), vertex(r.x, r.bottom(), tint)
        };
        shape(BGY_QUADS, points.data(), points.size());
    }

    void frame(Rect r, unsigned fill, unsigned border) override
    {
        rect(r, border);
        rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, fill);
    }

    void line(int x1, int y1, int x2, int y2, unsigned tint) override
    {
        const std::array<t_bunny_vertex, 2> points{vertex(x1, y1, tint), vertex(x2, y2, tint)};
        shape(BGY_LINES, points.data(), points.size());
    }

    void triangle(Point a, Point b, Point c, unsigned tint) override
    {
        const std::array<t_bunny_vertex, 3> points{
            vertex(a.x, a.y, tint), vertex(b.x, b.y, tint), vertex(c.x, c.y, tint)
        };
        shape(BGY_TRIANGLES, points.data(), points.size());
    }

    void dotGrid(Rect r) override
    {
        std::vector<t_bunny_vertex> points;
        for (int y = r.y + 10; y < r.bottom() - 3; y += 16)
            for (int x = r.x + 10; x < r.right() - 3; x += 16)
                points.push_back(vertex(x, y, color::grid));
        shape(BGY_PIXELS, points.data(), points.size());
    }

    int textWidth(std::string_view contents, int size, bool mono) const override
    {
        double width = 0;
        for (const char32_t character : decodeUtf8(contents))
            width += advance(atlas::index(character), size, mono);
        return static_cast<int>(std::ceil(width));
    }

    void text(int x, int y, std::string_view contents, unsigned tint, int size = 16,
              bool mono = false, bool bold = false, int maxWidth = 32768) override
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
