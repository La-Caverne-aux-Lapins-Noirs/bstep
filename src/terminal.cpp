#include "terminal.hpp"
#include "utf8.hpp"

#include <charconv>
#include <clocale>
#include <cstdlib>
#include <cwchar>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace
{

using Event = TerminalEvent;
using Clock = std::chrono::steady_clock;

constexpr std::string_view pasteEnd = "\x1b[201~";
constexpr std::size_t pasteLimit = 4 * 32768;
constexpr auto escapeDelay = std::chrono::milliseconds(50);
volatile std::sig_atomic_t interrupted = 0;

void stopTerminal(int signal) { interrupted = signal; }

bool send(std::string_view output)
{
    while (!output.empty())
    {
        const auto n = write(STDOUT_FILENO, output.data(), output.size());
        if (n > 0)
            output.remove_prefix(static_cast<std::size_t>(n));
        else if (n == 0 || errno != EINTR || interrupted)
            return false;
    }
    return true;
}

std::string_view environment(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string_view(value) : std::string_view{};
}

int floorCell(int value, int pitch)
{
    return (value < 0 ? value - pitch + 1 : value) / pitch;
}

Point cellPosition(int x, int y)
{
    return {floorCell(x, terminalCellWidth), floorCell(y, terminalCellHeight)};
}

Key letterKey(int lower) { return static_cast<Key>(static_cast<int>(Key::a) + lower - 'a'); }

Event keyEvent(Key key, int modifiers = 1)
{
    return {Event::Type::key, key, ((modifiers - 1) & 4) != 0, ((modifiers - 1) & 1) != 0};
}

Key cursorKey(char final)
{
    switch (final)
    {
    case 'A': return Key::up;
    case 'B': return Key::down;
    case 'C': return Key::right;
    case 'D': return Key::left;
    case 'F': return Key::end;
    case 'H': return Key::home;
    case 'Q': return Key::f2;
    case 'R': return Key::f3;
    case 'S': return Key::f4;
    case 'Z': return Key::tab;
    default: return Key::unknown;
    }
}

Key numberedKey(int number)
{
    switch (number)
    {
    case 1: case 7: return Key::home;
    case 3: return Key::del;
    case 4: case 8: return Key::end;
    case 5: return Key::pageUp;
    case 6: return Key::pageDown;
    case 12: return Key::f2;
    case 13: return Key::f3;
    case 14: return Key::f4;
    case 15: return Key::f5;
    case 17: return Key::f6;
    case 18: return Key::f7;
    case 19: return Key::f8;
    case 20: return Key::f9;
    case 21: return Key::f10;
    default: return Key::unknown;
    }
}

std::vector<int> parameters(std::string_view text)
{
    std::vector<int> values;
    while (!text.empty())
    {
        const auto sep = text.find(';');
        const auto digits = text.substr(0, sep);
        int value = 0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() || value < 0)
            return {};
        values.push_back(value);
        if (sep == text.npos)
            break;
        text.remove_prefix(sep + 1);
    }
    return values;
}

Key modifiedKey(int value)
{
    if (value >= 'a' && value <= 'z')
        return letterKey(value);
    if (value >= 'A' && value <= 'Z')
        return letterKey(value - 'A' + 'a');
    switch (value)
    {
    case 9: return Key::tab;
    case 13: return Key::enter;
    case 27: return Key::escape;
    case 127: return Key::backspace;
    default: return Key::unknown;
    }
}

std::optional<Event> mouseEvent(std::string_view params, char final, int columns, int rows)
{
    const auto numbers = parameters(params);
    if (numbers.size() != 3 || (final != 'M' && final != 'm')
        || numbers[1] < 1 || numbers[1] > columns || numbers[2] < 1 || numbers[2] > rows)
        return {};
    const int button = numbers.front();
    Event event{Event::Type::motion};
    event.point = {(numbers[1] - 1) * terminalCellWidth + terminalCellWidth / 2,
                   (numbers[2] - 1) * terminalCellHeight + terminalCellHeight / 2};
    event.shift = (button & 4) != 0;
    if (button & 64)
    {
        event.type = Event::Type::wheel;
        event.axis = (button & 2) != 0;
        event.delta = (button & 1) ? -1 : 1;
    }
    else if (final == 'm' && (button & 3) == 0)
        event.type = Event::Type::release;
    else if (!(button & 32) && (button & 3) == 0)
        event.type = Event::Type::press;
    return event;
}

int utf8Length(unsigned char byte)
{
    if (byte >= 0xc2 && byte <= 0xdf) return 2;
    if (byte >= 0xe0 && byte <= 0xef) return 3;
    if (byte >= 0xf0 && byte <= 0xf4) return 4;
    return 1;
}

} // namespace

bool Terminal::available()
{
    const auto term = environment("TERM");
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && !term.empty() && term != "dumb";
}

