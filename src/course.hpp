#pragma once

#include "cmini.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace training
{

enum class Goal { output, errorOutput, streams, returned, value };

struct Exercise
{
    std::string title;
    std::string filename;
    std::string source;
    Goal goal;
    std::string expression;
    std::string input;
    std::filesystem::path progress;
    bool passed = false;
};

struct Course
{
    std::string title;
    std::vector<Exercise> exercises;
};

struct CatalogEntry
{
    std::filesystem::path manifest;
    std::string title;
};

std::vector<CatalogEntry> discover(const std::filesystem::path &root);
Course load(const std::filesystem::path &manifest);
void recordSuccess(Exercise &exercise);

enum class Verdict { correct, wrong, integerExpected, unavailable, limit };
enum class Phase { ready, running, question, finished, unavailable, limit };

class Simulation
{
public:
    explicit Simulation(const Exercise &exercise);
    Phase phase() const { return phase_; }
    void start();
    bool tick();
    const cmini::Question *question() const;
    Verdict answerQuestion(std::string_view answer);
    Verdict answerFinal(std::string_view answer, std::string_view errorAnswer = {}) const;

private:
    Goal goal_;
    cmini::Expr expression_;
    std::optional<cmini::Machine> machine_;
    std::vector<int> values_;
    Phase phase_ = Phase::ready;
};

} // namespace training
