#include "course_view.hpp"
#include "code_view.hpp"
#include "editor_keys.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace
{

constexpr unsigned accent = vt::palette[14];
constexpr int rowHeight = 38;
constexpr int textPitch = 19;
constexpr int textGlyph = 8;
constexpr int textMargin = 36;

Rect rightButton(Rect area, int width)
{
    return {area.right() - width - 12, area.y + 9, width, 29};
}

Rect browseButton(Rect nav) { return {nav.right() - 118, nav.y, 118, nav.h}; }
Rect trackingNav(Rect area, bool terminal)
{
    return terminal ? Rect{area.x + 12, area.y + terminalCellHeight, area.w - 24, terminalCellHeight}
                    : inset({area.x, area.y, area.w, 38}, 12, 9, 0);
}

Rect verifyButton(Rect area, bool terminal)
{
    return terminal ? Rect{area.x + 12, area.bottom() - 2 * terminalCellHeight, 14 * terminalCellWidth, terminalCellHeight}
                    : Rect{area.x + 12, area.bottom() - 37, 102, 28};
}

int minimumTrackingHeight(bool terminal) { return terminal ? 8 * terminalCellHeight : 167; }

Rect listRow(Rect area, int row)
{
    return {area.x + 12, area.y + 50 + row * rowHeight, area.w - 24, rowHeight - 4};
}

int listRows(Rect area)
{
    return std::max(0, (area.h - 84) / rowHeight);
}

int textRows(Rect area) { return std::max(1, (area.h - 10) / textPitch); }
int textColumns(Rect area) { return std::max(1, (area.w - textMargin - 7) / textGlyph); }
Rect codeLines(Rect code, bool terminal)
{
    return terminal ? inset(code, terminalCellWidth, 2 * terminalCellHeight, terminalCellHeight)
                    : inset(code, 1, 45, 10);
}

void scroll(int &top, int delta, int total, int visible)
{
    top = std::clamp(top + delta, 0, std::max(0, total - visible));
}

void scrollEditor(Editor &ed, int axis, int delta, int rows)
{
    if (axis)
        scroll(ed.left, delta * 4, 32768, 1);
    else
        scroll(ed.top, delta * 3, ed.lines(), rows);
}

void paintScroll(Painter &draw, Rect area, int top, int total, int rows)
{
    if (rows <= 0 || total <= rows || area.h <= 0)
        return;
    const int thumb = std::min(area.h, std::max(12, area.h * rows / total));
    const int y = area.y + (area.h - thumb) * top / (total - rows);
    draw.rect({area.right() - 4, y, 2, thumb}, color::rule);
}

std::u32string visibleInput(std::string_view input)
{
    std::string display;
    constexpr std::string_view hex = "0123456789ABCDEF";
    for (const unsigned char byte : input)
    {
        if (byte == '\r')
            display += "\\r";
        else if (byte != '\n' && byte != '\t' && (byte < ' ' || byte == 127))
        {
            display += "\\x";
            display += hex[byte / 16];
            display += hex[byte % 16];
        }
        else
            display += static_cast<char>(byte);
    }
    return decodeUtf8(display, true, false);
}

void paintText(Painter &draw, Rect area, const Editor &ed, bool focused, std::string_view placeholder = {})
{
    if (area.w < 50 || area.h < textPitch + 10)
        return;
    draw.frame(area, color::background, focused ? accent : color::type);
    const int columns = textColumns(area);
    const auto [caretRow, caretCol] = ed.location();
    const auto [low, high] = ed.selection();
    const int textX = area.x + textMargin;
    if (!focused && ed.lines() == 1 && ed.line(0).empty())
        draw.text(textX, area.y + 4, placeholder, color::type, 14, false, false, area.w - textMargin - 8);
    const int end = std::min(ed.lines(), ed.top + textRows(area));
    for (int row = ed.top; row < end; ++row)
    {
        const int y = area.y + 4 + (row - ed.top) * textPitch;
        const auto line = ed.line(row);
        const auto lineStart = ed.at(row, 0);
        const auto number = std::to_string(row + 1);
        draw.text(textX - 7 - 7 * static_cast<int>(number.size()), y + 2, number, color::type, 12, true);
        if (focused && low < high && high > lineStart && low <= lineStart + line.size())
        {
            const auto first = static_cast<int>(std::max(low, lineStart) - lineStart);
            const auto last = static_cast<int>(std::min(high - lineStart, line.size() + 1));
            const int a = std::clamp(first - ed.left, 0, columns);
            const int b = std::clamp(last - ed.left, 0, columns);
            draw.rect({textX + a * textGlyph, y + 1, (b - a) * textGlyph, textPitch}, color::selection);
        }
        if (static_cast<std::size_t>(ed.left) < line.size())
        {
            auto visible = std::u32string(line.substr(ed.left, columns));
            std::ranges::replace(visible, U' ', U'·');
            std::ranges::replace(visible, U'\t', U'→');
            draw.text(textX, y, encodeUtf8(visible), color::text, 14, true);
        }
        if (focused && row == caretRow && caretCol >= ed.left && caretCol - ed.left < columns)
            draw.caret({textX + (caretCol - ed.left) * textGlyph, y + 3, 1, 15}, accent);
    }
    paintScroll(draw, inset(area, 0, 2, 2), ed.top, ed.lines(), textRows(area));
}

} // namespace

