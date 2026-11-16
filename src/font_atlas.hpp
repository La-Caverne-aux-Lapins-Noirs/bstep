#pragma once

#include <array>
#include <cstddef>

namespace atlas
{

// Rasterized from IBM Plex Mono Regular and Geist Regular. Advance widths are in quarter pixels.
inline constexpr int size = 32;
inline constexpr int columns = 16;
inline constexpr int cellWidth = 44;
inline constexpr int cellHeight = 48;
inline constexpr int leftPadding = 5;
inline constexpr int monoAdvance = 76;
inline constexpr auto extras = U"ŒœŸ–—’…→�";
inline constexpr std::size_t glyphCount = 200;

inline constexpr std::array<unsigned char, glyphCount> sansAdvance{
    32, 32, 40, 60, 80, 100, 76, 20, 32, 36, 52, 72, 24, 52, 24, 60,
    88, 48, 80, 76, 80, 80, 76, 68, 76, 76, 40, 40, 68, 68, 72, 68,
    112, 84, 88, 88, 88, 76, 76, 88, 92, 36, 76, 84, 76, 112, 96, 92,
    84, 92, 84, 84, 76, 84, 84, 120, 76, 76, 72, 44, 60, 48, 56, 68,
    32, 68, 76, 68, 76, 68, 48, 76, 76, 32, 32, 76, 36, 112, 76, 72,
    76, 76, 48, 64, 48, 76, 68, 104, 76, 68, 72, 52, 36, 48, 64, 32,
    32, 68, 80, 80, 76, 36, 72, 40, 104, 48, 68, 64, 76, 80, 44, 48,
    68, 44, 44, 32, 76, 80, 24, 40, 32, 52, 68, 96, 104, 104, 72, 84,
    84, 84, 88, 84, 88, 124, 88, 76, 76, 76, 76, 36, 36, 36, 40, 88,
    96, 92, 92, 92, 92, 92, 64, 92, 84, 84, 84, 84, 76, 80, 76, 68,
    68, 68, 68, 68, 68, 116, 68, 68, 68, 68, 68, 32, 32, 32, 36, 72,
    76, 72, 72, 72, 72, 72, 68, 72, 76, 76, 76, 76, 68, 76, 64, 136,
    120, 72, 72, 112, 24, 72, 104, 76,
};

constexpr std::size_t index(char32_t codepoint)
{
    if (codepoint >= U' ' && codepoint <= U'~')
        return codepoint - U' ';
    constexpr std::size_t asciiCount = U'~' - U' ' + 1;
    if (codepoint >= 0xA0 && codepoint <= 0xFF)
        return asciiCount + codepoint - 0xA0;
    for (std::size_t i = 0; extras[i]; ++i)
        if (extras[i] == codepoint)
            return asciiCount + 96 + i;
    return U'?' - U' ';
}

} // namespace atlas