Terminal::Terminal()
{
    if (!available())
        throw std::runtime_error("Le mode TUI requiert un terminal interactif.");
    if (tcgetattr(STDIN_FILENO, &saved_) != 0)
        throw std::system_error(errno, std::generic_category(), "Lecture des paramètres du terminal");
    if (!std::setlocale(LC_CTYPE, "") || MB_CUR_MAX == 1)
        std::setlocale(LC_CTYPE, "C.UTF-8");
    const auto colors = environment("COLORTERM");
    trueColor_ = colors == "truecolor" || colors == "24bit";
    indexedColor_ = environment("TERM").find("256color") != std::string_view::npos;
    updateSize();

    interrupted = 0;
    struct sigaction handler{};
    handler.sa_handler = stopTerminal;
    sigemptyset(&handler.sa_mask);
    for (const int signal : signals_)
    {
        if (sigaction(signal, &handler, &previousSignals_[installedSignals_]) != 0)
        {
            const auto error = errno;
            restoreSignals();
            throw std::system_error(error, std::generic_category(), "Gestion des signaux du terminal");
        }
        ++installedSignals_;
    }

    auto raw = saved_;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
    {
        const auto error = errno;
        restoreSignals();
        throw std::system_error(error, std::generic_category(), "Activation du mode terminal");
    }
    alive_ = send("\x1b[?1049h\x1b[?25l\x1b[?1003h\x1b[?1006h\x1b[?1004h\x1b[?2004h");
}

Terminal::~Terminal()
{
    send("\x1b[0m\x1b[?2004l\x1b[?1004l\x1b[?1006l\x1b[?1003l\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    restoreSignals();
}

void Terminal::restoreSignals()
{
    while (installedSignals_ > 0)
    {
        --installedSignals_;
        sigaction(signals_[installedSignals_], &previousSignals_[installedSignals_], nullptr);
    }
}

bool Terminal::running() const { return alive_ && !interrupted; }

void Terminal::readInput()
{
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const auto ready = poll(&input, 1, 28);
    if (ready < 0 && errno != EINTR)
        alive_ = false;
    if (ready <= 0)
        return;
    if (!(input.revents & POLLIN))
    {
        alive_ = false;
        return;
    }
    char bytes[4096];
    const auto size = read(STDIN_FILENO, bytes, sizeof bytes);
    if (size > 0)
    {
        input_.append(bytes, static_cast<std::size_t>(size));
        lastInput_ = Clock::now();
    }
    else if (size == 0 || (errno != EINTR && errno != EAGAIN))
        alive_ = false;
}

std::optional<TerminalEvent> Terminal::sequence(std::string_view bytes)
{
    const auto final = bytes.back();
    const auto param = bytes.substr(0, bytes.size() - 1);
    if (param.starts_with('<'))
        return mouseEvent(param.substr(1), final, columns_, rows_);
    if (param.empty() && (final == 'I' || final == 'O'))
        return Event{Event::Type::focus};
    if (param.empty() && final == 'Z')
        return keyEvent(Key::tab, 2);

    const auto values = parameters(param);
    const int first = values.empty() ? 0 : values.front();
    const int modifiers = values.size() >= 2 ? std::max(1, values[1]) : 1;
    if (final == '~' && first == 27 && values.size() == 3)
        return keyEvent(modifiedKey(values[2]), modifiers);
    if (final == 'u' && !values.empty())
    {
        const auto key = modifiedKey(first);
        if (first >= 32 && first != 127 && first <= 0x10ffff && modifiers <= 2)
        {
            Event event{Event::Type::text};
            event.character = static_cast<char32_t>(first);
            return event;
        }
        return keyEvent(key, modifiers);
    }
    const Key key = final == '~' ? numberedKey(first) : cursorKey(final);
    return key == Key::unknown ? std::nullopt : std::optional(keyEvent(key, modifiers));
}

std::optional<TerminalEvent> Terminal::event()
{
    while (!input_.empty())
    {
        if (pasting_)
        {
            const auto end = input_.find(pasteEnd);
            if (end == input_.npos)
            {
                if (input_.size() > pasteLimit + pasteEnd.size())
                {
                    discardPaste_ = true;
                    input_.erase(0, input_.size() - pasteEnd.size() + 1);
                }
                return {};
            }
            Event event{Event::Type::paste};
            if (!discardPaste_ && end <= pasteLimit)
                event.bytes = input_.substr(0, end);
            input_.erase(0, end + pasteEnd.size());
            pasting_ = discardPaste_ = false;
            if (!event.bytes.empty())
                return event;
            continue;
        }

        const auto first = static_cast<unsigned char>(input_.front());
        if (first == 27)
        {
            if (input_.size() > 1 && (input_[1] == '[' || input_[1] == 'O'))
            {
                auto final = input_.find_first_not_of(" !\"#$%&'()*+,-./0123456789:;<=>?", 2);
                if (final == input_.npos && input_.size() < 64 && Clock::now() - lastInput_ < escapeDelay)
                    return {};
                if (final != input_.npos && final < 64)
                {
                    const auto bytes = input_.substr(2, final - 1);
                    input_.erase(0, final + 1);
                    if (bytes == "200~")
                        pasting_ = true;
                    else if (auto result = sequence(bytes))
                        return result;
                    continue;
                }
                input_.erase(0, 2);
                continue;
            }
            if (input_.size() == 1 && Clock::now() - lastInput_ < escapeDelay)
                return {};
            input_.erase(0, 1);
            return keyEvent(Key::escape);
        }
        if (first < 32 || first == 127)
        {
            input_.erase(0, 1);
            if (first == 13 || first == 10) return keyEvent(Key::enter);
            if (first == 9) return keyEvent(Key::tab);
            if (first == 8 || first == 127) return keyEvent(Key::backspace);
            if (first >= 1 && first <= 26) return keyEvent(letterKey('a' + first - 1), 5);
            continue;
        }

        const auto length = static_cast<std::size_t>(utf8Length(first));
        std::size_t available = 1;
        while (available < length && available < input_.size()
               && (static_cast<unsigned char>(input_[available]) & 0xc0) == 0x80)
            ++available;
        if (available < length && available == input_.size() && Clock::now() - lastInput_ < escapeDelay)
            return {};
        const auto text = decodeUtf8(std::string_view(input_).substr(0, available));
        input_.erase(0, available);
        Event event{Event::Type::text};
        event.character = text.front();
        return event;
    }
    return {};
}

bool Terminal::updateSize()
{
    winsize size{};
    const bool readSize = ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0;
    const int columns = readSize && size.ws_col ? size.ws_col : 80;
    const int rows = readSize && size.ws_row ? size.ws_row : 24;
    if (columns == columns_ && rows == rows_)
        return false;
    columns_ = columns;
    rows_ = rows;
    previous_.clear();
    return true;
}

Rect Terminal::area(std::size_t index) const
{
    return index ? Rect{} : Rect{0, 0, columns_ * terminalCellWidth, rows_ * terminalCellHeight};
}

void Terminal::begin(std::size_t)
{
    cells_.assign(static_cast<std::size_t>(columns_) * rows_, {});
}

Rect Terminal::cellsIn(Rect r) const
{
    const auto a = cellPosition(r.x + (terminalCellWidth - 1) / 2, r.y + (terminalCellHeight - 1) / 2);
    const auto b = cellPosition(r.right() + (terminalCellWidth - 1) / 2, r.bottom() + (terminalCellHeight - 1) / 2);
    // Keep geometric edges here. Individual cells are clipped at the screen.
    return {a.x, a.y, std::max(0, b.x - a.x), std::max(0, b.y - a.y)};
}

Terminal::Cell *Terminal::cellAt(int column, int row)
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
        return nullptr;
    return &cells_[static_cast<std::size_t>(row) * columns_ + column];
}