CourseView::CourseView(std::vector<std::filesystem::path> roots, bool terminal)
    : roots_(std::move(roots)), terminal_(terminal) {}

void CourseView::enter(bool openSingle)
{
    if (course_)
        return;
    refresh();
    if (openSingle && catalog_.size() == 1)
        open(catalog_.front());
}

void CourseView::leave()
{
    focusedAnswer_.reset();
    questionSubmission_.reset();
}

void CourseView::layout(Rect main, Rect tracking)
{
    main_ = main;
    secondary_ = tracking;
    const auto body = inset(main_, 0, terminal_ ? 2 * terminalCellHeight : 44, 0);
    panes_ = {{main_.x, main_.y, main_.w, 34}, {}, {}};
    if (split())
    {
        panes_.code = body;
        panes_.tracking = secondary_;
    }
    else if (showProgress_)
        panes_.tracking = body;
    else if (body.w >= 900)
    {
        const int width = body.w * 3 / 5;
        panes_.code = {body.x, body.y, width, body.h};
        panes_.tracking = {body.x + width + 12, body.y, body.w - width - 12, body.h};
    }
    else
    {
        const int height = terminal_
            ? std::min(body.h, std::clamp(body.h / terminalCellHeight * 5 / 9, 10, 14) * terminalCellHeight)
            : std::min(260, body.h * 5 / 9);
        panes_.tracking = {body.x, body.bottom() - height, body.w, height};
        panes_.code = inset(body, 0, 0, height + (terminal_ ? 0 : 10));
    }
    scroll(catalogTop_, 0, static_cast<int>(catalog_.size()), listRows(main_));
    if (course_)
        scroll(progressTop_, 0, static_cast<int>(course_->exercises.size()), listRows(panes_.tracking));
    scroll(code_.top, 0, code_.lines(), codeLines(panes_.code, terminal_).h / codeLinePitch(terminal_));
    scroll(input_.top, 0, input_.lines(), textRows(codeLines(panes_.code, terminal_)));
    followQuestion();
    if (course_ && !showProgress_)
    {
        const auto areas = answerAreas();
        for (std::size_t i = 0; i < areas.size(); ++i)
            if (areas[i].w > 0)
                followAnswer(i, areas[i]);
    }
}

std::string CourseView::progress() const
{
    return std::to_string(std::ranges::count(course_->exercises, true, &training::Exercise::passed))
        + "/" + std::to_string(course_->exercises.size()) + " validés";
}

