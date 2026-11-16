#include "course.hpp"
#include "source_directory.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string_view>
#include <tuple>

namespace training
{
namespace
{

constexpr auto manifestName = "parcours.tsv";

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string readText(const fs::path &path, std::size_t limit)
{
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Fichier introuvable · " + path.filename().string());
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Fichier illisible · " + path.filename().string());
    std::string text(limit + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (file.bad() || text.size() > limit || text.find('\0') != std::string::npos)
        throw std::runtime_error("Fichier texte invalide · " + path.filename().string());
    return text;
}

std::string readManifest(const fs::path &path)
{
    auto text = readText(path, 128 * 1024);
    if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
    return text;
}

bool nextRow(std::istream &file, std::string &line, int &lineNumber)
{
    while (std::getline(file, line))
    {
        ++lineNumber;
        if (line.ends_with('\r'))
            line.pop_back();
        const auto text = trim(line);
        if (!text.empty() && !text.starts_with('#'))
            return true;
    }
    return false;
}

std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> result;
    while (true)
    {
        const auto tab = line.find('\t');
        result.push_back(trim(line.substr(0, tab)));
        if (tab == line.npos)
            return result;
        line.remove_prefix(tab + 1);
    }
}

fs::path resolveFile(const SourceDirectory &folder, std::string_view name)
{
    const fs::path path(name);
    if (path.empty() || path.is_absolute())
        throw std::runtime_error("Un chemin relatif au parcours est attendu.");
    const auto resolved = folder.resolve(path);
    if (!fs::is_regular_file(resolved))
        throw std::runtime_error("Fichier introuvable · " + path.string());
    return resolved;
}

fs::path progressRoot()
{
    if (const char *value = std::getenv("XDG_STATE_HOME"); value && fs::path(value).is_absolute())
        return fs::path(value) / "bstep" / "parcours";
    if (const char *value = std::getenv("HOME"); value && fs::path(value).is_absolute())
        return fs::path(value) / ".local" / "state" / "bstep" / "parcours";
    return {};
}

// Use a stable identifier across launches. File content changes invalidate prior success.
std::string fingerprint(std::initializer_list<std::string_view> fields)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto field : fields)
    {
        for (const unsigned char byte : field)
            hash = (hash ^ byte) * 1099511628211ull;
        hash *= 1099511628211ull;
    }
    return std::to_string(hash);
}

std::optional<int> parseInteger(std::string_view text)
{
    text = trim(text);
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
        if (text.starts_with('-'))
            return std::nullopt;
    }
    if (text.empty())
        return std::nullopt;
    int number{};
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, number);
    if (result.ec != std::errc{} || result.ptr != end)
        return std::nullopt;
    return number;
}

Verdict compareOutput(std::string_view output, std::string_view answer)
{
    if (encodeUtf8(decodeUtf8(output, true, false)) != output)
        return Verdict::unavailable;
    return output == answer ? Verdict::correct : Verdict::wrong;
}

} // namespace

std::vector<CatalogEntry> discover(const fs::path &root)
{
    std::vector<CatalogEntry> entries;
    const auto add = [&entries](const fs::path &manifest)
    {
        const auto path = fs::canonical(manifest);
        if (std::ranges::find(entries, path, &CatalogEntry::manifest) != entries.end())
            return;
        std::string title = path.parent_path().filename().string();
        try
        {
            std::istringstream file(readManifest(path));
            std::string line;
            int number = 0;
            if (nextRow(file, line, number))
                title = trim(line);
        }
        catch (const std::runtime_error &) {} // Keep the entry so opening it can report the error.
        entries.push_back({path, std::move(title)});
    };
    if (fs::is_regular_file(root))
        add(root);
    else if (fs::is_directory(root))
    {
        if (fs::is_regular_file(root / manifestName))
            add(root / manifestName);
        for (const auto &entry : fs::directory_iterator(root))
            if (!entry.path().filename().string().starts_with('.')
                && entry.is_directory() && fs::is_regular_file(entry.path() / manifestName))
                add(entry.path() / manifestName);
    }
    std::ranges::sort(entries, [](const CatalogEntry &a, const CatalogEntry &b)
        { return std::tie(a.title, a.manifest) < std::tie(b.title, b.manifest); });
    return entries;
}

Course load(const fs::path &manifest)
{
    const auto path = fs::canonical(manifest);
    const SourceDirectory folder(path.parent_path());
    auto state = progressRoot();
    if (!state.empty())
        state /= fingerprint({"v1", path.string()});
    std::istringstream file(readManifest(path));
    std::string line;
    int number = 0;
    if (!nextRow(file, line, number) || line.find('\t') != line.npos)
        throw std::runtime_error("La première ligne utile doit contenir le titre du parcours.");
    Course course{std::string(trim(line)), {}};
    std::map<std::string, unsigned> occurrences;
    while (nextRow(file, line, number))
    {
        try
        {
            const auto columns = fields(line);
            if (columns.size() < 3 || columns.size() > 4 || columns[1].empty())
                throw std::runtime_error("Indique le fichier, le titre et le type, séparés par des tabulations.");
            const auto source = resolveFile(folder, columns[0]);
            if (source.extension() != ".c")
                throw std::runtime_error("Un fichier source .c est attendu.");
            Exercise exercise{};
            exercise.filename = columns[0];
            exercise.title = columns[1];
            exercise.source = readSource(source);
            const auto goal = columns[2];
            if (goal == "sortie")
                exercise.goal = Goal::output;
            else if (goal == "stderr")
                exercise.goal = Goal::errorOutput;
            else if (goal == "flux")
                exercise.goal = Goal::streams;
            else if (goal == "retour")
                exercise.goal = Goal::returned;
            else if (goal.starts_with("valeur:") && !trim(goal.substr(7)).empty())
            {
                exercise.goal = Goal::value;
                exercise.expression = trim(goal.substr(7));
            }
            else
                throw std::runtime_error("Type attendu · sortie, stderr, flux, retour ou valeur:expression.");
            if (columns.size() == 4 && !columns[3].empty())
                exercise.input = readText(resolveFile(folder, columns[3]), 8192);
            if (!state.empty())
            {
                auto id = fingerprint({exercise.filename, exercise.title, goal, exercise.source, exercise.input});
                if (const auto occurrence = ++occurrences[id]; occurrence > 1)
                    id += "-" + std::to_string(occurrence);
                exercise.progress = state / id;
                std::error_code error;
                exercise.passed = fs::is_regular_file(exercise.progress, error);
            }
            course.exercises.push_back(std::move(exercise));
        }
        catch (const std::runtime_error &error)
        {
            throw std::runtime_error("Ligne " + std::to_string(number) + " · " + error.what());
        }
    }
    if (course.exercises.empty())
        throw std::runtime_error("Ce parcours doit contenir au moins un exercice.");
    return course;
}

