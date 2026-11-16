#include "cmini.hpp"
#include "code_view.hpp"
#include "course_view.hpp"
#include "desktop.hpp"
#include "draw.hpp"
#include "editor_keys.hpp"
#include "panels.hpp"
#include "source_directory.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <system_error>
#include <limits>

#include <unistd.h>

namespace
{

using Clock = std::chrono::steady_clock;
using cmini::Address;
using cmini::Frame;
using cmini::Object;

constexpr int cellPitch = 81;
constexpr int fileRows = 5;
constexpr auto noWindow = std::numeric_limits<std::size_t>::max();

enum class Focus { none, editor, stdinBox, command, filename };
enum class Browser { idle, open, save, folder };

namespace commandColor
{
constexpr unsigned file = vt::palette[12];
constexpr unsigned playback = vt::palette[10];
constexpr unsigned step = vt::palette[14];
constexpr unsigned debug = vt::palette[13];
constexpr unsigned terminal = vt::palette[11];
constexpr unsigned close = vt::palette[9];
constexpr unsigned course = vt::palette[14];
}

struct Shortcut
{
    std::string_view key, label;
    unsigned accent;
};

constexpr std::array shortcuts{
    Shortcut{"F5", "Lecture/pause", commandColor::playback},
    Shortcut{"F6", "Repartir", commandColor::step},
    Shortcut{"F7", "Retour", commandColor::step},
    Shortcut{"F8", "Pas suivant", commandColor::step},
    Shortcut{"F9", "Débogage", commandColor::debug},
    Shortcut{"F10", "Rouvrir les fenêtres", color::rule},
    Shortcut{"Ctrl+Q", "Quitter", commandColor::close},
    Shortcut{"Ctrl+O", "Ouvrir", commandColor::file},
    Shortcut{"Ctrl+S", "Enregistrer", commandColor::file},
    Shortcut{"Ctrl+Maj+S", "Enregistrer sous", commandColor::file}
};

struct Rule
{
    int id;
    std::string source;
    cmini::Expr expression;
};

struct TerminalLayout
{
    Rect merge, out, err, in, edit, eof;
};

struct ToolsLayout
{
    Rect frame, close, fileTab, debugTab, explorer, shell, watches;
    bool tabbed = false;
};

int windowMargin(Rect area)
{
    return std::min(area.w >= 1000 ? 24 : 12, area.w / 12);
}

struct Document
{
    std::string displayName;
    Editor editor;
    std::optional<std::filesystem::path> path;

    Document(std::string_view name, std::string_view code)
        : displayName(name), editor(code)
    {
    }
};

struct Node
{
    const Object *object;
    int index;
    Rect rect;
    bool terminal;
    std::uint64_t objectId;

    Address address() const { return {objectId, index}; }
};

struct ArrayRow
{
    const Object *object;
    int y;
};

std::string fitMono(std::string_view text, int columns)
{
    auto chars = decodeUtf8(text);
    if (columns <= 0)
        return {};
    if (static_cast<int>(chars.size()) > columns)
    {
        chars.resize(columns);
        chars.back() = U'…';
    }
    return encodeUtf8(chars);
}

class App
{
public:
    App(Desktop &desktop, const fs::path &assets, fs::path sourceRoot, const fs::path &file,
        fs::path courseRoot, bool startCourse)
        : desktop_(desktop), draw_(assets), source_(std::move(sourceRoot)), course_(std::move(courseRoot))
    {
        docs_.emplace_back("programme.c", "");
        layout();
        if (!file.empty())
            openSource(file);
        else
            prepare();
        if (startCourse)
            setStudy(true, true);
    }

    t_bunny_response tick()
    {
        if (restoreWindows_)
        {
            desktop_.reopen();
            restoreWindows_ = false;
            toolsOverlay_ = toolsOverlay_ && !desktop_.open(3);
            layout();
        }
        for (const auto index : closing_)
        {
            desktop_.close(index);
            hoverWindow_ = noWindow;
            if (index == 3)
            {
                if (browser_ != Browser::idle)
                    toolsOverlay_ = true;
                else if (focus_ == Focus::command)
                    setFocus(Focus::none);
            }
            layout();
        }
        closing_.clear();

        const auto now = Clock::now();
        if (studying_)
        {
            if (course_.tick())
                invalidate();
        }
        else if (playing_ && now >= nextTick_)
        {
            advance(true);
            nextTick_ = now + periods_[speed_];
        }

        const auto blink = std::chrono::duration_cast<std::chrono::milliseconds>(now - caretTime_).count() / 480;
        const bool visible = editing_ && blink % 2 == 0;
        if (visible != caretVisible_)
        {
            caretVisible_ = visible;
            invalidate();
        }
        if (pendingDraw_)
        {
            draw();
            pendingDraw_ = false;
        }
        return GO_ON;
    }

    t_bunny_response close(const t_bunny_window *window)
    {
        const auto index = desktop_.index(window);
        if (index == 0)
            return EXIT_ON_CROSS;
        if (index < desktop_.count())
            closing_.push_back(index);
        return GO_ON;
    }

    t_bunny_response moved(const t_bunny_window *window)
    {
        const auto index = desktop_.index(window);
        if (index >= desktop_.count())
            return GO_ON;
        hoverWindow_ = index;
        hover_ = *bunny_get_mouse_position();
        if (dragging_ && index == 0)
            placeMouse(hover_, true);
        invalidate();
        return GO_ON;
    }

    t_bunny_response resize(const t_bunny_window *window, const t_bunny_position &size)
    {
        if (auto *target = desktop_.at(desktop_.index(window)); target && size.x > 0 && size.y > 0
            && (target->buffer.width != size.x
                || target->buffer.height != size.y))
        {
            // LibLapin synchronizes the buffer dimensions and the SFML view here.
            bunny_resize_window(target, size);
        }
        layout();
        return GO_ON;
    }

    t_bunny_response inputEvent(const t_bunny_event &event)
    {
        switch (event.type)
        {
        case BET_LOST_FOCUS:
        case BET_GAIN_FOCUS:
            leftShift_ = rightShift_ = rightAlt_ = controlText_ = false;
            dragging_ = false;
            kills_.breakSequence();
            return GO_ON;
        case BET_TEXT_ENTERED:
            return text(event.text.unicode);
        case BET_KEY_PRESSED:
        case BET_KEY_RELEASED:
        {
            const auto sym = event.key.sym;
            const bool down = event.type == BET_KEY_PRESSED;
            if (sym == BKS_LSHIFT) leftShift_ = down;
            if (sym == BKS_RSHIFT) rightShift_ = down;
            if (sym == BKS_RALT) rightAlt_ = down;

            // X11 reports modifier states with the event. A release outside this window
            // is never delivered here, so the cached LibLapin keyboard array can be stale.
            const bool releasedControl = !down && (sym == BKS_LCONTROL || sym == BKS_RCONTROL);
            controlText_ = event.key.control && !rightAlt_ && !releasedControl;
            return down ? key(sym, controlText_, event.key.shift) : GO_ON;
        }
        default:
            return GO_ON;
        }
    }

    t_bunny_response key(t_bunny_keysym key, bool ctrl, bool shift)
    {
        if (!(ctrl && key == BKS_K) && !modifierKey(key))
            kills_.breakSequence();
        if (ctrl && key == BKS_Q)
            return EXIT_ON_SUCCESS;
        if (studying_)
        {
            const auto index = desktop_.index(bunny_get_window());
            if (index <= 1)
                course_.key(key, index == 1, ctrl, shift, kills_);
            invalidate();
            return GO_ON;
        }
        if (key == BKS_ESCAPE)
        {
            if (browser_ != Browser::idle)
                endExplorer();
            else if (toolsOverlay_ || fileMenuOpen_)
            {
                toolsOverlay_ = false;
                fileMenuOpen_ = false;
                setFocus(Focus::none);
            }
            else
            {
                playing_ = false;
                resumeInput_ = false;
                setFocus(Focus::none);
            }
            invalidate();
            return GO_ON;
        }
        switch (key)
        {
        case BKS_F5: togglePlay(); return GO_ON;
        case BKS_F6: restart(); return GO_ON;
        case BKS_F7: goBack(); return GO_ON;
        case BKS_F8: goForward(); return GO_ON;
        case BKS_F9: openTools(); return GO_ON;
        case BKS_F10: restoreWindows_ = true; return GO_ON;
        default: break;
        }
        if (ctrl && key == BKS_O)
        {
            startExplorer(Browser::open);
            return GO_ON;
        }
        if (ctrl && key == BKS_S)
        {
            if (shift) startExplorer(Browser::save);
            else save();
            return GO_ON;
        }
        if (ctrl && key == BKS_D && focus_ == Focus::stdinBox)
        {
            submitInput(true);
            return GO_ON;
        }
        if (auto *field = inputField())
        {
            if (key == BKS_RETURN)
            {
                if (focus_ == Focus::command) executeCommand();
                else if (focus_ == Focus::filename) acceptPath();
                else submitInput(false);
            }
            else if (focus_ == Focus::filename && !ctrl && (key == BKS_UP || key == BKS_DOWN))
                selectExplorer(key == BKS_UP ? -1 : 1);
            else if (ctrl && key == BKS_V)
            {
                const auto bytes = sf::Clipboard::getString().toUtf8();
                field->insert(decodeUtf8(std::string(bytes.begin(), bytes.end())));
            }
            else
                field->key(key, ctrl);
            invalidate();
            return GO_ON;
        }
        if (!editing_)
            return GO_ON;

        auto &ed = doc().editor;
        const auto revision = ed.revision();
        if (!editKey(ed, key, ctrl, shift, codeRows, true, kills_))
            return GO_ON;
        afterEdit(ed.revision() != revision);
        return GO_ON;
    }

    t_bunny_response text(std::uint32_t unicode)
    {
        if (unicode < 32 || unicode == 127 || unicode > 0x10ffff
            || (unicode >= 0xd800 && unicode <= 0xdfff) || controlText_)
            return GO_ON;
        kills_.breakSequence();
        const char32_t character = unicode;
        if (studying_)
        {
            const auto index = desktop_.index(bunny_get_window());
            if (index <= 1)
                course_.text(character, index == 1);
            invalidate();
        }
        else if (auto *field = inputField())
        {
            field->insert(std::u32string_view(&character, 1));
            invalidate();
        }
        else if (editing_ && doc().editor.type(character))
            afterEdit(true);
        return GO_ON;
    }

