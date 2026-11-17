#pragma once

#include "display.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <optional>
#include <termios.h>
#include <vector>

struct TerminalEvent
{
    enum class Type { key, text, paste, press, release, motion, wheel, focus };

    Type type;
    Key key = Key::unknown;
    bool control = false;
    bool shift = false;
    Point point{};
    int axis = 0;
    int delta = 0;
    char32_t character = 0;
    std::string bytes{};
};

class Terminal final : public UiDisplay, public Painter
{
public:
    Terminal();
    ~Terminal() override;

    static bool available();
    bool running() const;
    void readInput();
    std::optional<TerminalEvent> event();
    bool updateSize();

    Painter &painter() override { return *this; }
    bool terminal() const override { return true; }
    Rect area(std::size_t index) const override;
    void begin(std::size_t) override;
    void present(std::size_t) override;

    void rect(Rect r, unsigned tint) override;
    void frame(Rect r, unsigned fill, unsigned border) override;
    void line(int x1, int y1, int x2, int y2, unsigned tint) override;
    void triangle(Point a, Point b, Point c, unsigned tint) override;
    void dotGrid(Rect) override {}
    void caret(Rect r, unsigned tint) override;
    int textWidth(std::string_view contents, int, bool) const override;
    void text(int x, int y, std::string_view contents, unsigned tint, int,
              bool, bool bold, int maxWidth) override;

private:
    struct Cell
    {
        char32_t glyph = U' ';
        unsigned foreground = color::text;
        unsigned background = color::background;
        bool bold = false;

        bool operator==(const Cell &) const = default;
    };

    static constexpr std::array signals_{SIGINT, SIGTERM, SIGHUP};
    std::array<struct sigaction, signals_.size()> previousSignals_{};
    std::size_t installedSignals_ = 0;
    termios saved_{};
    int columns_ = 0, rows_ = 0;
    bool alive_ = true;
    bool trueColor_ = false;
    bool indexedColor_ = false;
    bool pasting_ = false;
    bool discardPaste_ = false;
    std::string input_;
    std::chrono::steady_clock::time_point lastInput_{};
    std::vector<Cell> cells_;
    std::vector<Cell> previous_;

    void restoreSignals();
    Rect cellsIn(Rect r) const;
    Cell *cellAt(int column, int row);
    void glyph(int column, int row, char32_t letter, unsigned tint, bool bold = false);
    void colorSequence(std::string &output, unsigned tint, bool background) const;
    std::optional<TerminalEvent> sequence(std::string_view bytes);
};