std::array<Rect, 2> CourseView::answerAreas() const
{
    auto area = terminal_ ? inset(panes_.tracking, 12, 5 * terminalCellHeight, 2 * terminalCellHeight)
                          : inset(panes_.tracking, 12, 118, 46);
    if (exercise().goal != training::Goal::streams || question())
        return {area, Rect{}};
    area = inset(area, 0, terminal_ ? terminalCellHeight : 20, 0);
    const int errorWidth = std::max(0, (area.w - 8) / 3);
    const Rect error{area.right() - errorWidth, area.y, errorWidth, area.h};
    area.w = std::max(0, area.w - errorWidth - 8);
    return {area, error};
}

bool CourseView::takingAnswer(bool secondary) const
{
    return course_ && !showProgress_ && secondary == split();
}

const cmini::Question *CourseView::question() const
{
    return simulation_ ? simulation_->question() : nullptr;
}

bool CourseView::busy() const
{
    return questionSubmission_ || (simulation_ && simulation_->phase() == training::Phase::running);
}

void CourseView::refresh()
{
    catalog_.clear();
    catalogNote_.clear();
    std::set<std::filesystem::path> selected;
    for (const auto &root : roots_)
    {
        try
        {
            auto entries = training::discover(root);
            if (entries.empty())
                continue;
            const auto base = std::filesystem::canonical(root);
            for (auto &entry : entries)
            {
                const auto key = entry.manifest.lexically_relative(base);
                if (selected.insert(key).second)
                    catalog_.push_back(std::move(entry));
            }
        }
        catch (const std::exception &error)
        {
            if (catalogNote_.empty())
                catalogNote_ = error.what();
        }
    }
    std::ranges::sort(catalog_, [](const auto &a, const auto &b)
        { return std::tie(a.title, a.manifest) < std::tie(b.title, b.manifest); });
    catalogTop_ = 0;
    listSelection_ = 0;
}

void CourseView::open(const training::CatalogEntry &entry)
{
    try
    {
        course_ = training::load(entry.manifest);
        const auto first = std::ranges::find(course_->exercises, false, &training::Exercise::passed);
        select(first == course_->exercises.end() ? 0 : static_cast<std::size_t>(first - course_->exercises.begin()));
    }
    catch (const std::exception &error)
    {
        course_.reset();
        catalogNote_ = error.what();
    }
}

void CourseView::select(std::size_t index)
{
    current_ = index;
    code_ = Editor(exercise().source);
    input_ = Editor("");
    input_.insert(visibleInput(exercise().input));
    for (auto &answer : answers_)
        answer = Editor("");
    feedback_.clear();
    simulation_.emplace(exercise());
    questionSubmission_.reset();
    submitted_ = false;
    pointsAnswered_ = 0;
    showInput_ = false;
    showProgress_ = false;
    focusedAnswer_.reset();
    progressTop_ = static_cast<int>(index);
    layout(main_, secondary_);
}

void CourseView::submit()
{
    if (!simulation_ || busy())
        return;
    if (simulation_->phase() == training::Phase::ready)
    {
        submitted_ = true;
        simulation_->start();
    }
    else if (const auto *point = question())
    {
        const auto &answer = answers_[0];
        std::vector<QuestionSubmission::Line> lines;
        for (int row = 0; row < answer.lines(); ++row)
        {
            const auto line = answer.line(row);
            if (line.find_first_not_of(U" \t\r") != line.npos)
                lines.push_back({row, encodeUtf8(line)});
        }
        if (lines.empty())
            report(training::Verdict::integerExpected, true);
        else
            questionSubmission_.emplace(QuestionSubmission{*simulation_, std::move(lines), point->source.line});
    }
    else
        report(simulation_->answerFinal(answers_[0].source(), answers_[1].source()), false);
}

bool CourseView::tick()
{
    if (questionSubmission_)
        return tickQuestionSubmission();
    if (!simulation_ || !simulation_->tick())
        return false;
    using training::Phase;
    switch (simulation_->phase())
    {
    case Phase::question:
        showInput_ = false;
        focusedAnswer_ = 0;
        followQuestion();
        break;
    case Phase::finished:
        if (submitted_)
            report(simulation_->answerFinal(answers_[0].source(), answers_[1].source()), false);
        break;
    case Phase::unavailable: report(training::Verdict::unavailable, false); break;
    case Phase::limit: report(training::Verdict::limit, false); break;
    default: break;
    }
    submitted_ = false;
    return true;
}