    t_bunny_response click(t_bunny_event_state state, t_bunny_mouse_button button)
    {
        if (button != BMB_LEFT)
            return GO_ON;
        if (state == GO_UP)
        {
            dragging_ = false;
            return GO_ON;
        }
        kills_.breakSequence();
        const auto *window = bunny_get_window();
        const auto index = desktop_.index(window);
        if (index >= desktop_.count())
            return GO_ON;
        const auto p = *bunny_get_mouse_position();
        hoverWindow_ = index;
        hover_ = p;
        if (index == 0 && quitButton.contains(p))
            return EXIT_ON_SUCCESS;
        if (index != 0 && auxiliaryClose(index).contains(p))
            return close(window);
        if (index == 0 && modeButton.contains(p))
            setStudy(!studying_);
        else if (studying_)
        {
            if (index <= 1)
                course_.click(p, index == 1, leftShift_ || rightShift_);
        }
        else if (index == 3 || (index == 0 && toolsOverlay_))
            toolClick(p, index != 0);
        else if (index == 2)
            terminalClick(p, fullTerminal(auxiliaryBody(2)));
        else if (index == 1)
            memoryClick(p);
        else if (index == 0)
            editorClick(p);
        invalidate();
        return GO_ON;
    }

    t_bunny_response wheel(int wheel, int delta)
    {
        if (!delta)
            return GO_ON;
        const auto *window = bunny_get_window();
        const auto index = desktop_.index(window);
        if (index >= desktop_.count())
            return GO_ON;
        const auto p = *bunny_get_mouse_position();
        if (studying_)
        {
            if (index <= 1)
                course_.wheel(p, index == 1, wheel, delta);
            invalidate();
            return GO_ON;
        }
        const bool tools = index == 3 || (index == 0 && toolsOverlay_);
        if (tools)
        {
            const auto panes = toolsLayout(index == 3);
            if (browser_ != Browser::idle && (!panes.tabbed || !debugTools_) && panes.explorer.contains(p))
                explorerTop_ = std::clamp(explorerTop_ - delta * 3, 0, maxExplorerTop());
        }
        else if ((index == 1 || (index == 0 && showingInMain(View::memory))) && memoryView.contains(p))
            memoryScroll_ = std::clamp(memoryScroll_ - delta * 54, 0, memoryMaxScroll_);
        else if ((index == 2 && fullTerminal(auxiliaryBody(2)).contains(p))
                 || (index == 0 && showingInMain(View::terminal) && mainTerminal().contains(p)))
        {
            const auto box = index == 2 ? fullTerminal(auxiliaryBody(2)) : mainTerminal();
            const auto panes = terminalLayout(box, terminalMerged_);
            auto &scroll = panes.err.contains(p) ? stderrScroll_ : terminalScroll_;
            scroll = std::clamp(scroll + delta * 3, 0, 6500);
        }
        else if (index == 0 && showingInMain(View::editor) && codeView.contains(p))
        {
            if (wheel)
                doc().editor.left = std::clamp(doc().editor.left - delta * 4, 0, 32768 - codeColumns);
            else
                doc().editor.top = std::clamp(doc().editor.top - delta * 3, 0, maxCodeTop());
        }
        invalidate();
        return GO_ON;
    }

private:
    Desktop &desktop_;
    Painter draw_;
    SourceDirectory source_;
    CourseView course_;
    KillBuffer kills_;
    Rect mainArea{}, codePanel{}, codeView{}, memoryPanel{}, memoryView{};
    Rect compactCompile{}, compactIo{}, transport{}, statusArea{};
    Rect titleBar{}, quitButton{}, fileButton{}, toolsButton{}, modeButton{}, fileMenu{};
    std::array<Rect, shortcuts.size()> shortcutKeys{};
    Rect resetButton{}, backButton{}, nextButton{}, playButton{}, speedButton{};
    std::array<Rect, 3> viewTabs{};
    bool narrow_ = false;
    bool studying_ = false;
    bool debugTools_ = false;
    View mainView_ = View::editor;
    int codeX = 0, codeRows = 0, codeColumns = 0, arrayColumns = 1;
    std::vector<std::size_t> closing_;
    std::size_t hoverWindow_ = noWindow;
    bool restoreWindows_ = false;
    Focus focus_ = Focus::none;
    bool resumeInput_ = false;
    bool terminalMerged_ = false;
    int terminalScroll_ = 0, stderrScroll_ = 0;
    LineInput stdinLine_;
    LineInput commandLine_;
    LineInput pathLine_;
    bool fileMenuOpen_ = false;
    bool toolsOverlay_ = false;
    Browser browser_ = Browser::idle;
    Browser parentBrowser_ = Browser::idle;
    int explorerTop_ = 0;
    int explorerSelection_ = -1;
    std::optional<fs::path> replacing_;
    std::string browserNote_;
    std::string shellLog_;
    std::vector<Rule> watches_;
    std::vector<Rule> breaks_;
    int nextRule_ = 1;
    std::string breakpoint_;
    std::vector<Document> docs_;
    std::unique_ptr<cmini::Machine> machine_;
    std::optional<cmini::Diagnostic> compileError_;
    std::optional<Address> selected_;
    std::size_t tab_ = 0;
    std::size_t compiledRevision_ = 0;
    std::size_t speed_ = 0;
    std::string fileNote_;
    std::vector<Node> nodes_;
    std::vector<ArrayRow> arrays_;
    int memoryScroll_ = 0;
    int memoryMaxScroll_ = 0;
    t_bunny_position hover_{-1, -1};
    bool playing_ = false;
    bool editing_ = false;
    bool dragging_ = false;
    bool leftShift_ = false;
    bool rightShift_ = false;
    bool rightAlt_ = false;
    bool controlText_ = false;
    bool pendingDraw_ = true;
    bool caretVisible_ = false;
    Clock::time_point caretTime_ = Clock::now();
    Clock::time_point nextTick_ = caretTime_;

    static constexpr std::array periods_{
        std::chrono::milliseconds(650), std::chrono::milliseconds(330), std::chrono::milliseconds(1300)
    };

    Document &doc() { return docs_[tab_]; }
    const Document &doc() const { return docs_[tab_]; }
    bool modified() const { return compiledRevision_ != doc().editor.revision(); }
    void invalidate() { pendingDraw_ = true; }

    void setStudy(bool enabled, bool openSingle = false)
    {
        studying_ = enabled;
        if (enabled)
        {
            playing_ = false;
            resumeInput_ = false;
            restoreWindows_ = false;
            fileMenuOpen_ = false;
            endExplorer();
            course_.enter(openSingle);
        }
        else
            course_.leave();
        layout();
    }

    bool prepare()
    {
        playing_ = false;
        resumeInput_ = false;
        breakpoint_.clear();
        stdinLine_.clear();
        terminalScroll_ = 0;
        stderrScroll_ = 0;
        fileNote_.clear();
        selected_.reset();
        memoryScroll_ = 0;
        compileError_.reset();
        compiledRevision_ = doc().editor.revision();
        machine_.reset();
        try
        {
            const auto code = doc().editor.source();
            if (!trim(code).empty())
                machine_ = std::make_unique<cmini::Machine>(cmini::compile(code));
        }
        catch (const cmini::Diagnostic &error)
        {
            compileError_ = error;
        }
        invalidate();
        return !!machine_;
    }

    const Frame *frame() const
    {
        return !modified() && machine_ ? &machine_->frame() : nullptr;
    }

    bool ready()
    {
        if (modified())
            return prepare();
        return !!machine_;
    }

    void restart()
    {
        setFocus(Focus::none);
        prepare();
        follow();
    }

    bool testRule(const Rule &rule) const
    {
        const auto value = machine_->inspect(rule.expression, false);
        return value && value->truth;
    }

    void advance(bool continuous = false)
    {
        breakpoint_.clear();
        if (!ready())
        {
            playing_ = false;
            follow();
            return;
        }
        std::vector<bool> before;
        if (continuous)
            for (const auto &rule : breaks_)
                before.push_back(testRule(rule));

        if (!machine_->next())
        {
            playing_ = false;
            if (machine_->waiting())
            {
                resumeInput_ = continuous;
                setFocus(Focus::stdinBox);
                toolsOverlay_ = false;
                if (narrow_ && !desktop_.open(2))
                    mainView_ = View::terminal;
            }
        }
        else
        {
            terminalScroll_ = 0;
            stderrScroll_ = 0;
            resumeInput_ = false;
            if (continuous)
                for (std::size_t i = 0; i < breaks_.size(); ++i)
                    if (testRule(breaks_[i]) && !before[i])
                    {
                        breakpoint_ = "Arrêt #" + std::to_string(breaks_[i].id) + " · " + breaks_[i].source;
                        shellPrint(breakpoint_);
                        playing_ = false;
                        break;
                    }
        }
        const auto &state = machine_->frame();
        if (state.finished || state.error)
            playing_ = false;
        follow();
    }

    void goBack()
    {
        playing_ = false;
        resumeInput_ = false;
        breakpoint_.clear();
        setFocus(Focus::none);
        if (!modified() && machine_)
            machine_->previous();
        follow();
    }

    void goForward()
    {
        playing_ = false;
        setFocus(Focus::none);
        advance();
    }

    void togglePlay()
    {
        setFocus(Focus::none);
        if (playing_)
            playing_ = false;
        else if (ready())
        {
            if (machine_->frame().finished && machine_->frame().steps > 0)
            {
                machine_->reset();
                selected_.reset();
                memoryScroll_ = 0;
                stdinLine_.clear();
            }
            if (!machine_->frame().finished && !machine_->frame().error)
            {
                playing_ = true;
                advance(true);
                nextTick_ = Clock::now() + periods_[speed_];
            }
        }
        follow();
    }

    void follow()
    {
        cmini::Position position{0, 0};
        if (!modified() && machine_)
            position = machine_->nextPosition();
        else if (compileError_)
            position = compileError_->where;
        const int line = position.line;
        if (line > 0)
        {
            auto &ed = doc().editor;
            if (line - 1 < ed.top)
                ed.top = line - 1;
            if (line > ed.top + codeRows)
                ed.top = std::max(0, line - codeRows + 2);
            ed.top = std::min(ed.top, maxCodeTop());
            const int col = position.column - 1;
            if (col < ed.left)
                ed.left = std::max(0, col - codeColumns / 3);
            else if (col >= ed.left + codeColumns)
                ed.left = std::max(0, col - codeColumns / 2);
        }
        invalidate();
    }

    int maxCodeTop() const
    {
        return std::max(0, doc().editor.lines() - codeRows);
    }

    void afterEdit(bool sourceChanged)
    {
        playing_ = false;
        if (sourceChanged)
        {
            compileError_.reset();
            fileNote_.clear();
        }

        auto &ed = doc().editor;
        const auto [row, col] = ed.location();
        ed.top = std::clamp(ed.top, 0, maxCodeTop());
        if (row < ed.top)
            ed.top = row;
        if (row >= ed.top + codeRows)
            ed.top = row - codeRows + 1;
        if (col < ed.left)
            ed.left = col;
        if (col >= ed.left + codeColumns)
            ed.left = col - codeColumns + 1;
        caretTime_ = Clock::now();
        invalidate();
    }

