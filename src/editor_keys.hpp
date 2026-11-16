#pragma once

#include "editor.hpp"

#include <lapin.h>
#include <SFML/System/String.hpp>
#include <SFML/Window/Clipboard.hpp>

class KillBuffer
{
public:
    void breakSequence() { consecutive_ = false; }

    void kill(Editor &editor)
    {
        auto text = editor.killLine();
        if (text.empty())
            return;
        if (consecutive_)
            text_ += text;
        else
            text_ = std::move(text);
        consecutive_ = true;
    }

    void yank(Editor &editor, bool code) const
    {
        if (text_.empty())
            return;
        if (code && text_.find(U'\t') != std::u32string::npos)
            editor.insert(decodeUtf8(encodeUtf8(text_), true));
        else
            editor.insert(text_);
    }

private:
    std::u32string text_;
    bool consecutive_ = false;
};

inline bool modifierKey(t_bunny_keysym key)
{
    switch (key)
    {
    case BKS_LCONTROL: case BKS_RCONTROL:
    case BKS_LSHIFT: case BKS_RSHIFT:
    case BKS_LALT: case BKS_RALT:
    case BKS_LSYSTEM: case BKS_RSYSTEM:
        return true;
    default:
        return false;
    }
}

inline bool editKey(Editor &ed, t_bunny_keysym key, bool control, bool shift, int page, bool code, KillBuffer &kills)
{
    if (control)
    {
        if (key == BKS_A)
            ed.selectAll();
        else if (key == BKS_C || key == BKS_X)
        {
            if (!ed.selected().empty())
            {
                const auto bytes = encodeUtf8(ed.selected());
                sf::Clipboard::setString(sf::String::fromUtf8(bytes.begin(), bytes.end()));
                if (key == BKS_X)
                    ed.erase(true);
            }
        }
        else if (key == BKS_V)
        {
            const auto bytes = sf::Clipboard::getString().toUtf8();
            ed.insert(decodeUtf8(std::string(bytes.begin(), bytes.end()), true, code));
        }
        else if (key == BKS_Z)
            shift ? ed.redo() : ed.undo();
        else if (key == BKS_K)
            kills.kill(ed);
        else if (key == BKS_Y)
            kills.yank(ed, code);
        else if (key == BKS_HOME || key == BKS_END)
            ed.endpoint(key == BKS_END, true, shift);
        else
            return false;
    }
    else
    {
        switch (key)
        {
        case BKS_LEFT: ed.horizontal(-1, shift); break;
        case BKS_RIGHT: ed.horizontal(1, shift); break;
        case BKS_UP: ed.vertical(-1, shift); break;
        case BKS_DOWN: ed.vertical(1, shift); break;
        case BKS_PAGEUP: ed.vertical(-page, shift); break;
        case BKS_PAGEDOWN: ed.vertical(page, shift); break;
        case BKS_HOME: ed.endpoint(false, false, shift); break;
        case BKS_END: ed.endpoint(true, false, shift); break;
        case BKS_BACKSPACE: ed.erase(false); break;
        case BKS_DELETE: ed.erase(true); break;
        case BKS_RETURN:
            if (code)
                ed.newline();
            else
                ed.insert(U"\n");
            break;
        case BKS_TAB:
            if (code)
                ed.indent();
            else
                ed.insert(U"\t");
            break;
        default: return false;
        }
    }
    return true;
}
