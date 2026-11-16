#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

inline std::string readSource(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Impossible d'ouvrir " + path.string());
    std::string code(32769, '\0');
    stream.read(code.data(), static_cast<std::streamsize>(code.size()));
    code.resize(static_cast<std::size_t>(stream.gcount()));
    if (code.size() > 32768 || stream.bad() || code.find('\0') != std::string::npos)
        throw std::runtime_error("Un fichier C en texte de 32 Kio maximum est attendu.");
    if (code.starts_with("\xEF\xBB\xBF"))
        code.erase(0, 3);
    return code;
}

class SourceDirectory
{
public:
    struct Entry
    {
        fs::path name;
        bool directory;
    };

    explicit SourceDirectory(fs::path path)
        : root(fs::weakly_canonical(path)), current(root)
    {
    }

    fs::path root;
    fs::path current;
    std::vector<Entry> entries;

    bool contains(const fs::path &absolute) const
    {
        const auto relative = absolute.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    }

    fs::path resolve(const fs::path &name) const
    {
        const auto normalized = fs::weakly_canonical(current / name);
        if (!contains(normalized))
            throw std::runtime_error("Le chemin doit rester dans le dossier source.");
        return normalized;
    }

    void refresh()
    {
        entries.clear();
        if (!fs::exists(current))
            return;
        for (const auto &entry : fs::directory_iterator(current))
        {
            const auto name = entry.path().filename();
            if (name.string().starts_with('.'))
                continue;
            std::error_code error;
            const auto resolved = fs::weakly_canonical(entry.path(), error);
            if (error || !contains(resolved))
                continue;
            if (entry.is_directory(error) && !error)
                entries.push_back({name, true});
            else if ((name.extension() == ".c" || name.extension() == ".h")
                     && entry.is_regular_file(error) && !error)
                entries.push_back({name, false});
        }
        std::ranges::sort(entries, [](const Entry &a, const Entry &b)
        {
            if (a.directory != b.directory)
                return a.directory;
            return a.name < b.name;
        });
    }

    void enter(const fs::path &name)
    {
        const auto previous = current;
        auto target = resolve(name);
        if (!fs::is_directory(target))
            throw std::runtime_error("Ce chemin ne désigne pas un dossier.");
        current = std::move(target);
        try { refresh(); }
        catch (...) { current = previous; throw; }
    }
};