    void placeMouse(t_bunny_position p, bool extend)
    {
        auto &ed = doc().editor;
        const auto row = std::clamp((p.y - codeView.y) / codePitch, 0, std::max(0, codeRows - 1)) + ed.top;
        const auto col = std::max(0, (p.x - codeX + glyphWidth / 2) / glyphWidth) + ed.left;
        const auto target = ed.at(row, col);
        if (target != ed.cursor() || !extend)
        {
            ed.place(target, extend);
            afterEdit(false);
        }
    }

    Rect windowArea(std::size_t index) const
    {
        const auto *window = desktop_.at(index);
        return window ? Rect{0, 0, static_cast<int>(window->buffer.width),
                              static_cast<int>(window->buffer.height)} : Rect{};
    }

    Rect auxiliaryBody(std::size_t index) const
    {
        const auto area = windowArea(index);
        const int margin = windowMargin(area);
        return inset(area, margin, 65, margin);
    }

    Rect auxiliaryClose(std::size_t index) const
    {
        const auto area = windowArea(index);
        return {area.right() - windowMargin(area) - 38, 14, 38, 29};
    }

    bool showingInMain(View view) const
    {
        const bool detached = view != View::editor && desktop_.open(static_cast<std::size_t>(view));
        return !detached && (!narrow_ || mainView_ == view);
    }

    int placeShortcuts(Rect space)
    {
        constexpr int badgeHeight = 19;
        int x = space.x;
        int y = space.y;
        for (std::size_t i = 0; i < shortcuts.size(); ++i)
        {
            const auto &shortcut = shortcuts[i];
            const int keyWidth = Painter::textWidth(shortcut.key, 12, true) + 12;
            const int itemWidth = keyWidth + 7 + Painter::textWidth(shortcut.label, 13);
            if (x > space.x && x + itemWidth > space.right())
            {
                x = space.x;
                y += badgeHeight + 6;
            }
            shortcutKeys[i] = {x, y, keyWidth, badgeHeight};
            x += itemWidth + 9;
        }
        return y + badgeHeight;
    }

    void layout()
    {
        const auto window = windowArea(0);
        const int margin = windowMargin(window);
        narrow_ = window.w < 940 || window.h < 580;
        if ((mainView_ == View::memory && desktop_.open(1))
            || (mainView_ == View::terminal && desktop_.open(2)))
            mainView_ = View::editor;
        if (narrow_ && !desktop_.open(2) && focus_ == Focus::stdinBox)
            mainView_ = View::terminal;
        titleBar = {0, 0, window.w, narrow_ ? 52 : 72};
        const int quitWidth = window.w < 360 ? 40 : 84;
        quitButton = {window.right() - margin - quitWidth, (titleBar.h - 28) / 2, quitWidth, 28};
        const int shortcutsBottom = placeShortcuts({margin, titleBar.bottom() + 8, window.w - 2 * margin, 0});
        const int navY = shortcutsBottom + 13;
        const int navWidth = narrow_ ? 92 : 118;
        fileButton = {window.right() - margin - navWidth, navY, navWidth, 32};
        toolsButton = {fileButton.x - navWidth - 8, navY, navWidth, 32};
        const int modeWidth = studying_ ? std::min(128, window.w - 2 * margin)
            : std::clamp(toolsButton.x - margin - 8, 0, 128);
        modeButton = {margin, studying_ ? titleBar.bottom() + 8 : navY, modeWidth, 32};
        const int menuWidth = std::min(326, window.w - 2 * margin);
        fileMenu = {window.right() - margin - menuWidth, fileButton.bottom() + 4, menuWidth, 10 + 36 * fileRows};
        if (studying_)
            course_.layout(inset(window, margin, modeButton.bottom() + 14, margin), auxiliaryBody(1));

        viewTabs.fill({});
        const int tabCount = 1 + !desktop_.open(1) + !desktop_.open(2);
        const bool showTabs = narrow_ && tabCount > 1;
        if (showTabs)
        {
            const Rect row{margin, fileButton.bottom() + 8, window.w - 2 * margin, 29};
            int position = 0;
            for (const auto view : {View::editor, View::memory, View::terminal})
            {
                const auto index = static_cast<std::size_t>(view);
                if (index && desktop_.open(index))
                    continue;
                viewTabs[index] = horizontalPart(row, position++, tabCount);
            }
        }

        transport = inset(window, margin, std::max(0, window.h - margin - 36), margin);
        statusArea = narrow_ ? Rect{margin, transport.y - 23, transport.w, 20}
                             : Rect{margin + 673, transport.y + 9, transport.w - 673, 20};
        const int contentTop = fileButton.bottom() + (showTabs ? 46 : 15);
        const int contentBottom = narrow_ ? statusArea.y - 6 : transport.y - 14;
        mainArea = inset(window, margin, contentTop, window.h - contentBottom);

        if (narrow_)
        {
            const bool showSpeed = transport.w >= 385;
            const int count = showSpeed ? 5 : 4;
            resetButton = horizontalPart(transport, 0, count);
            backButton = horizontalPart(transport, 1, count);
            nextButton = horizontalPart(transport, 2, count);
            playButton = horizontalPart(transport, 3, count);
            speedButton = showSpeed ? horizontalPart(transport, 4, count) : Rect{};
        }
        else
        {
            resetButton = {margin, transport.y, 132, transport.h};
            backButton = {resetButton.right() + 10, transport.y, 117, transport.h};
            nextButton = {backButton.right() + 10, transport.y, 142, transport.h};
            playButton = {nextButton.right() + 10, transport.y, 130, transport.h};
            speedButton = {playButton.right() + 10, transport.y, 92, transport.h};
        }

        const int left = (mainArea.w - 16) * 41 / 100;
        auto workspace = mainArea;
        compactCompile = {};
        compactIo = {};
        if (!desktop_.open(2) && !narrow_)
        {
            const int dockHeight = std::min(145, mainArea.h / 3);
            const int dockTop = mainArea.bottom() - dockHeight;
            compactCompile = {mainArea.x, dockTop, left, dockHeight};
            compactIo = {mainArea.x + left + 16, dockTop, mainArea.w - left - 16, dockHeight};
            workspace.h = std::max(0, dockTop - 15 - workspace.y);
        }

        codePanel = workspace;
        if (!narrow_ && !desktop_.open(1))
            codePanel.w = left;
        codeView = {codePanel.x + 1, codePanel.y + 43, codePanel.w - 2, std::max(0, codePanel.h - 76)};
        codeX = codeView.x + 54;
        codeRows = codeView.h / codePitch;
        codeColumns = std::max(0, (codeView.right() - codeX - 10) / glyphWidth);

        if (desktop_.open(1))
            memoryPanel = auxiliaryBody(1);
        else if (narrow_)
            memoryPanel = workspace;
        else
            memoryPanel = {workspace.x + left + 16, workspace.y, workspace.w - left - 16, workspace.h};
        memoryView = inset(memoryPanel, 12, memoryPanel.w < 620 ? 64 : 47, 38);
        arrayColumns = std::max(1, (memoryView.w - 26) / cellPitch);

        auto &editor = doc().editor;
        editor.top = std::min(editor.top, maxCodeTop());
        explorerTop_ = std::min(explorerTop_, maxExplorerTop());
        invalidate();
    }

    void button(Rect r, std::string_view label, std::string_view key, unsigned accent,
                bool primary = false, bool enabled = true)
    {
        paintButton(draw_, r, hover_, label, key, accent, primary, enabled);
    }

    void drawTitle(std::string_view label, unsigned tint)
    {
        const auto window = windowArea(0);
        const int margin = windowMargin(window);
        draw_.rect({0, titleBar.bottom() - 1, window.w, 1}, color::rule);
        const int statusWidth = std::min(186, (quitButton.x - margin) / 2);
        const Rect status{quitButton.x - 9 - statusWidth, quitButton.y, statusWidth, quitButton.h};
        const int titleX = margin + (narrow_ ? 0 : 51);
        if (!narrow_)
        {
            draw_.frame({margin, 19, 37, 37}, color::background, color::rule);
            draw_.text(margin + 12, 24, "b", color::lime, 20, true);
            draw_.text(titleX, 50, "Interpréteur visuel", color::type, 12, true);
        }
        draw_.text(titleX, narrow_ ? 10 : 16, "bstep", color::text, narrow_ ? 24 : 28,
                   false, false, status.x - titleX - 8);

        draw_.frame(status, color::background, color::rule);
        draw_.rect({status.x + 10, status.y + 11, 5, 5}, tint);
        draw_.text(status.x + 22, status.y + 7, label, tint, 13, true, false, status.w - 29);

        button(quitButton, quitButton.w < 60 ? "×" : "Quitter", {}, commandColor::close);
    }

    void drawHeader()
    {
        const auto window = windowArea(0);
        const int margin = windowMargin(window);
        const auto *state = frame();
        const bool fault = compileError_ || (state && state->error);
        const bool waiting = state && machine_->waiting();
        const auto tint = fault ? color::error : (modified() || waiting || !breakpoint_.empty()) ? color::gold : color::text;
        const std::string_view label = fault ? "ERREUR"
            : waiting ? "ATTENTE STDIN" : !breakpoint_.empty() ? "POINT D'ARRÊT"
            : modified() ? "CODE MODIFIÉ" : playing_ ? "EN LECTURE"
            : !state ? "PRÊT" : state->finished ? "TERMINÉ" : "EN PAUSE";
        drawTitle(label, tint);
        for (std::size_t i = 0; i < shortcuts.size(); ++i)
        {
            const auto &shortcut = shortcuts[i];
            const Rect badge = shortcutKeys[i];
            const int labelX = badge.right() + 7;
            draw_.frame(badge, color::background, shortcut.accent);
            draw_.text(badge.x + 6, badge.y + 2, shortcut.key, shortcut.accent, 12, true, true);
            draw_.text(labelX, badge.y + 1, shortcut.label, color::muted, 13, false, false,
                       window.right() - margin - labelX);
        }
        draw_.rect({margin, fileButton.y - 6, window.w - 2 * margin, 1}, color::grid);

        button(modeButton, "Parcours", {}, commandColor::course);
        button(toolsButton, "Débogage", "F9", commandColor::debug);
        button(fileButton, "Fichier", {}, commandColor::file);
        constexpr std::array<std::string_view, 3> names{"Éditeur", "Mémoire", "Terminal"};
        constexpr std::array accents{color::rule, commandColor::debug, commandColor::terminal};
        for (std::size_t i = 0; i < viewTabs.size(); ++i)
            button(viewTabs[i], names[i], {}, accents[i], static_cast<std::size_t>(mainView_) == i);
    }

