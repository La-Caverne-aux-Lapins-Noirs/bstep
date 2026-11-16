#pragma once

#include "c_indent.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Editor
{
public:
    explicit Editor(std::string_view code)
    {
        current_.text = decodeUtf8(code, true);
        reindex();
    }

    std::size_t revision() const { return revision_; }
    std::string source() const { return encodeUtf8(current_.text); }
    std::size_t cursor() const { return current_.cursor; }
    int lines() const { return static_cast<int>(starts_.size()); }
    int top = 0;
    int left = 0;

    std::u32string_view line(int row) const
    {
        const auto a = starts_[row];
        return std::u32string_view(current_.text).substr(a, length(row));
    }

    std::pair<int, int> location() const
    {
        const int row = rowAt(current_.cursor);
        return {row, static_cast<int>(current_.cursor - starts_[row])};
    }

    std::pair<std::size_t, std::size_t> selection() const
    {
        return std::minmax(current_.cursor, current_.anchor);
    }

    std::u32string_view selected() const
    {
        const auto [a, b] = selection();
        return std::u32string_view(current_.text).substr(a, b - a);
    }

    std::size_t at(int row, int column) const
    {
        row = std::clamp(row, 0, lines() - 1);
        return starts_[row] + std::clamp(column, 0, length(row));
    }

    void place(std::size_t to, bool extend = false)
    {
        current_.cursor = std::min(to, current_.text.size());
        if (!extend)
            current_.anchor = current_.cursor;
    }

    void selectAll()
    {
        current_.anchor = 0;
        current_.cursor = current_.text.size();
    }

    void horizontal(int delta, bool extend)
    {
        const auto [a, b] = selection();
        if (!extend && a != b)
            place(delta < 0 ? a : b);
        else if (delta < 0)
            place(current_.cursor ? current_.cursor - 1 : 0, extend);
        else
            place(current_.cursor + 1, extend);
    }

    void vertical(int delta, bool extend)
    {
        const auto [row, column] = location();
        place(at(row + delta, column), extend);
    }

    void endpoint(bool end, bool document, bool extend)
    {
        if (document)
            place(end ? current_.text.size() : 0, extend);
        else
        {
            const auto row = location().first;
            place(at(row, end ? length(row) : 0), extend);
        }
    }

    bool insert(std::u32string_view text)
    {
        const auto [a, b] = selection();
        if (a == b && text.empty())
            return false;
        return replace(a, b, text);
    }

    bool erase(bool forward)
    {
        auto [a, b] = selection();
        if (a == b)
        {
            if (forward && b < current_.text.size())
                ++b;
            else if (!forward && a > 0)
                --a;
            else
                return false;
        }
        return replace(a, b, {});
    }

    bool newline()
    {
        const auto [begin, end] = selection();
        const int row = rowAt(begin);
        auto edited = current_;
        edited.text.replace(begin, end - begin, 1, U'\n');
        edited.cursor = edited.anchor = begin + 1;
        const CIndentation plan(edited.text);
        return reindent(edited, plan, row, row + 1, row + 1) && commit(std::move(edited));
    }

    void indent()
    {
        const auto [begin, end] = selection();
        const int first = rowAt(begin);
        const int last = rowAt(begin == end ? end : end - 1);
        auto edited = current_;
        const CIndentation plan(edited.text);
        if (reindent(edited, plan, first, last, begin == end ? first : -1))
            commit(std::move(edited));
    }

    bool type(char32_t character)
    {
        const auto [begin, end] = selection();
        const bool afterElse = character == U' ' && std::u32string_view(current_.text).substr(0, begin)
                                                           .ends_with(U"else");
        if (!afterElse && std::u32string_view(U"{}()[];:#").find(character) == std::u32string_view::npos)
            return insert(std::u32string_view(&character, 1));

        auto edited = current_;
        edited.text.replace(begin, end - begin, 1, character);
        edited.cursor = edited.anchor = begin + 1;
        const CIndentation plan(edited.text, begin);
        const int row = rowAt(begin);
        if (plan.electric() && !reindent(edited, plan, row, row, row))
            return insert(std::u32string_view(&character, 1));
        return commit(std::move(edited));
    }

    std::u32string killLine()
    {
        const auto point = current_.cursor;
        const int row = rowAt(point);
        auto end = at(row, length(row));
        const auto suffix = std::u32string_view(current_.text).substr(point, end - point);
        if (end < current_.text.size() && std::ranges::all_of(suffix, CIndentation::space))
            ++end;
        if (end == point)
            return {};
        auto killed = current_.text.substr(point, end - point);
        if (!replace(point, end, {}))
            return {};
        return killed;
    }

    void undo()
    {
        moveDraft(undo_, redo_);
    }

    void redo()
    {
        moveDraft(redo_, undo_);
    }

private:
    struct Draft
    {
        std::u32string text;
        std::size_t cursor = 0;
        std::size_t anchor = 0;
    };

    Draft current_;
    std::deque<Draft> undo_;
    std::deque<Draft> redo_;
    std::vector<std::size_t> starts_;
    std::size_t revision_ = 0;
    static constexpr std::size_t textLimit = 32768;

    int rowAt(std::size_t position) const
    {
        return static_cast<int>(std::upper_bound(starts_.begin(), starts_.end(), position) - starts_.begin()) - 1;
    }

    static bool reindent(Draft &draft, const CIndentation &plan, int first, int last, int indentBlank)
    {
        const auto begin = plan.start(first);
        const auto end = last + 1 < plan.lines() ? plan.start(last + 1) : draft.text.size();
        const auto unaffected = draft.text.size() - (end - begin);
        if (unaffected > textLimit)
            return false;

        std::u32string replacement;
        for (int row = first; row <= last; ++row)
        {
            const auto start = plan.start(row);
            const auto content = plan.content(row);
            const auto next = row + 1 < plan.lines() ? plan.start(row + 1) : draft.text.size();
            const auto previous = content - start;
            const int wanted = plan.width(row);
            const bool blank = content == draft.text.size() || draft.text[content] == U'\n';
            const std::size_t spaces = wanted < 0 ? previous
                : blank && row != indentBlank ? 0 : static_cast<std::size_t>(wanted);
            if (unaffected + replacement.size() + spaces + (next - content) > textLimit)
                return false;
            if (wanted >= 0)
            {
                const auto shiftedStart = begin + replacement.size();
                auto movePoint = [=](std::size_t &point) {
                    if (point >= shiftedStart)
                        point = std::max(point, shiftedStart + previous) - previous + spaces;
                };
                movePoint(draft.cursor);
                movePoint(draft.anchor);
            }
            if (wanted < 0)
                replacement.append(draft.text, start, previous);
            else
                replacement.append(spaces, U' ');
            replacement.append(draft.text, content, next - content);
        }
        draft.text.replace(begin, end - begin, replacement);
        return true;
    }

    bool commit(Draft edited)
    {
        if (edited.text.size() > textLimit)
            return false;
        if (edited.text == current_.text)
        {
            current_.cursor = edited.cursor;
            current_.anchor = edited.anchor;
            return false;
        }
        push(undo_, current_);
        redo_.clear();
        current_ = std::move(edited);
        reindex();
        return true;
    }

    int length(int row) const
    {
        const auto end = row + 1 < lines() ? starts_[row + 1] - 1 : current_.text.size();
        return static_cast<int>(end - starts_[row]);
    }

    void reindex()
    {
        starts_.assign(1, 0);
        for (std::size_t i = 0; i < current_.text.size(); ++i)
            if (current_.text[i] == U'\n')
                starts_.push_back(i + 1);
        ++revision_;
    }

    static void push(std::deque<Draft> &stack, const Draft &draft)
    {
        if (stack.size() == 80)
            stack.pop_front();
        stack.push_back(draft);
    }

    bool replace(std::size_t a, std::size_t b, std::u32string_view text)
    {
        if (current_.text.size() - (b - a) + text.size() > textLimit)
            return false;
        push(undo_, current_);
        redo_.clear();
        current_.text.replace(a, b - a, text);
        place(a + text.size());
        reindex();
        return true;
    }

    void moveDraft(std::deque<Draft> &from, std::deque<Draft> &to)
    {
        if (from.empty())
            return;
        push(to, current_);
        current_ = std::move(from.back());
        from.pop_back();
        reindex();
    }
};