bool CourseView::tickQuestionSubmission()
{
    using training::Phase;
    using training::Verdict;
    auto &submission = *questionSubmission_;
    auto &trial = submission.simulation;
    if (trial.phase() == Phase::running && !trial.tick())
        return false;

    const auto &line = submission.lines[submission.next];
    const auto *point = trial.question();
    if (point && point->source.line == submission.sourceLine)
    {
        const auto result = trial.answerQuestion(line.text);
        if (result == Verdict::correct)
        {
            if (++submission.next < submission.lines.size())
                return false;

            // Commit any progress only after all the submitted iterations checked out.
            const auto count = static_cast<unsigned>(submission.lines.size());
            simulation_ = std::move(trial);
            questionSubmission_.reset();
            pointsAnswered_ += count;
            answers_[0] = Editor("");
            focusedAnswer_ = 0;
            report(Verdict::correct, true);
            if (count > 1)
                feedback_ = std::to_string(count) + " passages validés.";
            return true;
        }
        report(result, true);
    }
    else if (trial.phase() == Phase::limit)
        report(Verdict::limit, true);
    else if (trial.phase() == Phase::unavailable)
        report(Verdict::unavailable, true);
    else
    {
        feedback_ = "Réponse en trop pour ce point.";
        feedbackColor_ = color::gold;
    }

    focusedAnswer_ = 0;
    if (submission.lines.size() > 1)
    {
        feedback_ = "Ligne " + std::to_string(line.row + 1) + " · " + feedback_;
        auto &answer = answers_[0];
        answer.place(answer.at(line.row, 0));
        answer.endpoint(true, false, true);
        followAnswer(0, answerAreas()[0]);
    }
    questionSubmission_.reset();
    return true;
}

void CourseView::report(training::Verdict result, bool atQuestion)
{
    using training::Verdict;
    feedbackColor_ = color::gold;
    switch (result)
    {
    case Verdict::correct:
        feedback_ = atQuestion ? "Point validé." : "Réponse correcte.";
        feedbackColor_ = vt::palette[10];
        if (!atQuestion)
        {
            try { training::recordSuccess(course_->exercises[current_]); }
            catch (const std::exception &) { feedback_ = "Correct. Progression non enregistrée."; }
        }
        break;
    case Verdict::wrong: feedback_ = "Réponse incorrecte. Réessaie."; break;
    case Verdict::integerExpected:
        feedback_ = atQuestion ? "Vérifie le format et le nombre d’entiers." : "Saisis un entier en base dix.";
        break;
    case Verdict::unavailable: feedback_ = "Exercice indisponible."; break;
    case Verdict::limit: feedback_ = "Limite de simulation atteinte."; break;
    }
}

void CourseView::followQuestion()
{
    const auto *point = question();
    if (!point || panes_.code.h <= 0)
        return;
    const int row = point->source.line - 1;
    const int rows = std::max(1, codeLines(panes_.code, terminal_).h / codeLinePitch(terminal_));
    if (row < code_.top || row >= code_.top + rows)
        code_.top = std::clamp(row - rows / 2, 0, std::max(0, code_.lines() - rows));
}

void CourseView::followAnswer(std::size_t index, Rect area)
{
    auto &answer = answers_[index];
    const auto [row, column] = answer.location();
    const int rows = textRows(area);
    const int columns = textColumns(area);
    answer.top = std::min(answer.top, std::max(0, answer.lines() - rows));
    answer.top = std::clamp(answer.top, std::max(0, row - rows + 1), row);
    answer.left = std::clamp(answer.left, std::max(0, column - columns + 1), column);
}