    void drawCode()
    {
        if (codePanel.h < 36)
            return;
        draw_.frame(codePanel, color::panel, editing_ ? color::white : color::rule);
        draw_.text(codePanel.x + 16, codePanel.y + 11, "Éditeur", color::text, 20);
        const std::string location = frame() ? std::string(frame()->functionName()) + "() · " + doc().displayName
                                             : doc().displayName;
        draw_.text(codePanel.x + 155, codePanel.y + 16, fitMono(location, (codePanel.w - 164) / 7), color::type, 12, true);
        if (codePanel.h < 76)
            return;
        const auto &ed = doc().editor;
        const auto [caretRow, caretCol] = ed.location();
        const auto *state = frame();
        const int active = state ? machine_->nextLine() : compileError_ ? compileError_->where.line : 0;
        paintSource(draw_, ed, codeView, {active, state ? state->lastLine : 0,
                                        compileError_ || (state && state->error), caretVisible_ && editing_});
        draw_.rect({codePanel.x, codeView.bottom() + 1, codePanel.w, 1}, color::rule);
        std::string detail = "Clique dans le code pour l'éditer";
        if (modified())
            detail = "Pas suivant ou Lecture relance le code modifié";
        else if (editing_)
            detail = "Ligne " + std::to_string(caretRow + 1) + "   Colonne " + std::to_string(caretCol + 1);
        draw_.text(codePanel.x + 14, codeView.bottom() + 11, detail,
                   modified() ? color::gold : color::type, 13, false, false, codePanel.w - 28);
    }

    bool visible(Rect r) const
    {
        return r.y >= memoryView.y && r.bottom() <= memoryView.bottom();
    }

    void arrangeMemory(const Frame &state)
    {
        nodes_.clear();
        arrays_.clear();

        const int left = memoryView.x + 20;
        int y = memoryView.y + 43 - memoryScroll_;
        const int scalarTop = y;
        const int available = std::max(1, memoryView.w - 40);
        const int scalarColumns = std::max(1, available / 164);
        const int scalarWidth = std::min(205, (available - (scalarColumns - 1) * 14) / scalarColumns);
        const int cellWidth = std::min(72, available);
        int scalars = 0;
        for (const auto &object : state.objects)
            if (!object.array)
            {
                const Rect rect{left + (scalars % scalarColumns) * (scalarWidth + 14),
                                scalarTop + (scalars / scalarColumns) * 103, scalarWidth, 88};
                nodes_.push_back({&object, 0, rect, false, object.baseAddress});
                ++scalars;
            }
        if (scalars)
            y += ((scalars + scalarColumns - 1) / scalarColumns) * 103 + 28;

        for (const auto &object : state.objects)
        {
            if (!object.array)
                continue;
            arrays_.push_back({&object, y});
            const int length = static_cast<int>(object.cells.size());
            const bool endPointer = std::any_of(state.objects.begin(), state.objects.end(),
                [&object, length](const Object &candidate)
                {
                    return candidate.element.pointers
                        && std::any_of(candidate.cells.begin(), candidate.cells.end(),
                            [&object, length](const auto &value)
                            {
                                return value && value->address == Address{object.baseAddress, length};
                            });
                });
            const int columns = length + endPointer;
            for (int i = 0; i < columns; ++i)
            {
                const Rect rect{left + i % arrayColumns * cellPitch, y + 47 + i / arrayColumns * 108, cellWidth, 77};
                nodes_.push_back({&object, i, rect, i == length, object.baseAddress});
            }
            y += 47 + ((columns + arrayColumns - 1) / arrayColumns) * 108 + 14;
        }
        memoryMaxScroll_ = std::max(0, y + memoryScroll_ - memoryView.bottom());
        if (memoryScroll_ > memoryMaxScroll_)
        {
            memoryScroll_ = memoryMaxScroll_;
            arrangeMemory(state);
        }
    }

    enum class Activity { none, read, write };

    static Activity activity(const Frame &state, Address target)
    {
        auto result = Activity::none;
        for (const auto &access : state.accesses)
        {
            if (access.address == target)
            {
                if (access.write)
                    return Activity::write;
                result = Activity::read;
            }
        }
        return result;
    }

    std::map<std::pair<std::uint64_t, int>, std::string> indexLabels(const Frame &state) const
    {
        std::map<std::pair<std::uint64_t, int>, std::string> labels;
        for (const auto &[tableName, indexName] : machine_->program().indexUses)
        {
            const auto *table = state.find(tableName);
            const auto *index = state.find(indexName);
            if (!table || !index || index->array || index->element.pointers || !index->cells[0])
                continue;
            Address base;
            if (table->array)
                base = {table->baseAddress, 0};
            else if (table->element.pointers && table->cells[0])
                base = table->cells[0]->address;
            else
                continue;

            const auto subscript = static_cast<std::int64_t>(base.index) + index->cells[0]->number;
            const auto *target = state.find(base.object);
            if (target && subscript >= 0 && subscript <= static_cast<std::int64_t>(target->cells.size()))
            {
                auto &label = labels[{target->baseAddress, static_cast<int>(subscript)}];
                if (!label.empty())
                    label += ",";
                label += indexName;
            }
        }
        return labels;
    }

    void connector(const Node &from, const Node &to, int lane)
    {
        if (!visible(from.rect) || !visible(to.rect) || from.address() == to.address())
            return;
        const int x1 = from.object->array ? from.rect.middle() : from.rect.right() - 23;
        const int y1 = from.rect.bottom() + 1;
        const int x2 = to.object->array ? to.rect.right() - 10 : to.rect.middle();
        const bool down = to.rect.y > y1 + 32;
        const int y2 = down ? to.rect.y - 1 : to.rect.bottom() + 1;
        const int elbow = down
            ? std::max(y1 + 9, y2 - 25 - lane * 5)
            : std::max(y1, y2) + 13 + lane * 5;
        if (elbow >= memoryView.bottom() - 3)
            return;

        draw_.rect({x1 - 2, y1, 5, 5}, color::lavender);
        draw_.rect({x1 - 1, y1 + 3, 2, elbow - y1 - 2}, color::lavender);
        draw_.rect({std::min(x1, x2) - 1, elbow, std::abs(x1 - x2) + 2, 2}, color::lavender);
        draw_.rect({x2 - 1, std::min(elbow, y2), 2, std::abs(elbow - y2) + 1}, color::lavender);
        const int arrowBase = y2 + (down ? -8 : 8);
        draw_.triangle({x2 - 4, arrowBase}, {x2 + 5, arrowBase}, {x2, y2}, color::lavender);
    }

    void drawScalar(const Frame &state, const Node &node, bool pointed)
    {
        const auto &object = *node.object;
        const auto &value = object.cells[0];
        const auto action = activity(state, node.address());
        const bool focused = selected_ == node.address() || node.rect.contains(hover_);
        const auto typeColor = object.element.pointers ? color::lavender : color::mint;
        const auto edge = focused ? color::white : action == Activity::write ? color::gold
            : pointed ? color::lavender : action == Activity::read ? color::mint : color::rule;
        draw_.frame(node.rect, action == Activity::write ? color::goldDark : color::panel, edge);
        draw_.rect({node.rect.x, node.rect.y, 3, node.rect.h}, typeColor);
        const std::string name = object.function == "main" ? object.name : object.function + "." + object.name;
        const int labelWidth = node.rect.w - 24;
        draw_.text(node.rect.x + 13, node.rect.y + 10, fitMono(name, labelWidth / 8), color::text, 14, true);
        draw_.text(node.rect.x + 13, node.rect.y + 31,
                   value ? fitMono(cmini::valueDisplay(state, *value), labelWidth / 13) : "—",
                   value ? color::text : color::type, 22, true);
        const auto subtype = object.element.name()
            + (value && value->type.pointers ? "   " + state.hex(value->address) : "");
        draw_.text(node.rect.x + 13, node.rect.y + 62, fitMono(subtype, labelWidth / 7), color::type, 12, true);
    }

    void drawCell(const Frame &state, const Node &node, bool pointed, std::string_view index)
    {
        const auto &object = *node.object;
        const auto action = activity(state, node.address());
        const auto r = node.rect;
        const bool focused = !node.terminal && (selected_ == node.address() || r.contains(hover_));
        const auto edge = focused ? color::white : action == Activity::write ? color::gold
            : pointed ? color::lavender : action == Activity::read ? color::mint : color::rule;
        const auto background = node.terminal ? color::lavenderDark
            : action == Activity::write ? color::goldDark
            : action == Activity::read ? color::mintDark : color::panel;
        draw_.frame(r, background, edge);
        if (pointed)
            draw_.rect({r.right() - 13, r.y, 6, 3}, color::lavender);
        if (!index.empty())
        {
            const auto label = fitMono(index, 4);
            const int width = std::clamp(12 + static_cast<int>(decodeUtf8(label).size()) * 7, 19, 40);
            draw_.frame({r.x + 2, r.y - 22, width, 18}, color::limeDark, color::lime);
            draw_.text(r.x + 8, r.y - 21, label, color::lime, 11, true);
            draw_.triangle({r.x + 7, r.y - 5}, {r.x + 13, r.y - 5}, {r.x + 10, r.y - 1}, color::lime);
        }
        const auto indexText = "[" + std::to_string(node.index) + "]";
        draw_.text(r.x + 7, r.y + 3, indexText, node.terminal ? color::lavender : color::type, 12, true);
        if (node.terminal)
        {
            draw_.text(r.x + 7, r.y + 29, "fin", color::lavender, 14, true);
            return;
        }
        const auto &value = object.cells[node.index];
        auto shown = value ? cmini::valueDisplay(state, *value) : "—";
        const bool isChar = object.element.base == cmini::Base::character && !object.element.pointers;
        if (object.element.pointers && value)
            shown = value->address.object ? state.hex(value->address) : "NULL";
        const auto columns = static_cast<int>(decodeUtf8(shown).size());
        const int size = columns <= 4 ? 22 : columns <= 7 ? 14 : 10;
        draw_.text(std::max(r.x + 4, r.middle() - columns * size * 3 / 10), r.y + 23,
                   shown, value ? color::text : color::type, size, true);
        if (isChar && value)
        {
            const auto byte = std::to_string(static_cast<unsigned char>(value->number));
            draw_.text(r.middle() - static_cast<int>(byte.size()) * 3, r.y + 56, byte, color::type, 12, true);
        }
    }

