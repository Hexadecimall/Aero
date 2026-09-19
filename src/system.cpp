#include "system.hpp"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

int systemCommand(int argc, char** argv) {
    SystemOptions options;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Usage: aero --system --kernel Image [options]\n"
                "  --initrd PATH       Initial RAM disk\n"
                "  --disk PATH         Existing raw disk (guest writes persist)\n"
                "  --read-only         Make the disk read-only\n"
                "  --cpus N            Virtual CPUs (default 2)\n"
                "  --memory N          RAM in MiB (default 1024)\n"
                "  --cmdline TEXT      Kernel arguments (default console=hvc0)\n"
                "  --network           Enable NAT networking\n"
                "  --check             Validate configuration without booting\n"
                "Guest architecture must match the host. Serial console uses stdin/stdout.\n"
                "Send SIGTERM for graceful shutdown; send it again to force stop.\n";
            return 0;
        }
        if (arg == "--network") { options.network = true; continue; }
        if (arg == "--read-only") { options.readOnly = true; continue; }
        if (arg == "--check") { options.check = true; continue; }
        if (arg != "--kernel" && arg != "--initrd" && arg != "--disk" &&
            arg != "--cpus" && arg != "--memory" && arg != "--cmdline")
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
                (arg == "--cpus" && number > std::numeric_limits<unsigned>::max()) ||
                number > std::numeric_limits<std::uint64_t>::max() / (1024*1024))
                throw std::runtime_error(arg + " requires a positive integer in range");
            if (arg == "--cpus") options.cpuCount = static_cast<unsigned>(number);
            else options.memoryMiB = number;
        }
    }
    if (options.kernel.empty()) throw std::runtime_error("--system requires --kernel Image");
    if (options.readOnly && options.disk.empty()) throw std::runtime_error("--read-only requires --disk");
    for (const auto* path : {&options.kernel, &options.initrd, &options.disk}) {
        if (!path->empty() && !std::filesystem::is_regular_file(*path))
            throw std::runtime_error("not a regular file: " + *path);
    }
    return runSystem(options);
}
#ifndef __APPLE__
int runSystem(const SystemOptions&) {
    throw std::runtime_error("full-system virtualization currently requires macOS; this host backend is not implemented yet");
}
#endif