void CourseView::draw(Painter &draw, Point hover, bool secondary) const
{
    if (secondary)
    {
        if (course_)
            drawTracking(draw, hover);
        else
            draw.text(secondary_.x + 12, secondary_.y + 16,
                      "Choisis un parcours dans la fenêtre principale.", color::muted, 16,
                      false, false, secondary_.w - 24);
        return;
    }
    if (!course_)
    {
        drawCatalog(draw, hover);
        return;
    }

    const auto nav = panes_.mainNav;
    draw.text(nav.x, nav.y + 5, course_->title, color::text, 21, false, false, nav.w - 130);
    paintButton(draw, browseButton(nav), hover, "Parcours", {}, accent);
    if (panes_.code.w > 0)
        drawCode(draw, hover);
    if (!split())
        drawTracking(draw, hover);
}

void CourseView::drawCatalog(Painter &draw, Point hover) const
{
    draw.frame(main_, color::background, color::rule);
    draw.text(main_.x + 12, main_.y + 11, "Parcours disponibles", color::text, 20,
              false, false, main_.w - 136);
    paintButton(draw, rightButton(main_, 110), hover, "Actualiser", {}, accent);
    const int rows = listRows(main_);
    const int end = std::min(static_cast<int>(catalog_.size()), catalogTop_ + rows);
    for (int index = catalogTop_; index < end; ++index)
        paintButton(draw, listRow(main_, index - catalogTop_), hover, catalog_[index].title, {},
                    accent, terminal_ && index == listSelection_);
    paintScroll(draw, {main_.x, main_.y + 50, main_.w, rows * rowHeight},
                catalogTop_, static_cast<int>(catalog_.size()), rows);
    if (catalog_.empty() && catalogNote_.empty())
    {
        draw.text(main_.x + 12, main_.y + 56, "Aucun parcours trouvé.", color::muted, 16);
        draw.text(main_.x + 12, main_.y + 82, "Ajoute un dossier avec parcours.tsv.", color::muted,
                  14, false, false, main_.w - 24);
        if (!roots_.empty())
            draw.text(main_.x + 12, main_.y + 109, roots_.front().string(), color::type,
                      12, true, false, main_.w - 24);
    }
    if (!catalogNote_.empty())
        draw.text(main_.x + 12, main_.bottom() - 28, catalogNote_, color::gold,
                  13, false, false, main_.w - 24);
}

void CourseView::drawCode(Painter &draw, Point hover) const
{
    const auto area = panes_.code;
    if (area.h < 46)
        return;
    draw.frame(area, color::background, color::rule);
    const bool hasInput = !exercise().input.empty();
    const std::string title = showInput_ ? "Entrée fournie (stdin)" : exercise().filename;
    draw.text(area.x + 12, area.y + 11, title, color::text, 17,
              false, false, area.w - (hasInput ? 144 : 24));
    if (hasInput)
        paintButton(draw, rightButton(area, 118), hover, showInput_ ? "Code source" : "Entrée fournie",
                    {}, vt::palette[11], showInput_);
    const auto lines = codeLines(area, terminal_);
    if (showInput_)
        paintText(draw, lines, input_, false);
    else
    {
        const auto *point = question();
        paintSource(draw, code_, lines, {point ? point->source.line : 0});
    }
}

