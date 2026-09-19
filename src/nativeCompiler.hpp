#pragma once
#include <filesystem>
#include <string>

void compileNative(const std::string& source, const std::filesystem::path& directory,
                   const std::filesystem::path& output);
