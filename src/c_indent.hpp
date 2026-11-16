#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

class CIndentation
{
public:
    explicit CIndentation(std::u32string_view source, std::size_t typed = std::u32string_view::npos)
    {
        tokenize(source, typed);
        widths_.resize(starts_.size());
        for (const auto start : starts_)
        {
            auto content = start;
            while (content < source.size() && space(source[content]))
                ++content;
            content_.push_back(content);
        }
        while (!end())
            statement(0, 0);
        mark(token().row, 0);
    }

    // A negative width preserves leading whitespace in a continued literal or directive.
    int width(int row) const { return widths_[row]; }
    bool electric() const { return electric_; }
    int lines() const { return static_cast<int>(starts_.size()); }
    std::size_t start(int row) const { return starts_[row]; }
    std::size_t content(int row) const { return content_[row]; }

    static bool space(char32_t ch) { return ch == U' ' || ch == U'\t'; }

private:
    static constexpr int step = 4;
    static constexpr int maxDepth = 128;

    enum class Kind { code, quote, lineComment, blockComment, directive, end };

    struct Token
    {
        Kind kind;
        std::u32string_view text;
        std::size_t offset;
        int row, lastRow;
    };

    std::vector<Token> tokens_;
    std::vector<std::size_t> starts_{0}, content_;
    std::vector<int> widths_;
    std::size_t cursor_ = 0;
    int nextRow_ = 0;
    bool electric_ = false;

    static bool word(char32_t ch)
    {
        return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z')
            || (ch >= U'0' && ch <= U'9') || ch == U'_' || ch >= 128;
    }

    void tokenize(std::u32string_view source, std::size_t typed)
    {
        std::size_t pos = 0;
        auto take = [&] {
            const auto ch = source[pos++];
            if (ch == U'\n')
                starts_.push_back(pos);
            return ch;
        };
        auto logicalLine = [&] {
            while (pos < source.size())
            {
                if (source[pos] == U'\n' && source[pos - 1] != U'\\')
                    break;
                take();
            }
        };

        while (pos < source.size())
        {
            const auto start = pos;
            const int row = static_cast<int>(starts_.size()) - 1;
            const auto ch = take();
            if (space(ch) || ch == U'\n')
                continue;
            auto kind = Kind::code;
            if (ch == U'#' && source.substr(starts_.back(), start - starts_.back())
                                  .find_first_not_of(U" \t") == std::u32string_view::npos)
            {
                kind = Kind::directive;
                logicalLine();
            }
            else if (source.substr(start).starts_with(U"//"))
            {
                kind = Kind::lineComment;
                logicalLine();
            }
            else if (source.substr(start).starts_with(U"/*"))
            {
                kind = Kind::blockComment;
                take();
                while (pos < source.size() && !source.substr(pos).starts_with(U"*/"))
                    take();
                if (pos < source.size())
                {
                    take();
                    take();
                }
            }
            else if (ch == U'\'' || ch == U'"')
            {
                kind = Kind::quote;
                while (pos < source.size() && source[pos] != U'\n')
                {
                    const auto next = take();
                    if (next == ch)
                        break;
                    if (next == U'\\' && pos < source.size())
                        take();
                }
            }
            else if (word(ch))
                while (pos < source.size() && word(source[pos]))
                    take();

            const auto text = source.substr(start, pos - start);
            tokens_.push_back({kind, text, start, row, static_cast<int>(starts_.size()) - 1});
            if ((kind == Kind::code || kind == Kind::directive) && start == typed)
                electric_ = true;
            if (kind == Kind::code && text == U"else" && pos == typed)
                electric_ = true;
        }
        tokens_.push_back({Kind::end, {}, pos, static_cast<int>(starts_.size()) - 1,
                           static_cast<int>(starts_.size()) - 1});
    }

    const Token &token() const { return tokens_[cursor_]; }
    bool at(std::u32string_view text) const { return token().text == text; }
    bool end() const { return token().kind == Kind::end; }

    bool trivia() const
    {
        return token().kind == Kind::lineComment || token().kind == Kind::blockComment
            || token().kind == Kind::directive;
    }

    void fillBefore(int row, int indent)
    {
        while (nextRow_ < row)
            widths_[nextRow_++] = indent;
    }