void CourseView::drawTracking(Painter &draw, Point hover) const
{
    const auto area = panes_.tracking;
    if (area.h < 85)
        return;
    draw.frame(area, color::background, color::rule);
    const auto &exercises = course_->exercises;
    if (showProgress_)
    {
        draw.text(area.x + 12, area.y + 12, "Exercices · " + progress(), color::text,
                  17, false, false, area.w - 136);
        paintButton(draw, rightButton(area, 110), hover, "Répondre", {}, accent);
        const int rows = listRows(area);
        const int end = std::min(static_cast<int>(exercises.size()), progressTop_ + rows);
        for (int index = progressTop_; index < end; ++index)
        {
            const auto &item = exercises[index];
            const std::string label = std::to_string(index + 1) + ". " + item.title;
            const auto row = listRow(area, index - progressTop_);
            const unsigned tint = item.passed ? vt::palette[10] : accent;
            paintButton(draw, row, hover, label, item.passed ? "Validé" : "À faire", tint,
                        terminal_ ? index == listSelection_ : static_cast<std::size_t>(index) == current_);
        }
        paintScroll(draw, {area.x, area.y + 50, area.w, rows * rowHeight},
                    progressTop_, static_cast<int>(exercises.size()), rows);
        return;
    }

    const auto nav = trackingNav(area, terminal_);
    paintButton(draw, horizontalPart(nav, 0, 3), hover, "Exercices", {}, accent);
    paintButton(draw, horizontalPart(nav, 1, 3), hover, "Précédent", {}, accent, false, current_ > 0);
    paintButton(draw, horizontalPart(nav, 2, 3), hover, "Suivant", {}, accent, false, current_ + 1 < exercises.size());
    const auto &item = exercise();
    const std::string title = std::to_string(current_ + 1) + "/" + std::to_string(exercises.size()) + " · " + item.title;
    const auto passed = "Validé";
    const int titleY = area.y + (terminal_ ? 2 * terminalCellHeight : 45);
    draw.text(area.x + 12, titleY, title, color::text, 17,
              false, false, area.w - (item.passed ? 82 : 24));
    if (item.passed)
        draw.text(area.right() - 62, titleY + 3, passed, vt::palette[10], 14);
    if (area.h < minimumTrackingHeight(terminal_))
        return;
    const auto *point = question();
    std::string prompt;
    std::string pointDetail;
    std::string_view detail;
    if (point)
    {
        prompt = "Point question " + std::to_string(pointsAnswered_ + 1)
            + " · ligne " + std::to_string(point->source.line);
        pointDetail = point->text + (point->expressions.size() == 1 ? " · un entier par passage"
            : " · entiers espacés, une ligne par passage");
        detail = pointDetail;
    }
    else switch (item.goal)
    {
    case training::Goal::output:
        prompt = "Qu'affiche le programme sur stdout ?";
        detail = "Les espaces et sauts de ligne comptent.";
        break;
    case training::Goal::errorOutput:
        prompt = "Qu'affiche le programme sur stderr ?";
        detail = "Les espaces et sauts de ligne comptent.";
        break;
    case training::Goal::streams:
        prompt = "Que produit le programme sur chaque flux ?";
        detail = "Les espaces et sauts de ligne comptent.";
        break;
    case training::Goal::returned:
        prompt = "Quel entier main renvoie-t-il ?";
        break;
    case training::Goal::value:
        prompt = "Quelle valeur entière à la fin du programme ?";
        detail = item.expression;
        break;
    }
    draw.text(area.x + 12, area.y + (terminal_ ? 3 * terminalCellHeight : 75),
              prompt, point ? accent : color::text, 15, false, false, area.w - 24);
    draw.text(area.x + 12, area.y + (terminal_ ? 4 * terminalCellHeight : 96),
              detail, color::muted, 13, false, false, area.w - 24);
    const auto areas = answerAreas();
    const bool streams = item.goal == training::Goal::streams && !point;
    const auto placeholder = point ? "Une ligne par passage" : "Ta réponse";
    constexpr std::array<std::string_view, 2> labels{"stdout", "stderr"};
    for (std::size_t i = 0; i < areas.size(); ++i)
    {
        const auto field = areas[i];
        if (field.w <= 0)
            continue;
        if (streams)
            draw.text(field.x, field.y - (terminal_ ? terminalCellHeight : 20), labels[i], i ? vt::palette[11] : color::muted, 13, true);
        paintText(draw, field, answers_[i], focusedAnswer_ == i && !busy(), placeholder);
    }
    const auto verify = verifyButton(area, terminal_);
    paintButton(draw, verify, hover, "Vérifier", terminal_ ? "F4" : "", accent, true, !busy());
    const auto hint = terminal_ ? (streams ? "Maj+Tab change de flux" : "F4 pour vérifier")
                                : "Ctrl+Entrée pour vérifier";
    const auto note = busy() ? "Simulation en cours…" : feedback_.empty() ? hint : feedback_;
    draw.text(verify.right() + 12, verify.y + 5, note, busy() || feedback_.empty() ? color::muted : feedbackColor_,
              14, false, false, area.right() - verify.right() - 24);
}