    void drawMemory()
    {
        if (memoryPanel.h < 80)
            return;
        draw_.frame(memoryPanel, color::panel, color::rule);
        const auto *state = frame();
        const std::string title = desktop_.open(1)
            ? (state ? std::string("Fonction courante · ") + std::string(state->functionName()) + "()" : "État de la mémoire")
            : "Mémoire";
        const bool narrow = memoryPanel.w < 620;
        draw_.text(memoryPanel.x + 16, memoryPanel.y + (narrow ? 7 : 11), title, color::text, 20,
                   false, false, memoryPanel.w - (narrow ? 32 : 340));
        const int legendY = memoryPanel.y + (narrow ? 38 : 17);
        const int legendX = narrow ? memoryPanel.x + 16 : memoryPanel.right() - 324;
        draw_.rect({legendX, legendY + 5, 6, 6}, color::mint);
        draw_.text(legendX + 14, legendY, "Lecture", color::muted, 12);
        draw_.rect({legendX + 84, legendY + 5, 6, 6}, color::gold);
        draw_.text(legendX + 98, legendY, "Écriture", color::muted, 12);
        draw_.line(legendX + 172, legendY + 8, legendX + 185, legendY + 8, color::lavender);
        draw_.triangle({legendX + 180, legendY + 5}, {legendX + 180, legendY + 11}, {legendX + 186, legendY + 8}, color::lavender);
        draw_.text(legendX + 195, legendY, "Pointeur", color::muted, 12);

        draw_.frame(memoryView, color::board, color::grid);
        draw_.dotGrid(memoryView);
        draw_.rect({memoryPanel.x, memoryView.bottom() + 2, memoryPanel.w, 1}, color::rule);
        draw_.text(memoryPanel.x + 14, memoryView.bottom() + 12, "Adresses virtuelles", color::type, 12, true);

        if (!state || state->objects.empty())
        {
            nodes_.clear();
            arrays_.clear();
            memoryMaxScroll_ = 0;
            std::string_view title = "La mémoire est vide";
            std::string_view hint = "Les variables apparaissent à leur déclaration.";
            if (modified())
            {
                title = "Code modifié";
                hint = "Clique sur Pas suivant ou Lecture pour lancer cette version.";
            }
            else if (compileError_)
            {
                title = "Erreur de syntaxe";
                hint = "La ligne à corriger est indiquée dans l'éditeur.";
            }
            const int width = std::min(420, std::max(0, memoryView.w - 20));
            const Rect notice{memoryView.middle() - width / 2, memoryView.y + (memoryView.h - 94) / 2, width, 94};
            if (notice.h <= memoryView.h)
            {
                draw_.frame(notice, color::panel, color::rule);
                draw_.text(notice.x + 18, notice.y + 20, title, color::text, 21, false, false, width - 36);
                vt::paint(draw_, {notice.x + 18, notice.y + 48, width - 36, 40}, hint, 0, 12);
            }
            return;
        }

        arrangeMemory(*state);
        const auto labels = indexLabels(*state);
        const bool hasScalars = std::any_of(nodes_.begin(), nodes_.end(), [](const Node &node)
            { return !node.object->array; });
        const Rect scalarTitle{memoryView.x + 20, memoryView.y + 13 - memoryScroll_, 210, 18};
        if (hasScalars && visible(scalarTitle))
            draw_.text(scalarTitle.x, scalarTitle.y, "VARIABLES", color::muted, 12, true);

        std::vector<Address> targets;
        for (const auto &from : nodes_)
        {
            if (from.terminal || !from.object->element.pointers)
                continue;
            const auto &value = from.object->cells[from.index];
            if (!value || !value->address.object)
                continue;
            const auto dest = std::find_if(nodes_.begin(), nodes_.end(),
                [&value](const Node &candidate) { return candidate.address() == value->address; });
            if (dest != nodes_.end())
            {
                connector(from, *dest, static_cast<int>(targets.size() % 3));
                targets.push_back(value->address);
            }
        }

        for (const auto &row : arrays_)
        {
            const auto &object = *row.object;
            const Rect header{memoryView.x + 20, row.y, 440, 25};
            if (!visible(header))
                continue;
            const auto name = object.literal || object.function == "main" ? object.name : object.function + "." + object.name;
            const auto text = name + "  ·  " + object.element.name() + "[" + std::to_string(object.cells.size()) + "]";
            draw_.text(header.x, header.y, fitMono(text, (memoryView.w - 40) / 8), color::text, 14, true);
        }

        std::optional<Address> inspect = selected_;
        for (const auto &node : nodes_)
        {
            if (!visible(node.rect))
                continue;
            const auto address = node.address();
            const bool pointed = std::find(targets.begin(), targets.end(), address) != targets.end();
            if (node.object->array)
            {
                const auto label = labels.find({address.object, address.index});
                const auto caption = label != labels.end() && node.rect.y - 22 >= memoryView.y
                    ? std::string_view(label->second) : std::string_view{};
                drawCell(*state, node, pointed, caption);
            }
            else
                drawScalar(*state, node, pointed);

            if (!node.terminal && node.rect.contains(hover_))
                inspect = address;
        }

        if (memoryMaxScroll_)
        {
            const int height = std::max(20, memoryView.h * memoryView.h / (memoryView.h + memoryMaxScroll_));
            const int y = memoryView.y + (memoryView.h - height) * memoryScroll_ / memoryMaxScroll_;
            draw_.rect({memoryView.right() - 5, y + 1, 3, height - 1}, color::type);
        }
        if (!inspect)
            return;
        if (const auto *object = state->find(inspect->object))
        {
            auto detail = object->label(inspect->index) + "   " + state->hex(*inspect);
            const auto &value = object->cells[inspect->index];
            if (value && value->type.pointers)
                detail += "   →   " + state->describe(value->address);
            else if (!value)
                detail += "   non initialisé";
            draw_.text(memoryPanel.x + 180, memoryView.bottom() + 12, fitMono(detail, (memoryPanel.w - 194) / 7), color::muted, 12, true);
        }
    }

    void detailLines(int x, int y, std::string_view detail, int columns, unsigned tint)
    {
        const auto text = decodeUtf8(detail);
        if (static_cast<int>(text.size()) <= columns)
        {
            draw_.text(x, y, detail, tint, 13, true);
            return;
        }
        const auto boundary = text.rfind(U' ', static_cast<std::size_t>(columns));
        const auto cut = boundary == text.npos || boundary == 0
            ? static_cast<std::size_t>(columns) : boundary;
        draw_.text(x, y - 4, encodeUtf8(std::u32string_view(text).substr(0, cut)), tint, 13, true);
        auto rest = std::u32string_view(text).substr(cut);
        if (rest.starts_with(U' '))
            rest.remove_prefix(1);
        draw_.text(x, y + 13, fitMono(encodeUtf8(rest), columns), tint, 13, true);
    }

    void setFocus(Focus focus)
    {
        focus_ = focus;
        editing_ = focus == Focus::editor;
        if (!editing_)
            dragging_ = false;
        caretTime_ = Clock::now();
        invalidate();
    }

    LineInput *inputField()
    {
        switch (focus_)
        {
        case Focus::stdinBox: return &stdinLine_;
        case Focus::command: return &commandLine_;
        case Focus::filename: return &pathLine_;
        default: return nullptr;
        }
    }

    void openDraft()
    {
        const auto draft = std::ranges::find_if(docs_, [](const Document &item) { return !item.path; });
        if (draft == docs_.end())
        {
            docs_.emplace_back("programme.c", "");
            tab_ = docs_.size() - 1;
        }
        else
            tab_ = static_cast<std::size_t>(draft - docs_.begin());
        endExplorer();
        mainView_ = View::editor;
        setFocus(Focus::editor);
        prepare();
        follow();
    }

    void openSource(const fs::path &file)
    {
        const auto path = fs::canonical(file);
        if (!source_.contains(path) || !fs::is_regular_file(path))
            throw std::runtime_error("Choisis un fichier ordinaire du dossier source.");
        const auto open = std::ranges::find_if(docs_, [&](const Document &item) { return item.path == path; });
        if (open != docs_.end())
            tab_ = static_cast<std::size_t>(open - docs_.begin());
        else
        {
            docs_.emplace_back(path.filename().string(), readSource(path));
            docs_.back().path = path;
            tab_ = docs_.size() - 1;
        }
        endExplorer();
        prepare();
        follow();
    }

    void writeSource(const fs::path &raw)
    {
        const auto target = source_.resolve(raw);
        if (fs::is_directory(target))
            throw std::runtime_error("Choisis un nom de fichier.");
        if (!fs::is_directory(target.parent_path()))
            throw std::runtime_error("Crée d'abord le dossier de destination.");
        const auto code = doc().editor.source();
        auto pattern = target.string() + ".cpasapas-XXXXXX";
        const int fd = mkstemp(pattern.data());
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(), "Impossible de préparer l'enregistrement");
        const fs::path temp(pattern);
        auto bytes = std::string_view(code);
        int error = 0;
        while (!bytes.empty())
        {
            const auto written = write(fd, bytes.data(), bytes.size());
            if (written > 0)
                bytes.remove_prefix(static_cast<std::size_t>(written));
            else if (written == 0 || errno != EINTR)
            {
                error = written == 0 ? EIO : errno;
                break;
            }
        }
        if (::close(fd) && !error)
            error = errno;
        try
        {
            if (error)
                throw std::system_error(error, std::generic_category(), "Impossible d'écrire le fichier");
            if (fs::exists(target))
                fs::permissions(temp, fs::status(target).permissions());
            fs::rename(temp, target);
        }
        catch (...)
        {
            std::error_code ignored;
            fs::remove(temp, ignored);
            throw;
        }
        doc().path = target;
        doc().displayName = target.filename().string();
        fileNote_ = "Enregistré · " + doc().displayName;
        invalidate();
    }

    void save()
    {
        fileMenuOpen_ = false;
        if (!doc().path)
        {
            startExplorer(Browser::save);
            return;
        }
        try { writeSource(*doc().path); }
        catch (const std::exception &error) { fileNote_ = error.what(); invalidate(); }
    }

    void startExplorer(Browser mode)
    {
        playing_ = false;
        resumeInput_ = false;
        fileMenuOpen_ = false;
        browser_ = mode;
        debugTools_ = false;
        toolsOverlay_ = !desktop_.open(3);
        explorerTop_ = 0;
        explorerSelection_ = -1;
        browserNote_.clear();
        replacing_.reset();
        pathLine_.set(mode == Browser::save ? doc().displayName : "");
        try { source_.refresh(); }
        catch (const std::exception &error) { browserNote_ = error.what(); }
        setFocus(Focus::filename);
    }