void Terminal::glyph(int column, int row, char32_t letter, unsigned tint, bool bold)
{
    if (auto *cell = cellAt(column, row))
    {
        cell->glyph = letter;
        cell->foreground = tint;
        cell->bold = bold;
    }
}

void Terminal::rect(Rect r, unsigned tint)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    if (r.h <= 3 && r.w >= terminalCellWidth)
        line(r.x, r.y, r.right() - 1, r.y, tint);
    else if (r.w <= 3 && r.h >= terminalCellHeight)
        line(r.x, r.y, r.x, r.bottom() - 1, tint);
    else
    {
        const auto box = cellsIn(r);
        if (box.w == 0 || box.h == 0)
        {
            const auto p = cellPosition(r.middle(), r.y + r.h / 2);
            glyph(p.x, p.y, U'▪', tint);
            return;
        }
        for (int y = std::max(0, box.y); y < std::min(rows_, box.bottom()); ++y)
            for (int x = std::max(0, box.x); x < std::min(columns_, box.right()); ++x)
                *cellAt(x, y) = {U' ', color::text, tint, false};
    }
}

void Terminal::frame(Rect r, unsigned fill, unsigned border)
{
    const auto box = cellsIn(r);
    for (int y = std::max(0, box.y); y < std::min(rows_, box.bottom()); ++y)
        for (int x = std::max(0, box.x); x < std::min(columns_, box.right()); ++x)
        {
            char32_t mark = U' ';
            const bool left = x == box.x, right = x == box.right() - 1;
            if (box.h == 1)
                mark = left ? U'[' : right ? U']' : U' ';
            else if (y == box.y)
                mark = left ? U'┌' : right ? U'┐' : U'─';
            else if (y == box.bottom() - 1)
                mark = left ? U'└' : right ? U'┘' : U'─';
            else if (left || right)
                mark = U'│';
            *cellAt(x, y) = {mark, border, fill, false};
        }
}

