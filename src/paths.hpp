#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

class BstepPaths
{
public:
    explicit BstepPaths(const fs::path &executable)
        : beside_(executable.parent_path())
    {
        // Both /prefix/bin/bstep and /prefix/lib/bstep/bstep-gui have the same prefix.
        if (beside_.filename() == "bin")
            prefix_ = beside_.parent_path();
        else if (beside_.filename() == "bstep" && beside_.parent_path().filename() == "lib")
            prefix_ = beside_.parent_path().parent_path();

        if (const char *home = std::getenv("HOME"); home && fs::path(home).is_absolute())
        {
            user_ = fs::path(home) / ".bstep";
            addRoot(user_);
        }
        addRoot(beside_);
        if (!prefix_.empty())
            addRoot(prefix_ / "share" / "bstep");
        addRoot("/usr/share/bstep");
    }

    fs::path defaultSources() const
    {
        const auto portable = beside_ / "programmes";
        if (!user_.empty())
        {
            const auto personal = user_ / "programmes";
            if (isDirectory(personal))
                return personal;
            if (!isDirectory(portable))
            {
                std::error_code error;
                fs::create_directories(personal, error);
                if (!error && isDirectory(personal))
                    return personal;
            }
        }
        for (const auto &root : roots_)
            if (isDirectory(root / "programmes"))
                return root / "programmes";
        return portable;
    }

    std::vector<fs::path> courseRoots() const
    {
        std::vector<fs::path> courses;
        for (const auto &root : roots_)
            courses.push_back(root / "parcours");
        return courses;
    }

    fs::path commandPath(const fs::path &path, std::string_view subdirectory) const
    {
        std::error_code error;
        if (path.empty() || path.is_absolute() || fs::exists(path, error))
            return fs::canonical(path);

        const auto name = path.lexically_normal();
        if (name.begin() != name.end() && *name.begin() != "..")
            for (const auto &root : roots_)
                for (const auto &base : {root, root / subdirectory})
                    if (fs::exists(base / name, error))
                        return fs::canonical(base / name);
        return fs::canonical(path);
    }

    fs::path asset(std::string_view filename) const
    {
        const auto relative = fs::path("assets") / filename;
        for (const auto &root : roots_)
        {
            std::error_code error;
            if (fs::is_regular_file(root / relative, error))
                return root / relative;
        }
        return beside_ / relative;
    }

    fs::path guiExecutable() const
    {
        if (beside_.filename() == "bin")
        {
            const auto installed = prefix_ / "lib" / "bstep" / "bstep-gui";
            std::error_code error;
            if (fs::is_regular_file(installed, error))
                return installed;
        }
        return beside_ / "bstep-gui";
    }

private:
    fs::path beside_, prefix_, user_;
    std::vector<fs::path> roots_;

    void addRoot(const fs::path &path)
    {
        if (std::ranges::find(roots_, path) == roots_.end())
            roots_.push_back(path);
    }

    static bool isDirectory(const fs::path &path)
    {
        std::error_code error;
        return fs::is_directory(path, error);
    }
};
