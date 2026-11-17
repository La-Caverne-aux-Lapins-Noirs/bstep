#pragma once

#include <filesystem>
#include <optional>

std::optional<int> launchGui(const std::filesystem::path &executable, char **arguments);