void CourseView::click(Point p, bool secondary, bool shift)
{
    if (!course_)
    {
        if (secondary)
            return;
        if (rightButton(main_, 110).contains(p))
            refresh();
        else
            for (int i = 0; i < listRows(main_) && catalogTop_ + i < static_cast<int>(catalog_.size()); ++i)
                if (listRow(main_, i).contains(p))
                {
                    open(catalog_[catalogTop_ + i]);
                    break;
                }
        return;
    }
    if (!secondary)
    {
        if (browseButton(panes_.mainNav).contains(p))
        {
            leave();
            course_.reset();
            simulation_.reset();
            refresh();
            return;
        }
        if (!exercise().input.empty() && panes_.code.w > 0 && rightButton(panes_.code, 118).contains(p))
        {
            showInput_ = !showInput_;
            focusedAnswer_.reset();
            return;
        }
    }
    if (secondary == split())
        clickTracking(p, shift);
    else
        focusedAnswer_.reset();
}

void CourseView::clickTracking(Point p, bool shift)
{
    const auto area = panes_.tracking;
    if (showProgress_)
    {
        if (rightButton(area, 110).contains(p))
            showProgress_ = false;
        else
        {
            const int end = std::min(static_cast<int>(course_->exercises.size()), progressTop_ + listRows(area));
            for (int index = progressTop_; index < end; ++index)
                if (listRow(area, index - progressTop_).contains(p))
                {
                    if (static_cast<std::size_t>(index) == current_)
                        showProgress_ = false;
                    else
                        select(index);
                    break;
                }
        }
        layout(main_, secondary_);
        return;
    }

    const auto nav = trackingNav(area, terminal_);
    const auto verify = verifyButton(area, terminal_);
    focusedAnswer_.reset();
    if (horizontalPart(nav, 0, 3).contains(p))
    {
        showProgress_ = true;
        progressTop_ = listSelection_ = static_cast<int>(current_);
        layout(main_, secondary_);
    }
    else if (horizontalPart(nav, 1, 3).contains(p) && current_ > 0)
        select(current_ - 1);
    else if (horizontalPart(nav, 2, 3).contains(p) && current_ + 1 < course_->exercises.size())
        select(current_ + 1);
    else if (area.h >= minimumTrackingHeight(terminal_) && verify.contains(p))
        submit();
    else if (!busy())
    {
        const auto areas = answerAreas();
        for (std::size_t i = 0; i < areas.size(); ++i)
        {
            const auto field = areas[i];
            if (!field.contains(p))
                continue;
            auto &answer = answers_[i];
            const int row = (p.y - field.y - 4) / textPitch + answer.top;
            const int bias = terminal_ ? 0 : textGlyph / 2;
            const int column = std::max(0, (p.x - field.x - textMargin + bias) / textGlyph) + answer.left;
            answer.place(answer.at(row, column), shift);
            focusedAnswer_ = i;
            followAnswer(i, field);
            break;
        }
    }
}

void CourseView::wheel(Point p, bool secondary, int axis, int delta)
{
    if (!course_)
    {
        if (!secondary)
            scroll(catalogTop_, -delta * 3, static_cast<int>(catalog_.size()), listRows(main_));
    }
    else if (secondary == split() && panes_.tracking.contains(p))
    {
        if (showProgress_)
            scroll(progressTop_, -delta * 3, static_cast<int>(course_->exercises.size()), listRows(panes_.tracking));
        else
        {
            const auto areas = answerAreas();
            for (std::size_t i = 0; i < areas.size(); ++i)
                if (areas[i].contains(p))
                    scrollEditor(answers_[i], axis, -delta, textRows(areas[i]));
        }
    }
    else if (!secondary && codeLines(panes_.code, terminal_).contains(p))
    {
        const auto lines = codeLines(panes_.code, terminal_);
        if (showInput_)
            scrollEditor(input_, axis, -delta, textRows(lines));
        else
            scrollEditor(code_, axis, -delta, lines.h / codeLinePitch(terminal_));
    }
}

