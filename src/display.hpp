#pragma once

#include "draw.hpp"

#include <cstddef>
#include <string>

enum class View { editor, memory, terminal, tools, watches };

class UiDisplay
{
public:
    virtual ~UiDisplay() = default;
    virtual Painter &painter() = 0;
    virtual Rect area(std::size_t index) const = 0;
    virtual std::size_t count() const { return 1; }
    bool open(std::size_t index) const { return area(index).w > 0; }
    virtual void close(std::size_t) {}
    virtual void reopen() {}
    virtual void begin(std::size_t index) = 0;
    virtual void present(std::size_t index) = 0;

    virtual void copy(std::string_view text) { clipboard_ = text; }
    virtual std::string clipboard() const { return clipboard_; }

private:
    std::string clipboard_;
};
