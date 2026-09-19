#pragma once
#include <cstdint>
#include <string>

struct SystemOptions {
    std::string kernel, initrd, disk;
    std::string commandLine = "console=hvc0";
    std::uint64_t memoryMiB = 1024;
    unsigned cpuCount = 2;
    bool network = false;
    bool readOnly = false;
    bool check = false;
};
int systemCommand(int argc, char** argv);
int runSystem(const SystemOptions& options);