bool CourseView::navigateList(Key key)
{
    if (key != Key::up && key != Key::down && key != Key::enter)
        return false;
    const int count = static_cast<int>(course_ ? course_->exercises.size() : catalog_.size());
    if (count == 0)
        return true;
    auto &top = course_ ? progressTop_ : catalogTop_;
    const int rows = std::max(1, listRows(course_ ? panes_.tracking : main_));
    if (key == Key::enter)
    {
        if (!course_)
            open(catalog_[listSelection_]);
        else if (static_cast<std::size_t>(listSelection_) != current_)
            select(static_cast<std::size_t>(listSelection_));
        else
        {
            showProgress_ = false;
            layout(main_, secondary_);
        }
    }
    else
    {
        listSelection_ = std::clamp(listSelection_ + (key == Key::up ? -1 : 1), 0, count - 1);
        top = std::clamp(top, std::max(0, listSelection_ - rows + 1), listSelection_);
    }
    return true;
}

void CourseView::key(Key key, bool secondary, bool control, bool shift, KillBuffer &kills, UiDisplay &display)
{
    if (terminal_ && key == Key::f2 && course_)
    {
        showProgress_ = !showProgress_;
        if (showProgress_)
            progressTop_ = listSelection_ = static_cast<int>(current_);
        layout(main_, secondary_);
        return;
    }
    if (terminal_ && !control && (!course_ || showProgress_) && navigateList(key))
        return;
    if (key == Key::escape)
    {
        if (showProgress_ && course_)
        {
            showProgress_ = false;
            layout(main_, secondary_);
        }
        focusedAnswer_.reset();
    }
    else if (takingAnswer(secondary) && ((control && key == Key::enter) || (terminal_ && key == Key::f4)))
        submit();
    else if (terminal_ && takingAnswer(secondary) && !busy() && key == Key::tab && shift)
    {
        const auto areas = answerAreas();
        focusedAnswer_ = areas[1].w > 0 && focusedAnswer_ == 0 ? 1 : 0;
        followAnswer(*focusedAnswer_, areas[*focusedAnswer_]);
    }
    else if (takingAnswer(secondary) && focusedAnswer_ && !busy())
    {
        const auto index = *focusedAnswer_;
        const auto field = answerAreas()[index];
        auto &answer = answers_[index];
        const auto revision = answer.revision();
        if (editKey(answer, key, control, shift, textRows(field), false, kills, display))
        {
            if (answer.revision() != revision)
                feedback_.clear();
            followAnswer(index, field);
        }
    }
    else if (!secondary && course_ && panes_.code.w > 0 && !control)
    {
        auto &ed = showInput_ ? input_ : code_;
        const auto lines = codeLines(panes_.code, terminal_);
        const int rows = showInput_ ? textRows(lines) : std::max(1, lines.h / codeLinePitch(terminal_));
        if (key == Key::up || key == Key::down)
            scroll(ed.top, key == Key::up ? -1 : 1, ed.lines(), rows);
        else if (key == Key::pageUp || key == Key::pageDown)
            scroll(ed.top, key == Key::pageUp ? -rows : rows, ed.lines(), rows);
        else if (key == Key::left || key == Key::right)
            scroll(ed.left, key == Key::left ? -4 : 4, 32768, 1);
    }
}

void CourseView::insert(std::u32string_view text, bool secondary)
{
    if (!takingAnswer(secondary) || busy())
        return;
    if (!focusedAnswer_ && question())
        focusedAnswer_ = 0;
    if (!focusedAnswer_)
        return;
    const auto index = *focusedAnswer_;
    if (answers_[index].insert(text))
    {
        feedback_.clear();
        followAnswer(index, answerAreas()[index]);
    }
}
