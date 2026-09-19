#include "system.hpp"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

void printSystemHelp() {
    std::cout << "VM options:\n"
        "  --kernel PATH       Linux kernel Image\n"
        "  --initrd PATH       Initial RAM disk\n"
        "  --disk PATH         Existing raw disk (guest writes persist)\n"
        "  --read-only         Protect the disk from writes\n"
        "  --read-write        Enable persistent disk writes\n"
        "  --vthreads N        Virtual CPU threads (default 2)\n"
        "  --memory N          RAM in MiB (default 1024)\n"
        "  --cmdline TEXT      Kernel arguments\n"
        "  --network           NAT networking (default)\n"
        "  --no-network        Disable networking\n"
        "  --window            Graphical VM window (default for start)\n"
        "  --console           Interactive terminal; Ctrl-] requests shutdown\n"
        "  --headless          No window or terminal input; use aero stop NAME\n"
        "  --check             Validate without booting\n";
}
void parseSystemOptions(SystemOptions& options, int argc, char** argv, int first) {
    bool displaySet = false;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--window" || arg == "--console" || arg == "--headless") {
            if (displaySet) throw std::runtime_error("choose one of --window, --console, or --headless");
            displaySet = true;
            options.display = arg == "--window" ? SystemOptions::Display::window :
                arg == "--console" ? SystemOptions::Display::console : SystemOptions::Display::headless;
            continue;
        }
        if (arg == "--network") { options.network = true; continue; }
        if (arg == "--no-network") { options.network = false; continue; }
        if (arg == "--read-only") { options.readOnly = true; continue; }
        if (arg == "--read-write") { options.readOnly = false; continue; }
        if (arg == "--check") { options.check = true; continue; }
        if (arg != "--kernel" && arg != "--initrd" && arg != "--disk" &&
            arg != "--vthreads" && arg != "--memory" && arg != "--cmdline")
            throw std::runtime_error("unknown system option: " + arg);
        if (++i == argc) throw std::runtime_error(arg + " requires a value");
        std::string value = argv[i];
        if (value.empty()) throw std::runtime_error(arg + " requires a nonempty value");
        if (arg == "--kernel") options.kernel = value;
        else if (arg == "--initrd") options.initrd = value;
        else if (arg == "--disk") options.disk = value;
        else if (arg == "--cmdline") options.commandLine = value;
        else {
            std::uint64_t number = 0;
            auto result = std::from_chars(value.data(), value.data()+value.size(), number);
            if (result.ec != std::errc{} || result.ptr != value.data()+value.size() || !number ||
                (arg == "--vthreads" && number > std::numeric_limits<unsigned>::max()) ||
                number > std::numeric_limits<std::uint64_t>::max() / (1024*1024))
                throw std::runtime_error(arg + " requires a positive integer in range");
            if (arg == "--vthreads") options.virtualThreads = static_cast<unsigned>(number);
            else options.memoryMiB = number;
        }
    }
}
void validateSystemOptions(const SystemOptions& options) {
    if (options.kernel.empty()) throw std::runtime_error("VM requires --kernel Image");
    if (options.readOnly && options.disk.empty()) throw std::runtime_error("--read-only requires --disk");
    for (const auto* path : {&options.kernel, &options.initrd, &options.disk}) {
        if (!path->empty() && !std::filesystem::is_regular_file(*path))
            throw std::runtime_error("not a regular file: " + *path);
    }
}
int systemCommand(int argc, char** argv) {
    if (argc == 3 && std::string(argv[2]) == "--help") { printSystemHelp(); return 0; }
    SystemOptions options;
    parseSystemOptions(options, argc, argv, 2);
    validateSystemOptions(options);
    return runSystem(options);
}
#ifndef __APPLE__
std::string chooseVm(const std::vector<std::string>&) {
    throw std::runtime_error("the VM window currently requires macOS");
}
int runSystem(const SystemOptions&) {
    throw std::runtime_error("full-system virtualization currently requires macOS; this host backend is not implemented yet");
}
#endif
