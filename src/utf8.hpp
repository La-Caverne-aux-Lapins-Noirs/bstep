#pragma once

#include <string>
#include <string_view>

inline std::u32string decodeUtf8(std::string_view bytes, bool normalize = false, bool expandTabs = true)
{
    std::u32string text;
    for (std::size_t i = 0; i < bytes.size();)
    {
        unsigned first = static_cast<unsigned char>(bytes[i++]);
        char32_t value = first;
        int length = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            value &= 31;
            length = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            value &= 15;
            length = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            value &= 7;
            length = 3;
        }
        else if (first >= 0x80)
            value = U'�';

        bool complete = true;
        for (int j = 0; j < length; ++j)
        {
            if (i == bytes.size() || (static_cast<unsigned char>(bytes[i]) & 0xc0) != 0x80)
            {
                complete = false;
                break;
            }
            value = (value << 6) | (static_cast<unsigned char>(bytes[i++]) & 63);
        }
        constexpr char32_t mins[] = {0, 0x80, 0x800, 0x10000};
        if (!complete || value < mins[length] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            value = U'�';

        if (normalize && value == U'\r')
        {
            if (i < bytes.size() && bytes[i] == '\n')
                ++i;
            text += U'\n';
        }
        else if (normalize && value == U'\t')
            text += expandTabs ? U"    " : U"\t";
        else if (value == U'\n' || value >= U' ')
            text += value;
        else if (!normalize)
            text += U'·';
    }
    return text;
}

inline std::string encodeUtf8(std::u32string_view text)
{
    std::string bytes;
    for (const auto value : text)
    {
        if (value < 0x80)
            bytes += static_cast<char>(value);
        else if (value < 0x800)
        {
            bytes += static_cast<char>(0xc0 | (value >> 6));
            bytes += static_cast<char>(0x80 | (value & 0x3f));
        }
        else if (value < 0x10000)
        {
            bytes += static_cast<char>(0xe0 | (value >> 12));
            bytes += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
            bytes += static_cast<char>(0x80 | (value & 0x3f));
        }
        else
        {
            bytes += static_cast<char>(0xf0 | (value >> 18));
            bytes += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
            bytes += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
            bytes += static_cast<char>(0x80 | (value & 0x3f));
        }
    }
    return bytes;
}
