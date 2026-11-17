#pragma once

#include "editor.hpp"
#include "panels.hpp"

inline constexpr int codeLinePitch(bool terminal) { return terminal ? terminalCellHeight : 22; }
inline constexpr int codeGlyphWidth(bool terminal) { return terminal ? terminalCellWidth : 10; }
inline constexpr int codeGutter(bool terminal) { return terminal ? 40 : 54; }

struct CodeMarkers
{
    int active = 0;
    int previous = 0;
    bool fault = false;
    bool caret = false;
};

inline bool codeName(char32_t c)
{
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')
        || (c >= U'0' && c <= U'9') || c == U'_';
}

inline std::vector<vt::Style> syntax(std::u32string_view line, bool &comment)
{
    std::vector<vt::Style> spans(line.size());
    auto mark = [&spans](std::size_t start, std::size_t end, unsigned tint, bool bold = false)
    {
        std::fill(spans.begin() + start, spans.begin() + end, vt::Style{tint, color::editor, bold});
    };

    for (std::size_t i = 0; i < line.size();)
    {
        const auto start = i;
        const auto tail = line.substr(i);
        if (comment || tail.starts_with(U"/*"))
        {
            const auto end = line.find(U"*/", i + (comment ? 0 : 2));
            comment = end == line.npos;
            i = comment ? line.size() : end + 2;
            mark(start, i, vt::palette[8]);
        }
        else if (tail.starts_with(U"//") || line[i] == U'#')
        {
            mark(i, line.size(), line[i] == U'#' ? vt::palette[3] : vt::palette[8]);
            break;
        }
        else if (line[i] == U'"' || line[i] == U'\'')
        {
            const char32_t delim = line[i++];
            while (i < line.size())
            {
                if (line[i] == U'\\' && i + 1 < line.size())
                    i += 2;
                else if (line[i++] == delim)
                    break;
            }
            mark(start, i, vt::palette[2]);
        }
        else if (codeName(line[i]))
        {
            while (i < line.size() && codeName(line[i]))
                ++i;
            const auto word = line.substr(start, i - start);
            constexpr std::array<std::u32string_view, 8> words{
                U"int", U"char", U"void", U"while", U"if", U"else", U"for", U"return"
            };
            if (std::find(words.begin(), words.end(), word) != words.end())
                mark(start, i, vt::palette[14], true);
            else if ((line[start] >= U'0' && line[start] <= U'9') || word == U"NULL")
                mark(start, i, vt::palette[13]);
        }
        else
            ++i;
    }
    return spans;
}

inline void paintSource(Painter &draw, const Editor &ed, Rect codeView, CodeMarkers marks = {})
{
    const int codePitch = codeLinePitch(draw.terminal());
    const int glyphWidth = codeGlyphWidth(draw.terminal());
    const int codeX = codeView.x + codeGutter(draw.terminal());
    const int codeRows = codeView.h / codePitch;
    const int codeColumns = std::max(0, (codeView.right() - codeX - 10) / glyphWidth);
    draw.rect(codeView, color::editor);

    const auto [caretRow, caretCol] = ed.location();
    const auto [selA, selB] = ed.selection();
    bool comment = false;
    for (int row = 0; row < std::min(ed.lines(), ed.top + codeRows); ++row)
    {
        const auto raw = ed.line(row);
        const auto tint = syntax(raw, comment);
        if (row < ed.top)
            continue;
        const int y = codeView.y + (row - ed.top) * codePitch;
        const bool current = row + 1 == marks.active;
        if (current)
        {
            draw.rect({codeView.x, y, codeView.w, codePitch}, marks.fault ? color::errorDark : color::limeDark);
            const auto marker = marks.fault ? color::error : color::lime;
            draw.triangle({codeView.x + 8, y + 6}, {codeView.x + 8, y + 16}, {codeView.x + 15, y + 11}, marker);
        }
        else if (row + 1 == marks.previous)
            draw.rect({codeView.x + 10, y + 9, 4, 4}, color::mint);

        const auto lineNo = std::to_string(row + 1);
        draw.text(codeX - 10 - static_cast<int>(lineNo.size()) * 7, y + 4, lineNo,
                   current ? (marks.fault ? color::error : color::lime) : color::type, 12, true);

        const auto lineStart = ed.at(row, 0);
        if (selA != selB && selB > lineStart && selA <= lineStart + raw.size())
        {
            const auto low = static_cast<int>(std::max(lineStart, selA) - lineStart);
            const auto high = static_cast<int>(std::min(selB - lineStart, raw.size() + 1));
            const auto a = std::clamp(low - ed.left, 0, codeColumns);
            const auto b = std::clamp(high - ed.left, 0, codeColumns);
            draw.rect({codeX + a * glyphWidth, y + 1, (b - a) * glyphWidth, codePitch - 2},
                       color::selection);
        }

        const int end = std::min(static_cast<int>(raw.size()), ed.left + codeColumns);
        for (int i = ed.left; i < end;)
        {
            int j = i + 1;
            while (j < end && tint[j] == tint[i])
                ++j;
            // The font renderer trims leading blanks. Position each span after its indent.
            while (i < j && raw[i] == U' ')
                ++i;
            if (i < j)
                draw.text(codeX + (i - ed.left) * glyphWidth, y + 2,
                           encodeUtf8(raw.substr(i, j - i)), tint[i].foreground, 16, true, tint[i].bold);
            i = j;
        }
        if (marks.caret && row == caretRow && caretCol >= ed.left
            && caretCol <= ed.left + codeColumns)
            draw.caret({codeX + (caretCol - ed.left) * glyphWidth, y + 3, 2, 16}, color::mint);
    }
    if (ed.lines() > codeRows && codeRows > 0)
    {
        const int thumb = std::min(codeView.h, std::max(15, codeView.h * codeRows / ed.lines()));
        const int y = codeView.y + (codeView.h - thumb) * ed.top / (ed.lines() - codeRows);
        draw.rect({codeView.right() - 4, y, 3, thumb}, color::rule);
    }
}
