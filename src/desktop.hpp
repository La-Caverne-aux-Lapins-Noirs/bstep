#pragma once

#include <lapin.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

enum class View { editor, memory, terminal, tools, watches };

class Desktop
{
public:
    explicit Desktop(bool windowed)
        : windowed_(windowed)
    {
        // Clone outputs share the same origin. Use one window for that desktop region.
        if (const auto *reported = bunny_list_monitors())
            for (auto monitor = reported; monitor->w && monitor->h; ++monitor)
            {
                const auto duplicate = std::ranges::any_of(monitors_, [&](const auto &old)
                    { return old.x == monitor->x && old.y == monitor->y; });
                if (!duplicate && monitor->w > 0 && monitor->h > 0)
                    monitors_.push_back(*monitor);
            }
        if (monitors_.empty())
        {
            const auto *size = bunny_get_screen_resolution();
            monitors_.push_back({0, 0, size && size->x > 0 ? size->x : 1280,
                                   size && size->y > 0 ? size->y : 800});
        }
        else
        {
            const auto origin = bunny_get_monitors_origin();
            const auto main = std::ranges::find_if(monitors_, [&](const auto &area)
                { return area.x == origin.x && area.y == origin.y; });
            if (main != monitors_.end())
                std::rotate(monitors_.begin(), main, main + 1);
        }

        width_ = monitors_.front().w;
        height_ = monitors_.front().h;
        for (const auto &area : monitors_)
        {
            width_ = std::min(width_, area.w);
            height_ = std::min(height_, area.h);
        }
        if (windowed_)
        {
            width_ = std::max(1, width_ * 8 / 10);
            height_ = std::max(1, height_ * 8 / 10);
        }
        windows_.resize(monitors_.size());
        events_.resize(monitors_.size());
        reopen();
        if (!events_[0])
            throw std::runtime_error("Impossible d'ouvrir la fenêtre principale.");
    }

    static View view(std::size_t index)
    {
        switch (index)
        {
        case 0: return View::editor;
        case 1: return View::memory;
        case 2: return View::terminal;
        case 3: return View::tools;
        default: return View::watches;
        }
    }

    static const char *title(std::size_t index)
    {
        switch (view(index))
        {
        case View::editor: return "bstep";
        case View::memory: return "bstep - Memoire";
        case View::terminal: return "bstep - Terminal";
        case View::tools: return "bstep - Fichiers et debogage";
        case View::watches: return "bstep - Surveillance";
        }
        return "bstep";
    }

    t_bunny_window *at(std::size_t index) const
    {
        return index < events_.size() ? events_[index] : nullptr;
    }
    bool open(std::size_t index) const { return at(index) != nullptr; }
    std::size_t count() const { return events_.size(); }
    t_bunny_window **events() { return events_.data(); }

    std::size_t index(const t_bunny_window *window) const
    {
        return static_cast<std::size_t>(std::ranges::find(events_, window) - events_.begin());
    }

    void close(std::size_t index)
    {
        if (index == 0 || index >= count())
            return;
        // Called from the loop callback, after LibLapin has finished dispatching events.
        events_[index] = nullptr;
        windows_[index].reset();
    }

    void reopen()
    {
        for (std::size_t i = 0; i < count(); ++i)
        {
            if (open(i))
                continue;
            const auto style = windowed_ ? DEFAULT_WIN_STYLE : NO_BORDER;
            windows_[i].reset(bunny_start_style(width_, height_, style, title(i)));
            if (!windows_[i])
                continue;
            const auto &area = monitors_[i];
            bunny_move_window(windows_[i].get(), {area.x + (area.w - width_) / 2,
                                                 area.y + (area.h - height_) / 2});
            events_[i] = windows_[i].get();
        }
    }

private:
    struct DeleteWindow
    {
        void operator()(t_bunny_window *window) const { bunny_stop(window); }
    };
    std::vector<t_bunny_area> monitors_;
    std::vector<std::unique_ptr<t_bunny_window, DeleteWindow>> windows_;
    std::vector<t_bunny_window *> events_;
    bool windowed_;
    int width_ = 0;
    int height_ = 0;
};
