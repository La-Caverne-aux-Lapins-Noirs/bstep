#pragma once

#include "course.hpp"
#include "draw.hpp"
#include "editor.hpp"

#include <array>

class UiDisplay;
class KillBuffer;

class CourseView
{
public:
    CourseView(std::vector<std::filesystem::path> roots, bool terminal);

    void enter(bool openSingle = false);
    void leave();
    void layout(Rect main, Rect tracking);
    bool tick();
    void draw(Painter &painter, Point hover, bool secondary) const;
    void click(Point point, bool secondary, bool shift);
    void wheel(Point point, bool secondary, int axis, int delta);
    void key(Key key, bool secondary, bool control, bool shift, KillBuffer &kills, UiDisplay &display);
    void insert(std::u32string_view text, bool secondary);

private:
    struct Layout
    {
        Rect mainNav, code, tracking;
    };

    struct QuestionSubmission
    {
        struct Line
        {
            int row;
            std::string text;
        };

        training::Simulation simulation;
        std::vector<Line> lines;
        int sourceLine;
        std::size_t next = 0;
    };

    std::vector<std::filesystem::path> roots_;
    std::vector<training::CatalogEntry> catalog_;
    std::optional<training::Course> course_;
    std::size_t current_ = 0;
    int catalogTop_ = 0;
    int progressTop_ = 0;
    int listSelection_ = 0;
    Editor code_{""};
    Editor input_{""};
    std::array<Editor, 2> answers_{Editor(""), Editor("")};
    std::optional<std::size_t> focusedAnswer_;
    bool terminal_;
    bool showInput_ = false;
    bool showProgress_ = false;
    Rect main_{}, secondary_{};
    Layout panes_{};
    std::string catalogNote_;
    std::string feedback_;
    unsigned feedbackColor_ = color::text;
    std::optional<training::Simulation> simulation_;
    std::optional<QuestionSubmission> questionSubmission_;
    bool submitted_ = false;
    unsigned pointsAnswered_ = 0;

    bool split() const { return secondary_.w > 0; }
    const training::Exercise &exercise() const { return course_->exercises[current_]; }
    std::string progress() const;
    std::array<Rect, 2> answerAreas() const;
    const cmini::Question *question() const;
    bool busy() const;
    bool takingAnswer(bool secondary) const;
    void refresh();
    void open(const training::CatalogEntry &entry);
    void select(std::size_t index);
    void submit();
    bool tickQuestionSubmission();
    void report(training::Verdict result, bool atQuestion);
    void followQuestion();
    void followAnswer(std::size_t index, Rect area);
    void drawCatalog(Painter &painter, Point hover) const;
    void drawCode(Painter &painter, Point hover) const;
    void drawTracking(Painter &painter, Point hover) const;
    void clickTracking(Point point, bool shift);
    bool navigateList(Key key);
};
