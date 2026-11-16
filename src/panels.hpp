#pragma once

#include "draw.hpp"

#include <charconv>
#include <optional>

namespace vt
{

constexpr std::array<unsigned, 16> palette{
    ink(0x000000), ink(0xca5454), ink(0x66b569), ink(0xc6a457),
    ink(0x6889ce), ink(0xb56dba), ink(0x55b4b6), ink(0xc0c0c0),
    ink(0x909090), ink(0xf07070), ink(0x83db82), ink(0xffd178),
    ink(0x8faeff), ink(0xe99bed), ink(0x78e0e1), ink(0xf8f8f8)
};

struct Style
{
    unsigned foreground = color::text;
    unsigned background = color::editor;
    bool bold = false;

    bool operator==(const Style &) const = default;
};

inline void sgr(Style &style, std::string_view sequence)
{
    while (true)
    {
        const auto delimiter = sequence.find(';');
        const auto param = sequence.substr(0, delimiter);
        int value = 0;
        const auto result = std::from_chars(param.data(), param.data() + param.size(), value);
        if (param.empty() || (result.ec == std::errc{} && result.ptr == param.end()))
        {
            if (value == 0) style = {};
            else if (value == 1) style.bold = true;
            else if (value == 22) style.bold = false;
            else if (value >= 30 && value <= 37) style.foreground = palette[value - 30];
            else if (value >= 90 && value <= 97) style.foreground = palette[value - 90 + 8];
            else if (value >= 40 && value <= 47) style.background = palette[value - 40];
            else if (value >= 100 && value <= 107) style.background = palette[value - 100 + 8];
            else if (value == 39) style.foreground = color::text;
            else if (value == 49) style.background = color::editor;
        }
        if (delimiter == sequence.npos)
            return;
        sequence.remove_prefix(delimiter + 1);
    }
}

struct Cell
{
    char32_t letter;
    Style style;
};

inline std::vector<std::vector<Cell>> lines(std::string_view stream, std::size_t width)
{
    std::vector<std::vector<Cell>> result(1);
    Style style;
    std::size_t column = 0;
    auto put = [&](char32_t character)
    {
        if (column == width)
        {
            result.emplace_back();
            column = 0;
        }
        auto &line = result.back();
        if (column == line.size())
            line.push_back({character, style});
        else
            line[column] = {character, style};
        ++column;
    };

    for (std::size_t i = 0; i < stream.size();)
    {
        if (stream[i] == '\x1b' && i + 1 < stream.size() && stream[i + 1] == '[')
        {
            const auto first = i + 2;
            i = first;
            while (i < stream.size() && stream[i] >= ' ' && stream[i] <= '?')
                ++i;
            if (i < stream.size() && stream[i] == 'm')
                sgr(style, stream.substr(first, i - first));
            if (i < stream.size())
                ++i;
        }
        else if (stream[i] == '\n')
        {
            ++i;
            result.emplace_back();
            column = 0;
        }
        else if (stream[i] == '\r')
        {
            ++i;
            column = 0;
        }
        else if (stream[i] == '\b')
        {
            ++i;
            if (column)
                --column;
        }
        else if (stream[i] == '\t')
        {
            ++i;
            const auto spaces = 4 - column % 4;
            for (std::size_t n = 0; n < spaces; ++n)
                put(U' ');
        }
        else
        {
            const auto first = i++;
            while (i < stream.size() && (static_cast<unsigned char>(stream[i]) & 0xc0) == 0x80)
                ++i;
            for (const auto character : decodeUtf8(stream.substr(first, i - first)))
                put(character);
        }
    }
    return result;
}

inline void paint(Painter &draw, Rect area, std::string_view stream, int scroll = 0, int size = 14)
{
    constexpr int lineHeight = 19;
    const int glyph = static_cast<int>(std::lround(atlas::monoAdvance * size / (4.0 * atlas::size)));
    const int rows = area.h / lineHeight;
    if (rows <= 0 || area.w < glyph)
        return;
    auto text = lines(stream, static_cast<std::size_t>(area.w / glyph));
    if (text.size() > 1 && text.back().empty())
        text.pop_back();
    const int excess = std::max(0, static_cast<int>(text.size()) - rows);
    const int start = excess - std::clamp(scroll, 0, excess);
    const int end = std::min(start + rows, static_cast<int>(text.size()));
    for (int i = start; i < end; ++i)
    {
        const auto &line = text[i];
        const int y = area.y + (i - start) * lineHeight;
        for (std::size_t begin = 0; begin < line.size();)
        {
            const auto &style = line[begin].style;
            std::u32string letters;
            std::size_t next = begin;
            while (next < line.size() && line[next].style == style)
                letters += line[next++].letter;
            const int x = area.x + static_cast<int>(begin) * glyph;
            if (style.background != color::editor)
                draw.rect({x, y, static_cast<int>(next - begin) * glyph, lineHeight}, style.background);
            draw.text(x, y, encodeUtf8(letters), style.foreground, size, true, style.bold);
            begin = next;
        }
    }
}

} // namespace vt

class LineInput
{
public:
    std::string text() const { return encodeUtf8(text_); }
    void set(std::string_view value)
    {
        text_ = decodeUtf8(value);
        cursor_ = text_.size();
        selectAll_ = false;
    }
    void clear() { set(""); }

    bool key(t_bunny_keysym key, bool control)
    {
        if (control && key == BKS_A)
            return selectAll_ = true;
        if (control)
            return false;
        switch (key)
        {
        case BKS_HOME: cursor_ = 0; break;
        case BKS_END: cursor_ = text_.size(); break;
        case BKS_LEFT: if (cursor_) --cursor_; break;
        case BKS_RIGHT: if (cursor_ < text_.size()) ++cursor_; break;
        case BKS_BACKSPACE:
        case BKS_DELETE:
            if (selectAll_)
                clear();
            else if (key == BKS_BACKSPACE && cursor_)
                text_.erase(--cursor_, 1);
            else if (key == BKS_DELETE && cursor_ < text_.size())
                text_.erase(cursor_, 1);
            break;
        default: return false;
        }
        selectAll_ = false;
        return true;
    }

    void insert(std::u32string_view characters)
    {
        if (selectAll_)
            clear();
        for (const auto character : characters)
            if (character >= U' ' && character != 127 && text_.size() < 2048)
                text_.insert(text_.begin() + cursor_++, character);
    }

    void paint(Painter &draw, Rect area, std::string_view placeholder, bool focused) const
    {
        if (area.w < 22 || area.h < 20)
            return;
        draw.frame(area, color::background, focused ? color::white : color::type);
        constexpr int font = 14, glyph = 8;
        const int columns = std::max(1, (area.w - 20) / glyph);
        const auto start = cursor_ < static_cast<std::size_t>(columns) ? 0 : cursor_ - columns + 1;
        const auto visible = std::u32string_view(text_).substr(start, columns);
        if (text_.empty() && !focused)
            draw.text(area.x + 8, area.y + 4, placeholder, color::type, font, true, false, area.w - 16);
        else
        {
            if (selectAll_ && focused && !visible.empty())
                draw.rect({area.x + 8, area.y + 3, static_cast<int>(visible.size()) * glyph, area.h - 6},
                          color::selection);
            draw.text(area.x + 8, area.y + 4, encodeUtf8(visible), color::text, font, true);
            if (focused)
                draw.rect({area.x + 8 + static_cast<int>(cursor_ - start) * glyph, area.y + 4, 1, area.h - 8},
                          color::white);
        }
    }

private:
    std::u32string text_;
    std::size_t cursor_ = 0;
    bool selectAll_ = false;
};