    void endExplorer()
    {
        browser_ = Browser::idle;
        browserNote_.clear();
        toolsOverlay_ = false;
        setFocus(Focus::none);
    }

    void openTools()
    {
        fileMenuOpen_ = false;
        debugTools_ = true;
        if (!desktop_.open(3))
        {
            toolsOverlay_ = !toolsOverlay_;
            if (!toolsOverlay_)
            {
                endExplorer();
                return;
            }
        }
        setFocus(Focus::command);
    }

    void chooseEntry(int index)
    {
        if (index < 0 || index >= static_cast<int>(source_.entries.size()))
            return;
        explorerSelection_ = index;
        const auto entry = source_.entries[index];
        replacing_.reset();
        browserNote_.clear();
        try
        {
            if (entry.directory)
            {
                source_.enter(entry.name);
                explorerTop_ = 0;
                explorerSelection_ = -1;
            }
            else if (browser_ == Browser::open)
                openSource(source_.resolve(entry.name));
            else if (browser_ == Browser::save)
                pathLine_.set(entry.name.string());
        }
        catch (const std::exception &error) { browserNote_ = error.what(); }
    }

    void selectExplorer(int step)
    {
        if (source_.entries.empty())
            return;
        explorerSelection_ = std::clamp(explorerSelection_ + step, 0, static_cast<int>(source_.entries.size()) - 1);
        if (explorerSelection_ < explorerTop_)
            explorerTop_ = explorerSelection_;
        if (explorerSelection_ >= explorerTop_ + explorerRows())
            explorerTop_ = explorerSelection_ - explorerRows() + 1;
        const auto &entry = source_.entries[explorerSelection_];
        if (browser_ != Browser::folder)
            pathLine_.set(entry.name.string());
    }

    void acceptPath()
    {
        browserNote_.clear();
        try
        {
            const auto entered = pathLine_.text();
            if (entered.empty())
                throw std::runtime_error("Choisis un élément ou saisis un chemin.");
            auto path = source_.resolve(entered);
            if (browser_ == Browser::folder)
            {
                if (!fs::create_directories(path))
                    throw std::runtime_error("Ce dossier existe déjà.");
                const auto back = parentBrowser_;
                source_.enter(path);
                startExplorer(back);
                return;
            }
            if (fs::is_directory(path))
            {
                source_.enter(path);
                explorerTop_ = 0;
                explorerSelection_ = -1;
                pathLine_.set(browser_ == Browser::save ? doc().displayName : "");
                return;
            }
            if (browser_ == Browser::open)
                openSource(path);
            else if (browser_ == Browser::save)
            {
                if (path.extension().empty())
                    path += ".c";
                path = source_.resolve(path);
                if (path != doc().path && fs::exists(path) && replacing_ != path)
                {
                    replacing_ = path;
                    browserNote_ = "Ce fichier existe. Entrée pour le remplacer.";
                    return;
                }
                writeSource(path);
                endExplorer();
            }
        }
        catch (const std::exception &error) { browserNote_ = error.what(); }
    }

    void submitInput(bool endOfInput)
    {
        const auto line = stdinLine_.text();
        if (!ready() || machine_->frame().finished || machine_->frame().error)
            return;
        const bool waiting = machine_->waiting();
        const bool resume = resumeInput_;
        if ((!endOfInput || !line.empty()) && !machine_->submit(line + (endOfInput ? "" : "\n")))
        {
            fileNote_ = "Entrée fermée ou tampon d'entrée plein.";
            invalidate();
            return;
        }
        if (endOfInput)
            machine_->closeInput();
        stdinLine_.clear();
        terminalScroll_ = 0;
        if (waiting)
        {
            playing_ = resume;
            advance(resume);
            nextTick_ = Clock::now() + periods_[speed_];
        }
        invalidate();
    }

    static std::string_view trim(std::string_view text)
    {
        const auto first = text.find_first_not_of(" \t");
        return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t") - first + 1);
    }

    void shellPrint(std::string_view text)
    {
        shellLog_ += text;
        shellLog_ += '\n';
        if (shellLog_.size() > 4000)
        {
            const auto nextLine = shellLog_.find('\n', shellLog_.size() - 4000);
            if (nextLine != shellLog_.npos)
                shellLog_.erase(0, nextLine + 1);
        }
    }

    void executeCommand()
    {
        const auto command = commandLine_.text();
        commandLine_.clear();
        if (trim(command).empty())
            return;
        shellPrint("> " + command);
        const auto input = trim(command);
        const auto space = input.find_first_of(" \t");
        const auto word = input.substr(0, space);
        auto args = space == input.npos ? std::string_view{} : trim(input.substr(space + 1));
        try
        {
            if (word == "watch" || word == "break")
            {
                if (word == "break" && args.starts_with("if "))
                    args = trim(args.substr(3));
                if (watches_.size() + breaks_.size() >= 32)
                    throw std::runtime_error("Retire une règle avant d'en ajouter une.");
                auto expr = cmini::compileProbe(args);
                auto &rules = word == "watch" ? watches_ : breaks_;
                rules.push_back({nextRule_++, std::string(args), std::move(expr)});
                shellPrint("Règle #" + std::to_string(rules.back().id) + " ajoutée.");
            }
            else if (word == "delete" || word == "unwatch")
            {
                int id = 0;
                if (args.empty())
                    throw std::runtime_error("Indique le numéro de la règle à retirer.");
                const auto n = std::from_chars(args.data(), args.data() + args.size(), id);
                if (n.ec != std::errc{} || n.ptr != args.end())
                    throw std::runtime_error("Indique le numéro de la règle à retirer.");
                const auto match = [&](const Rule &rule) { return rule.id == id; };
                const auto count = std::erase_if(watches_, match)
                    + (word == "delete" ? std::erase_if(breaks_, match) : 0);
                shellPrint(count ? "Règle retirée." : "Règle inconnue.");
            }
            else if (word == "list")
            {
                for (const auto &rule : watches_)
                    shellPrint("#" + std::to_string(rule.id) + " watch " + rule.source);
                for (const auto &rule : breaks_)
                    shellPrint("#" + std::to_string(rule.id) + " break if " + rule.source);
                if (watches_.empty() && breaks_.empty())
                    shellPrint("Aucune règle.");
            }
            else if (word == "continue")
            {
                if (!playing_)
                    togglePlay();
            }
            else if (word == "next") goForward();
            else if (word == "back") goBack();
            else if (word == "help")
                shellPrint("watch fonction.variable[2]  |  break if expression\nlist  |  delete numéro  |  continue  |  next  |  back");
            else
                shellPrint("Commande inconnue. Saisis help.");
        }
        catch (const std::exception &error) { shellPrint(error.what()); }
        if (!machine_ || !machine_->waiting())
            setFocus(Focus::command);
        invalidate();
    }

    void memoryClick(t_bunny_position p)
    {
        if (!memoryView.contains(p))
            return;
        selected_.reset();
        for (const auto &node : nodes_)
            if (node.rect.contains(p) && visible(node.rect) && !node.terminal)
            {
                selected_ = node.address();
                return;
            }
    }

    void editorClick(t_bunny_position p)
    {
        if (fileButton.contains(p))
        {
            fileMenuOpen_ = !fileMenuOpen_;
            setFocus(Focus::none);
            return;
        }
        if (fileMenuOpen_)
        {
            fileMenuOpen_ = false;
            if (fileMenu.contains(p))
            {
                const int selected = (p.y - fileMenu.y - 6) / 36;
                if (selected == 0) openDraft();
                else if (selected == 1) startExplorer(Browser::open);
                else if (selected == 2) save();
                else if (selected == 3) startExplorer(Browser::save);
                else restoreWindows_ = true;
                return;
            }
        }
        if (toolsButton.contains(p))
        {
            openTools();
            return;
        }
        for (std::size_t i = 0; i < viewTabs.size(); ++i)
            if (viewTabs[i].contains(p))
            {
                mainView_ = static_cast<View>(i);
                setFocus(Focus::none);
                return;
            }
        if (showingInMain(View::terminal) && mainTerminal().contains(p))
        {
            terminalClick(p, mainTerminal());
            return;
        }
        if (showingInMain(View::editor) && codeView.contains(p))
        {
            playing_ = false;
            setFocus(Focus::editor);
            dragging_ = true;
            placeMouse(p, leftShift_ || rightShift_);
            return;
        }
        setFocus(Focus::none);
        if (resetButton.contains(p)) restart();
        else if (backButton.contains(p)) goBack();
        else if (nextButton.contains(p)) goForward();
        else if (playButton.contains(p)) togglePlay();
        else if (speedButton.contains(p))
        {
            speed_ = (speed_ + 1) % periods_.size();
            nextTick_ = Clock::now() + periods_[speed_];
        }
        else if (showingInMain(View::memory) && memoryView.contains(p))
            memoryClick(p);
    }

    void drawFileMenu()
    {
        draw_.frame(fileMenu, color::background, commandColor::file);
        constexpr std::array<std::string_view, fileRows> labels{
            "Brouillon", "Ouvrir…", "Enregistrer", "Enregistrer sous…", "Rouvrir les fenêtres"
        };
        constexpr std::array<std::string_view, fileRows> keys{"", "Ctrl+O", "Ctrl+S", "Ctrl+Maj+S", "F10"};
        for (int i = 0; i < fileRows; ++i)
        {
            const Rect row{fileMenu.x + 5, fileMenu.y + 7 + i * 36, fileMenu.w - 10, 34};
            if (row.contains(hover_))
                draw_.rect(row, color::selection);
            draw_.text(row.x + 9, row.y + 7, labels[i], color::text, 16);
            draw_.text(row.right() - 102, row.y + 10, keys[i], color::muted, 12, true);
        }
    }

    Rect fullCompiler(Rect area) const
    {
        const auto *state = frame();
        const bool active = state && state->steps && !state->error && !state->finished;
        const int wanted = active ? 130 : 268;
        return {area.x, area.y, area.w, std::min(wanted, area.h * 2 / 5)};
    }

    Rect fullTerminal(Rect area) const
    {
        const int y = std::min(fullCompiler(area).bottom() + 12, area.bottom());
        return {area.x, y, area.w, area.bottom() - y};
    }

    Rect mainCompiler() const { return narrow_ ? fullCompiler(mainArea) : compactCompile; }
    Rect mainTerminal() const { return narrow_ ? fullTerminal(mainArea) : compactIo; }