void recordSuccess(Exercise &exercise)
{
    exercise.passed = true;
    if (exercise.progress.empty())
        throw std::runtime_error("Dossier de progression introuvable.");
    // Each success is independent, so parallel launches cannot overwrite other results.
    fs::create_directories(exercise.progress.parent_path());
    std::ofstream marker(exercise.progress, std::ios::app);
    if (!marker)
        throw std::runtime_error("Impossible d'enregistrer la progression.");
}

Simulation::Simulation(const Exercise &exercise) : goal_(exercise.goal)
{
    try
    {
        if (goal_ == Goal::value)
            expression_ = cmini::compileProbe(exercise.expression);
        machine_.emplace(cmini::compile(exercise.source, true), false);
        if (!exercise.input.empty() && !machine_->submit(exercise.input))
        {
            phase_ = Phase::unavailable;
            return;
        }
        machine_->closeInput();
        if (!machine_->program().questions.empty())
            start();
    }
    catch (const cmini::Diagnostic &)
    {
        phase_ = Phase::unavailable;
    }
}

void Simulation::start()
{
    if (phase_ == Phase::ready)
        phase_ = Phase::running;
}

bool Simulation::tick()
{
    if (phase_ != Phase::running)
        return false;
    constexpr std::size_t stepLimit = 20000;
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(6);
    auto &machine = *machine_;
    while (Clock::now() < deadline)
    {
        const auto &frame = machine.frame();
        if (frame.error)
        {
            phase_ = Phase::unavailable;
            return true;
        }
        if (frame.finished)
        {
            phase_ = Phase::finished;
            return true;
        }
        if (frame.steps >= stepLimit)
        {
            phase_ = Phase::limit;
            return true;
        }
        if (const auto *point = machine.pendingQuestion())
        {
            values_.clear();
            for (const auto &expression : point->expressions)
            {
                const auto value = machine.inspect(expression, false);
                if (!value || !value->number)
                {
                    phase_ = Phase::unavailable;
                    return true;
                }
                values_.push_back(*value->number);
            }
            phase_ = Phase::question;
            return true;
        }
        if (!machine.next())
        {
            phase_ = Phase::unavailable;
            return true;
        }
    }
    return false;
}

const cmini::Question *Simulation::question() const
{
    return phase_ == Phase::question ? machine_->pendingQuestion() : nullptr;
}

Verdict Simulation::answerQuestion(std::string_view answer)
{
    if (phase_ != Phase::question)
        return Verdict::unavailable;
    bool correct = true;
    for (const int expected : values_)
    {
        answer = trim(answer);
        const auto end = answer.find_first_of(" \t\r\n");
        const auto got = parseInteger(answer.substr(0, end));
        if (!got)
            return Verdict::integerExpected;
        correct = correct && *got == expected;
        answer = end == answer.npos ? std::string_view{} : answer.substr(end);
    }
    if (!trim(answer).empty())
        return Verdict::integerExpected;
    if (!correct)
        return Verdict::wrong;
    machine_->next();
    phase_ = Phase::running;
    return Verdict::correct;
}

Verdict Simulation::answerFinal(std::string_view answer, std::string_view errorAnswer) const
{
    if (phase_ == Phase::limit)
        return Verdict::limit;
    if (phase_ != Phase::finished)
        return Verdict::unavailable;
    const auto &frame = machine_->frame();
    if (goal_ == Goal::output)
        return compareOutput(frame.output, answer);
    if (goal_ == Goal::errorOutput)
        return compareOutput(frame.errorOutput, answer);
    if (goal_ == Goal::streams)
    {
        const auto output = compareOutput(frame.output, answer);
        const auto error = compareOutput(frame.errorOutput, errorAnswer);
        if (output == Verdict::unavailable || error == Verdict::unavailable)
            return Verdict::unavailable;
        return output == Verdict::correct && error == Verdict::correct ? Verdict::correct : Verdict::wrong;
    }
    const auto got = parseInteger(answer);
    if (!got)
        return Verdict::integerExpected;
    auto number = frame.returnCode;
    if (goal_ == Goal::value)
    {
        const auto value = machine_->inspect(expression_);
        number = value ? value->number : std::nullopt;
    }
    if (!number)
        return Verdict::unavailable;
    return number == got ? Verdict::correct : Verdict::wrong;
}

} // namespace training
