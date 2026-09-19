#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SystemOptions {
    enum class Display { window, console, headless };
    std::string kernel, initrd, disk;
    std::string name, profileDirectory, macAddress;
    std::string commandLine = "console=hvc0";
    std::uint64_t memoryMiB = 1024;
    unsigned virtualThreads = 2;
    bool network = true;
    bool readOnly = false;
    bool check = false;
    Display display = Display::console;
};
void parseSystemOptions(SystemOptions& options, int argc, char** argv, int first);
void validateSystemOptions(const SystemOptions& options);
void printSystemHelp();
int vmCommand(int argc, char** argv);
std::string chooseVm(const std::vector<std::string>& names);
int systemCommand(int argc, char** argv);
int runSystem(const SystemOptions& options);
