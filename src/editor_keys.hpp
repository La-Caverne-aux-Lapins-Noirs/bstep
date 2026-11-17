#pragma once

#include "display.hpp"
#include "editor.hpp"

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

inline bool editKey(Editor &ed, Key key, bool control, bool shift, int page, bool code, KillBuffer &kills, UiDisplay &display)
{
    if (control)
    {
        if (key == Key::a)
            ed.selectAll();
        else if (key == Key::c || key == Key::x)
        {
            if (!ed.selected().empty())
            {
                display.copy(encodeUtf8(ed.selected()));
                if (key == Key::x)
                    ed.erase(true);
            }
        }
        else if (key == Key::v)
        {
            ed.insert(decodeUtf8(display.clipboard(), true, code));
        }
        else if (key == Key::z)
            shift ? ed.redo() : ed.undo();
        else if (key == Key::k)
            kills.kill(ed);
        else if (key == Key::y)
            kills.yank(ed, code);
        else if (key == Key::home || key == Key::end)
            ed.endpoint(key == Key::end, true, shift);
        else
            return false;
    }
    else
    {
        switch (key)
        {
        case Key::left: ed.horizontal(-1, shift); break;
        case Key::right: ed.horizontal(1, shift); break;
        case Key::up: ed.vertical(-1, shift); break;
        case Key::down: ed.vertical(1, shift); break;
        case Key::pageUp: ed.vertical(-page, shift); break;
        case Key::pageDown: ed.vertical(page, shift); break;
        case Key::home: ed.endpoint(false, false, shift); break;
        case Key::end: ed.endpoint(true, false, shift); break;
        case Key::backspace: ed.erase(false); break;
        case Key::del: ed.erase(true); break;
        case Key::enter:
            if (code)
                ed.newline();
            else
                ed.insert(U"\n");
            break;
        case Key::tab:
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