    void mark(int row, int indent)
    {
        fillBefore(row + 1, indent);
    }

    Token take(int indent)
    {
        const auto consumed = tokens_[cursor_++];
        mark(consumed.row, indent);
        if (consumed.lastRow > consumed.row)
            mark(consumed.lastRow, consumed.kind == Kind::blockComment ? widths_[consumed.row] + 1 : -1);
        return consumed;
    }

    void skipTrivia(int indent)
    {
        while (trivia())
        {
            fillBefore(token().row, indent);
            take(token().kind == Kind::directive ? 0 : indent);
        }
        fillBefore(token().row, indent);
    }

    void group(int indent, int depth)
    {
        if (depth >= maxDepth)
        {
            while (!end())
                take(indent);
            return;
        }
        const auto open = take(indent);
        const auto close = open.text == U"(" ? U")" : open.text == U"[" ? U"]" : U"}";
        const int base = widths_[open.row] < 0 ? static_cast<int>(content_[open.row] - starts_[open.row])
                                               : widths_[open.row];
        int body = base + step;
        if (!end() && !at(close) && !trivia() && token().row == open.row)
            body = base + static_cast<int>(token().offset - content_[open.row]);
        while (true)
        {
            skipTrivia(body);
            if (end())
            {
                mark(token().row, body);
                return;
            }
            if (at(close))
            {
                take(base);
                return;
            }
            if (at(U"}") || at(U")") || at(U"]"))
                return;
            if (at(U"(") || at(U"[") || at(U"{"))
                group(body, depth + 1);
            else
                take(body);
        }
    }

    void body(int indent, int depth)
    {
        skipTrivia(indent + step);
        statement(at(U"{") ? indent : indent + step, depth + 1);
    }

    void block(int indent, int depth)
    {
        take(indent);
        int bodyIndent = indent + step;
        while (true)
        {
            skipTrivia(bodyIndent);
            if (end())
            {
                mark(token().row, bodyIndent);
                return;
            }
            if (at(U"}"))
            {
                take(indent);
                return;
            }
            if (at(U"case") || at(U"default"))
            {
                take(indent + step);
                while (!end() && !at(U":") && !at(U"}") && !at(U";"))
                    take(indent + step);
                if (at(U":"))
                    take(indent + step);
                bodyIndent = indent + 2 * step;
            }
            else
                statement(bodyIndent, depth + 1);
        }
    }

    void simple(int indent, int depth)
    {
        bool first = true;
        bool afterGroup = false;
        bool initializer = false;
        while (true)
        {
            const int continuation = first ? indent : indent + step;
            skipTrivia(continuation);
            if (end())
            {
                mark(token().row, afterGroup ? indent : continuation);
                return;
            }
            if (at(U"}") && !first)
                return;
            if (at(U";") || at(U"}"))
            {
                take(indent);
                return;
            }
            if (at(U"{") && !initializer)
            {
                block(indent, depth);
                return;
            }
            initializer = initializer || at(U"=");
            afterGroup = at(U"(") || at(U"[") || at(U"{");
            if (afterGroup)
                group(continuation, depth + 1);
            else
                take(continuation);
            first = false;
        }
    }

    void statement(int indent, int depth)
    {
        skipTrivia(indent);
        if (depth >= maxDepth)
        {
            while (!end())
                take(indent);
        }
        if (end())
            mark(token().row, indent);
        else if (at(U"{"))
            block(indent, depth);
        else if (at(U"if") || at(U"for") || at(U"while") || at(U"switch"))
        {
            const auto head = take(indent);
            skipTrivia(indent);
            if (at(U"("))
                group(indent, depth + 1);
            body(indent, depth);
            if (head.text == U"if")
            {
                skipTrivia(indent);
                if (at(U"else"))
                {
                    const auto otherwise = take(indent);
                    if (at(U"if") && token().row == otherwise.row)
                        statement(indent, depth + 1);
                    else
                        body(indent, depth);
                }
            }
        }
        else if (at(U"do"))
        {
            take(indent);
            body(indent, depth);
            skipTrivia(indent);
            if (at(U"while"))
                simple(indent, depth);
        }
        else
            simple(indent, depth);
    }
};