void Terminal::line(int x1, int y1, int x2, int y2, unsigned tint)
{
    auto from = cellPosition(x1, y1);
    const auto to = cellPosition(x2, y2);
    const int dx = std::abs(to.x - from.x), dy = -std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1, sy = from.y < to.y ? 1 : -1;
    const auto mark = from.y == to.y ? U'─' : from.x == to.x ? U'│' : U'·';
    int error = dx + dy;
    while (true)
    {
        glyph(from.x, from.y, mark, tint);
        if (from.x == to.x && from.y == to.y)
            break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; from.x += sx; }
        if (twice <= dx) { error += dx; from.y += sy; }
    }
}

void Terminal::triangle(Point a, Point b, Point c, unsigned tint)
{
    const auto tip = cellPosition(c.x, c.y);
    const auto mark = a.x == b.x ? (c.x >= a.x ? U'▶' : U'◀') : (c.y >= a.y ? U'▼' : U'▲');
    glyph(tip.x, tip.y, mark, tint);
}

void Terminal::caret(Rect r, unsigned tint)
{
    const auto p = cellPosition(r.x, r.y);
    if (auto *cell = cellAt(p.x, p.y))
    {
        cell->background = tint;
        cell->foreground = color::background;
    }
}

int Terminal::textWidth(std::string_view contents, int, bool) const
{
    return static_cast<int>(decodeUtf8(contents.substr(0, contents.find('\n'))).size()) * terminalCellWidth;
}

void Terminal::text(int x, int y, std::string_view contents, unsigned tint, int,
                    bool, bool bold, int maxWidth)
{
    auto p = cellPosition(x, y);
    const auto right = std::min(columns_, p.x + maxWidth / terminalCellWidth);
    if (p.y < 0 || p.y >= rows_ || p.x >= right)
        return;
    for (const char32_t letter : decodeUtf8(contents))
    {
        if (letter == U'\n' || p.x >= right)
            break;
        // The shared editor uses one column per code point.
        const auto shown = wcwidth(static_cast<wchar_t>(letter)) == 1 ? letter : U'�';
        glyph(p.x++, p.y, shown, tint, bold);
    }
}

void Terminal::colorSequence(std::string &output, unsigned tint, bool background) const
{
    const int r = tint & 255, g = (tint >> 8) & 255, b = (tint >> 16) & 255;
    if (trueColor_)
    {
        output += background ? ";48;2;" : ";38;2;";
        output += std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b);
    }
    else if (indexedColor_)
    {
        const auto cube = [](int value) { return value < 48 ? 0 : value < 115 ? 1 : (value - 115) / 40 + 2; };
        const int index = r == g && g == b && r >= 8 && r <= 238
            ? 232 + (r - 8) / 10 : 16 + cube(r) * 36 + cube(g) * 6 + cube(b);
        output += background ? ";48;5;" : ";38;5;";
        output += std::to_string(index);
    }
    else
    {
        constexpr std::array colors{0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
                                    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff};
        int closest = 0, distance = 3 * 256 * 256;
        for (int i = 0; i < static_cast<int>(colors.size()); ++i)
        {
            const int dr = r - (colors[i] >> 16), dg = g - ((colors[i] >> 8) & 255), db = b - (colors[i] & 255);
            const int d = dr * dr + dg * dg + db * db;
            if (d < distance) { closest = i; distance = d; }
        }
        output += ";" + std::to_string((background ? 40 : 30) + (closest % 8) + (closest >= 8 ? 60 : 0));
    }
}

void Terminal::present(std::size_t)
{
    const bool redraw = previous_.size() != cells_.size();
    std::string output = redraw ? "\x1b[0m\x1b[2J" : "";
    std::optional<Cell> style;
    for (int y = 0; y < rows_; ++y)
    {
        bool position = true;
        for (int x = 0; x < columns_; ++x)
        {
            const auto index = static_cast<std::size_t>(y) * columns_ + x;
            const auto &cell = cells_[index];
            if (!redraw && cell == previous_[index])
            {
                position = true;
                continue;
            }
            if (position)
            {
                output += "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
                position = false;
            }
            if (!style || style->foreground != cell.foreground || style->background != cell.background
                || style->bold != cell.bold)
            {
                output += cell.bold ? "\x1b[0;1" : "\x1b[0";
                colorSequence(output, cell.foreground, false);
                colorSequence(output, cell.background, true);
                output += 'm';
                style = cell;
            }
            output += encodeUtf8(std::u32string_view(&cell.glyph, 1));
        }
    }
    if (!output.empty())
        alive_ = alive_ && send(output);
    cells_.swap(previous_);
}