    static TerminalLayout terminalLayout(Rect box, bool merged)
    {
        const int checkboxWidth = box.w >= 400 ? 150 : 80;
        const Rect merge{box.right() - checkboxWidth - 8, box.y + 4, checkboxWidth, 25};
        const Rect pane{box.x + 1, box.y + 33, box.w - 2, std::max(0, box.h - 34)};
        Rect out = pane, in = pane;
        if (!merged)
        {
            if (pane.w < 470 && pane.h >= 180)
            {
                out.h = pane.h * 2 / 3;
                in.y = out.bottom();
                in.h = pane.bottom() - in.y;
            }
            else
            {
                out.w = pane.w * 2 / 3;
                in.x = out.right();
                in.w = pane.right() - in.x;
            }
        }
        const int stderrWidth = out.w / 3;
        const Rect err{out.right() - stderrWidth, out.y, stderrWidth, out.h};
        out.w -= stderrWidth;
        if (merged)
            in = out;
        if (in.h < 56)
            return {merge, out, err, in, {}, {}};
        const int eofWidth = in.w >= 136 ? 52 : 35;
        const Rect edit{in.x + 8, in.bottom() - 35, std::max(0, in.w - eofWidth - 22), 27};
        const Rect eof{in.right() - eofWidth - 8, edit.y, eofWidth, 27};
        return {merge, out, err, in, edit, eof};
    }

    void terminalClick(t_bunny_position p, Rect box)
    {
        const auto rects = terminalLayout(box, terminalMerged_);
        if (rects.merge.contains(p))
        {
            terminalMerged_ = !terminalMerged_;
            terminalScroll_ = 0;
        }
        else if (rects.eof.contains(p))
            submitInput(true);
        else if (rects.edit.contains(p))
            setFocus(Focus::stdinBox);
        invalidate();
    }

    void drawCompile(Rect box)
    {
        if (box.h < 33)
            return;
        draw_.frame(box, color::panel, color::rule);
        draw_.text(box.x + 13, box.y + 9, "Compilation", color::text, 18);
        const auto *state = frame();
        std::string report;
        if (compileError_)
        {
            report = "\x1b[1;31mLigne " + std::to_string(compileError_->where.line) + ", colonne "
                + std::to_string(compileError_->where.column) + "\x1b[0m\n" + compileError_->what();
        }
        else if (modified())
            report = "Code modifié. F6 pour préparer ou F8 pour exécuter.";
        else if (state)
        {
            report = "\x1b[32mAnalyse réussie\x1b[0m";
            if (state->error)
                report += "\n\x1b[1;31mErreur d'exécution · ligne " + std::to_string(state->error->where.line)
                    + "\x1b[0m\n" + state->error->what();
            else if (!breakpoint_.empty())
                report += "\n\x1b[1;33m" + breakpoint_ + "\x1b[0m";
            else if (machine_->waiting())
                report += "\n\x1b[1;33mAttente sur stdin\x1b[0m";
            else if (state->finished)
                report += "\nProgramme terminé · retour " + std::to_string(state->returnCode.value_or(0));
            else if (state->lastLine)
                report += "\nLigne " + std::to_string(state->lastLine) + " · " + state->event;
            else
                report += "\nPrêt à exécuter.";
        }
        vt::paint(draw_, {box.x + 13, box.y + 41, box.w - 30, box.h - 48}, report);
    }

    void drawTerminal(Rect box)
    {
        if (box.h < 34)
            return;
        const auto rects = terminalLayout(box, terminalMerged_);
        const auto *state = frame();
        draw_.frame(box, color::panel, color::rule);
        const bool waiting = state && machine_->waiting();
        draw_.text(box.x + 13, box.y + 7, waiting ? "Terminal · attente sur stdin" : "Terminal",
                   waiting ? color::gold : color::text, 16, false, false, rects.merge.x - box.x - 23);
        draw_.frame({rects.merge.x + 4, rects.merge.y + 6, 13, 13}, color::background, commandColor::terminal);
        if (terminalMerged_)
            draw_.rect({rects.merge.x + 7, rects.merge.y + 9, 7, 7}, color::white);
        draw_.text(rects.merge.x + 24, rects.merge.y + 4, box.w >= 400 ? "stdin et stdout réunis" : "Fusion",
                   color::muted, 12, false, false, rects.merge.w - 25);
        draw_.frame(rects.out, color::editor, color::rule);
        draw_.frame(rects.err, color::editor, commandColor::terminal);
        if (!terminalMerged_)
            draw_.frame(rects.in, color::editor, color::rule);

        const auto title = terminalMerged_ ? "stdin + stdout" : "stdout";
        draw_.text(rects.out.x + 10, rects.out.y + 6, title, color::muted, 13, true, false, rects.out.w - 20);
        draw_.text(rects.err.x + 10, rects.err.y + 6, "stderr", commandColor::terminal,
                   13, true, false, rects.err.w - 20);
        if (!terminalMerged_)
            draw_.text(rects.in.x + 10, rects.in.y + 6, "stdin", waiting ? color::gold : color::muted, 13, true);
        const int outBottom = terminalMerged_ && rects.edit.h ? rects.edit.y - 4 : rects.out.bottom() - 6;
        if (state)
        {
            vt::paint(draw_, {rects.out.x + 10, rects.out.y + 25, rects.out.w - 22,
                              outBottom - rects.out.y - 25},
                      terminalMerged_ ? state->terminal : state->output, terminalScroll_);
            vt::paint(draw_, inset(rects.err, 10, 25, 6), state->errorOutput, stderrScroll_);
            if (!terminalMerged_)
                vt::paint(draw_, {rects.in.x + 10, rects.in.y + 25, rects.in.w - 22, rects.edit.y - rects.in.y - 28},
                          machine_->input());
        }
        const bool inputClosed = state && machine_->inputClosed();
        stdinLine_.paint(draw_, rects.edit, inputClosed ? "Entrée fermée" : "Saisir puis Entrée",
                         focus_ == Focus::stdinBox && !inputClosed);
        button(rects.eof, "EOF", {}, commandColor::terminal, false, !inputClosed);
    }

    ToolsLayout toolsLayout(bool separate) const
    {
        ToolsLayout layout;
        Rect body = separate ? auxiliaryBody(3) : mainArea;
        if (!separate)
        {
            layout.frame = body;
            layout.close = {body.right() - 43, body.y + 7, 32, 28};
            body = inset(body, 10, 43, 10);
        }
        layout.tabbed = body.w < 880;
        if (layout.tabbed)
        {
            const Rect row{body.x, body.y, body.w, 29};
            layout.fileTab = horizontalPart(row, 0, 2);
            layout.debugTab = horizontalPart(row, 1, 2);
            const int offset = std::min(body.h, 37);
            body.y += offset;
            body.h -= offset;
        }

        layout.explorer = body;
        auto debugger = body;
        if (!layout.tabbed)
        {
            layout.explorer.w = (body.w - 14) * 47 / 100;
            debugger.x = layout.explorer.right() + 14;
            debugger.w = body.right() - debugger.x;
        }
        layout.shell = debugger;
        layout.shell.h = debugger.h * 54 / 100;
        const int watchTop = std::min(layout.shell.bottom() + 12, debugger.bottom());
        layout.watches = {debugger.x, watchTop, debugger.w, debugger.bottom() - watchTop};
        return layout;
    }

    static Rect pathField(Rect box) { return {box.x + 13, box.bottom() - 72, box.w - 116, 29}; }
    static Rect shellField(Rect box) { return {box.x + 13, box.bottom() - 43, box.w - 26, 29}; }
    static Rect explorerUp(Rect box) { return {box.x + 13, box.y + 42, 97, 30}; }
    static Rect explorerMkDir(Rect box) { return {box.right() - 159, box.y + 42, 146, 30}; }
    static Rect explorerAction(Rect box) { return {box.right() - 95, box.bottom() - 72, 82, 29}; }
    static constexpr int filesTop = 116;
    static constexpr int filePitch = 25;

    int explorerRows() const
    {
        return std::max(0, (toolsLayout(desktop_.open(3)).explorer.h - filesTop - 91) / filePitch);
    }
    int maxExplorerTop() const { return std::max(0, static_cast<int>(source_.entries.size()) - explorerRows()); }

    void toolClick(t_bunny_position p, bool separate)
    {
        const auto panes = toolsLayout(separate);
        if (!separate && (panes.close.contains(p) || !panes.frame.contains(p)))
        {
            endExplorer();
            return;
        }
        if (panes.tabbed && panes.fileTab.contains(p))
        {
            debugTools_ = false;
            setFocus(browser_ != Browser::idle ? Focus::filename : Focus::none);
            return;
        }
        if (panes.tabbed && panes.debugTab.contains(p))
        {
            debugTools_ = true;
            setFocus(Focus::command);
            return;
        }
        if ((!panes.tabbed || debugTools_) && shellField(panes.shell).contains(p))
        {
            setFocus(Focus::command);
            return;
        }
        const auto explorer = panes.explorer;
        if ((panes.tabbed && debugTools_) || !explorer.contains(p))
            return;
        if (browser_ == Browser::idle)
        {
            if (Rect{explorer.x + 13, explorer.y + 44, 120, 32}.contains(p))
                startExplorer(Browser::open);
            else if (Rect{explorer.x + 144, explorer.y + 44, 160, 32}.contains(p))
                startExplorer(Browser::save);
            return;
        }
        if (explorerUp(explorer).contains(p))
        {
            if (source_.current != source_.root)
            {
                try
                {
                    source_.enter("..");
                    explorerTop_ = 0;
                    explorerSelection_ = -1;
                }
                catch (const std::exception &error) { browserNote_ = error.what(); }
            }
            return;
        }
        if (explorerMkDir(explorer).contains(p))
        {
            if (browser_ != Browser::folder)
            {
                parentBrowser_ = browser_;
                browser_ = Browser::folder;
                pathLine_.clear();
                browserNote_.clear();
            }
            setFocus(Focus::filename);
            return;
        }
        if (explorerAction(explorer).contains(p))
        {
            acceptPath();
            return;
        }
        setFocus(Focus::filename);
        if (p.y >= explorer.y + filesTop && p.y < explorer.y + filesTop + explorerRows() * filePitch)
            chooseEntry((p.y - explorer.y - filesTop) / filePitch + explorerTop_);
    }

    void drawExplorer(Rect box)
    {
        draw_.frame(box, color::panel, color::rule);
        const auto title = browser_ == Browser::save ? "Enregistrer sous" : browser_ == Browser::folder
            ? "Créer un dossier" : browser_ == Browser::open ? "Ouvrir un fichier" : "Explorateur";
        draw_.text(box.x + 13, box.y + 10, title, color::text, 20);
        if (browser_ == Browser::idle)
        {
            button({box.x + 13, box.y + 44, 120, 32}, "Ouvrir…", {}, commandColor::file);
            button({box.x + 144, box.y + 44, 160, 32}, "Enregistrer sous…", {}, commandColor::file);
            draw_.text(box.x + 13, box.y + 95, fitMono(source_.root.string(), (box.w - 26) / 8),
                       color::muted, 14, true);
            return;
        }
        button(explorerUp(box), "Parent", {}, commandColor::file, false, source_.current != source_.root);
        button(explorerMkDir(box), "Nouveau dossier", {}, commandColor::file);
        draw_.text(box.x + 13, box.y + 85, fitMono(source_.current.string(), (box.w - 26) / 8),
                   color::muted, 14, true);
        draw_.rect({box.x + 1, box.y + filesTop - 8, box.w - 2, 1}, color::rule);
        for (int row = 0; row < explorerRows(); ++row)
        {
            const auto index = explorerTop_ + row;
            if (index >= static_cast<int>(source_.entries.size()))
                break;
            const auto &entry = source_.entries[index];
            const Rect item{box.x + 6, box.y + filesTop + row * filePitch, box.w - 12, filePitch - 1};
            if (item.contains(hover_) || index == explorerSelection_)
                draw_.rect(item, color::selection);
            const std::string prefix = entry.directory ? "  /  " : "     ";
            draw_.text(item.x + 4, item.y + 3,
                       fitMono(prefix + entry.name.string(), (item.w - 12) / 8),
                       entry.directory ? color::white : color::text, 14, true, entry.directory);
        }
        pathLine_.paint(draw_, pathField(box), "Nom ou chemin relatif", focus_ == Focus::filename);
        const std::string_view action = browser_ == Browser::open ? "Ouvrir" : browser_ == Browser::folder ? "Créer" : "Écrire";
        button(explorerAction(box), action, {}, commandColor::file, true);
        if (!browserNote_.empty())
            draw_.text(box.x + 13, box.bottom() - 30, fitMono(browserNote_, (box.w - 26) / 8),
                       color::gold, 14, true);
    }

    void drawWatches(Rect box)
    {
        draw_.frame(box, color::panel, color::rule);
        draw_.text(box.x + 13, box.y + 10, "Variables surveillées", color::text, 18);
        std::string display;
        for (const auto &watch : watches_)
        {
            display += "#" + std::to_string(watch.id) + " " + watch.source + "  ";
            auto value = frame() ? machine_->inspect(watch.expression) : std::nullopt;
            if (value)
                display += "= " + value->value + (value->expired ? "  · hors portée" : "");
            else
                display += "· indisponible";
            display += '\n';
        }
        if (!breaks_.empty())
        {
            display += "\x1b[1mPoints d'arrêt\x1b[0m\n";
            for (const auto &rule : breaks_)
                display += "#" + std::to_string(rule.id) + " si " + rule.source + "\n";
        }
        if (display.empty())
            display = "watch fonction.variable   ·   break if expression";
        vt::paint(draw_, {box.x + 13, box.y + 44, box.w - 26, box.h - 50}, display);
    }

    void drawTools(bool separate)
    {
        const auto panes = toolsLayout(separate);
        if (!separate)
        {
            draw_.frame(panes.frame, color::background, color::rule);
            draw_.text(panes.frame.x + 12, panes.frame.y + 9, "Fichiers et débogage", color::text, 20,
                       false, false, panes.frame.w - 65);
            button(panes.close, "×", {}, commandColor::close);
        }
        if (panes.tabbed)
        {
            button(panes.fileTab, "Fichiers", {}, commandColor::file, !debugTools_);
            button(panes.debugTab, "Débogage", {}, commandColor::debug, debugTools_);
        }
        if (!panes.tabbed || !debugTools_)
            drawExplorer(panes.explorer);
        if (panes.tabbed && !debugTools_)
            return;

        const auto shell = panes.shell;
        draw_.frame(shell, color::panel, color::rule);
        draw_.text(shell.x + 13, shell.y + 10, "Console de débogage", color::text, 20,
                   false, false, shell.w - 26);
        vt::paint(draw_, {shell.x + 13, shell.y + 46, shell.w - 26, shell.h - 101},
                  shellLog_.empty() ? "help   ·   Commandes disponibles" : shellLog_);
        commandLine_.paint(draw_, shellField(shell), "Commande", focus_ == Focus::command);
        drawWatches(panes.watches);
    }

    void drawTransport()
    {
        const auto *state = frame();
        const bool canAdvance = modified() || (state && !state->finished && !state->error);
        const bool canPlay = modified() || (state && !state->error && (!state->finished || state->steps > 0));
        button(resetButton, "Repartir", "F6", commandColor::step);
        button(backButton, "Retour", "F7", commandColor::step, false, state && machine_->canPrevious());
        button(nextButton, nextButton.w < 94 ? "Suivant" : "Pas suivant", "F8",
               commandColor::step, true, canAdvance);
        button(playButton, playing_ ? "Pause" : "Lecture", "F5", commandColor::playback, false, canPlay);
        constexpr std::array<std::string_view, 3> speeds{"1×", "2×", "0,5×"};
        button(speedButton, speeds[speed_], {}, commandColor::playback);

        std::string status;
        if (state)
        {
            status = "Pas " + std::to_string(state->steps);
            if (const int line = machine_->nextLine(); line > 0 && !state->error)
                status += " · Ligne " + std::to_string(line);
            else if (state->finished)
                status += " · Fin";
        }
        if (!fileNote_.empty())
            status = status.empty() ? fileNote_ : status + "  ·  " + fileNote_;
        draw_.text(statusArea.x, statusArea.y + 2, fitMono(status, statusArea.w / 7), color::muted, 12, true);
    }

    void drawAuxHeader(std::size_t index, std::string_view title)
    {
        const auto area = windowArea(index);
        const int margin = windowMargin(area);
        const Rect close = auxiliaryClose(index);
        draw_.text(margin, 13, title, color::text, 25, false, false, close.x - margin - 8);
        draw_.rect({margin, 53, area.w - 2 * margin, 1}, color::rule);
        button(close, "×", {}, commandColor::close);
    }

    void draw()
    {
        const auto realHover = hover_;
        for (std::size_t i = 0; i < desktop_.count(); ++i)
        {
            auto *window = desktop_.at(i);
            if (!window)
                continue;
            hover_ = hoverWindow_ == i ? realHover : t_bunny_position{-1, -1};
            draw_.target(window->buffer);
            bunny_clear(&window->buffer, color::background);
            if (studying_)
            {
                if (i == 0)
                {
                    drawTitle("PARCOURS", commandColor::course);
                    button(modeButton, "Éditeur", {}, commandColor::course);
                    course_.draw(draw_, hover_, false);
                }
                else if (i == 1)
                {
                    drawAuxHeader(i, "Suivi des exercices");
                    course_.draw(draw_, hover_, true);
                }
                else
                    drawAuxHeader(i, "Parcours en cours");
                bunny_display(window);
                continue;
            }
            switch (Desktop::view(i))
            {
            case View::editor:
                drawHeader();
                if (showingInMain(View::editor)) drawCode();
                if (showingInMain(View::memory)) drawMemory();
                if (showingInMain(View::terminal))
                {
                    drawCompile(mainCompiler());
                    drawTerminal(mainTerminal());
                }
                drawTransport();
                if (fileMenuOpen_) drawFileMenu();
                if (toolsOverlay_) drawTools(false);
                break;
            case View::memory:
                drawAuxHeader(i, "Mémoire");
                drawMemory();
                break;
            case View::terminal:
                drawAuxHeader(i, "Terminal");
                drawCompile(fullCompiler(auxiliaryBody(i)));
                drawTerminal(fullTerminal(auxiliaryBody(i)));
                break;
            case View::tools:
                drawAuxHeader(i, "Fichiers et débogage");
                drawTools(true);
                break;
            case View::watches:
                drawAuxHeader(i, "Surveillance");
                drawWatches(auxiliaryBody(i));
                break;
            }
            bunny_display(window);
        }
        hover_ = realHover;
    }

};

t_bunny_response loop(void *data) { return static_cast<App *>(data)->tick(); }
t_bunny_response inputEvent(const t_bunny_event *event, void *data)
{
    return static_cast<App *>(data)->inputEvent(*event);
}
t_bunny_response click(t_bunny_event_state state, t_bunny_mouse_button button, void *data)
{
    return static_cast<App *>(data)->click(state, button);
}
t_bunny_response wheel(int id, int delta, void *data)
{
    return static_cast<App *>(data)->wheel(id, delta);
}
t_bunny_response resize(const t_bunny_window *window, const t_bunny_position *size, void *data)
{
    return static_cast<App *>(data)->resize(window, *size);
}

t_bunny_response close(const t_bunny_window *window, void *data)
{
    return static_cast<App *>(data)->close(window);
}

t_bunny_response motion(const t_bunny_position *, void *data)
{
    return static_cast<App *>(data)->moved(bunny_get_window());
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        bool windowed = false;
        fs::path target;
        fs::path courseRoot;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument(argv[i]);
            if (argument == "--help")
            {
                std::cout << "Usage : " << argv[0]
                          << " [--fenetre] [--parcours dossier-ou-manifeste.tsv] [dossier-source | fichier.c]\n";
                return 0;
            }
            if (argument == "--fenetre")
                windowed = true;
            else if (argument == "--parcours")
            {
                if (++i == argc)
                    throw std::runtime_error("Indique un dossier de parcours ou un fichier parcours.tsv.");
                courseRoot = fs::canonical(argv[i]);
            }
            else if (target.empty())
                target = argument;
            else
                throw std::runtime_error("Un dossier source ou un fichier est attendu.");
        }

        std::error_code ec;
        auto executable = fs::canonical("/proc/self/exe", ec);
        if (ec)
            executable = fs::absolute(argv[0]);
        fs::path root = executable.parent_path() / "programmes";
        fs::path file;
        if (!target.empty())
        {
            target = fs::canonical(target);
            if (fs::is_directory(target))
                root = target;
            else if (fs::is_regular_file(target))
            {
                file = target;
                root = target.parent_path();
            }
            else
                throw std::runtime_error("Un dossier source ou un fichier ordinaire est attendu.");
        }

        const bool startCourse = !courseRoot.empty();
        if (!startCourse)
            courseRoot = executable.parent_path() / "parcours";
        Desktop desktop(windowed);
        App app(desktop, executable.parent_path() / "assets", root, file, courseRoot, startCourse);
        bunny_set_event_response(inputEvent);
        bunny_set_click_response(click);
        bunny_set_move_response(motion);
        bunny_set_wheel_response(wheel);
        bunny_set_resize_response(resize);
        bunny_set_close_response(close);
        bunny_set_loop_main_function(loop);
        return bunny_loop_mw(desktop.events(), desktop.count(), 35, &app) == EXIT_ON_ERROR ? 1 : 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
