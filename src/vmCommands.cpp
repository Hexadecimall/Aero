#include "system.hpp"
#include "systemControl.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
fs::path vmRoot() {
    if (const char* value = std::getenv("AERO_HOME"); value && *value) return fs::absolute(value) / "vms";
    const char* homePath = std::getenv("HOME");
#ifdef _WIN32
    if (!homePath) homePath = std::getenv("USERPROFILE");
#endif
    if (!homePath) throw std::runtime_error("set AERO_HOME or HOME to store VMs");
    return fs::path(homePath) / ".aero" / "vms";
}
fs::path vmDirectory(const std::string& name) {
    if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
        throw std::runtime_error("VM names must contain 1-64 letters, digits, hyphens, or underscores");
    return vmRoot() / name;
}
std::string trim(std::string text) {
    auto first = text.find_first_not_of(" \t\r");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r")-first+1);
}
std::string quotedValue(const std::string& value) {
    std::string result = "\"";
    for (unsigned char ch : value) {
        if (ch < 32 || ch == 127) throw std::runtime_error("configuration values cannot contain control characters");
        if (ch == '\\' || ch == '"') result += '\\';
        result += static_cast<char>(ch);
    }
    return result + '"';
}
std::string parseString(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"')
        throw std::runtime_error("configuration strings must use double quotes");
    std::string value;
    for (std::size_t i = 1; i+1 < text.size(); ++i) {
        char ch = text[i];
        if (ch == '\\') {
            if (++i+1 >= text.size() || (text[i] != '\\' && text[i] != '"'))
                throw std::runtime_error("unsupported configuration string escape");
            ch = text[i];
        } else if (ch == '"') throw std::runtime_error("unescaped quote in configuration");
        if (static_cast<unsigned char>(ch) < 32) throw std::runtime_error("control character in configuration");
        value += ch;
    }
    return value;
}
SystemOptions loadVm(const std::string& name) {
    SystemOptions options;
    options.name = name;
    options.display = SystemOptions::Display::window;
    options.profileDirectory = vmDirectory(name).string();
    std::ifstream input(fs::path(options.profileDirectory) / "configuration.toml");
    if (!input) throw std::runtime_error("VM not found: " + name + "; create it with aero create " + name);
    std::string line;
    std::vector<std::string> seen;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto equals = line.find('=');
        if (equals == std::string::npos) throw std::runtime_error("invalid VM configuration line");
        auto key = trim(line.substr(0, equals)), value = trim(line.substr(equals+1));
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) throw std::runtime_error("duplicate VM setting: " + key);
        seen.push_back(key);
        if (key == "kernel") options.kernel = parseString(value);
        else if (key == "initrd") options.initrd = parseString(value);
        else if (key == "disk") options.disk = parseString(value);
        else if (key == "commandLine") options.commandLine = parseString(value);
        else if (key == "macAddress") options.macAddress = parseString(value);
        else if (key == "network" || key == "readOnly") {
            if (value != "true" && value != "false") throw std::runtime_error("invalid boolean: " + key);
            (key == "network" ? options.network : options.readOnly) = value == "true";
        } else if (key == "virtualThreads" || key == "memoryMiB") {
            std::string flag = key == "virtualThreads" ? "--vthreads" : "--memory";
            char* args[] = {flag.data(), value.data()};
            parseSystemOptions(options, 2, args, 0);
        } else throw std::runtime_error("unknown VM setting: " + key);
    }
    return options;
}
void saveVm(SystemOptions options, bool create) {
    auto directory = vmDirectory(options.name);
    if (SystemControl::isRunning(directory.string())) throw std::runtime_error("stop the VM before changing its configuration");
    if (create && fs::exists(directory)) throw std::runtime_error("VM already exists: " + options.name);
    for (auto* path : {&options.kernel, &options.initrd, &options.disk})
        if (!path->empty()) *path = fs::absolute(*path).lexically_normal().string();
    validateSystemOptions(options);
    std::ostringstream content;
    content << "# Aero VM configuration\n";
    for (const auto& entry : {std::pair{"kernel", options.kernel}, {"initrd", options.initrd},
                             {"disk", options.disk}, {"commandLine", options.commandLine}, {"macAddress", options.macAddress}})
        content << entry.first << " = " << quotedValue(entry.second) << '\n';
    content << "virtualThreads = " << options.virtualThreads << "\nmemoryMiB = " << options.memoryMiB
            << "\nnetwork = " << (options.network ? "true" : "false")
            << "\nreadOnly = " << (options.readOnly ? "true" : "false") << '\n';
    if (create && !fs::create_directories(directory)) throw std::runtime_error("VM already exists");
    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace);
    auto temporary = directory / "configuration.tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        output << content.str();
        output.close();
        if (!output) throw std::runtime_error("cannot save VM configuration");
    }
#ifdef _WIN32
    // Windows does not replace an existing destination with filesystem::rename.
    if (!create) fs::remove(directory / "configuration.toml");
#endif
    fs::rename(temporary, directory / "configuration.toml");
}
std::string makeMac() {
    std::random_device random;
    std::ostringstream result;
    result << "02" << std::hex << std::setfill('0');
    for (int i = 0; i < 5; ++i) result << ':' << std::setw(2) << (random() & 255);
    return result.str();
}
std::vector<std::string> vmNames() {
    auto root = vmRoot();
    std::vector<std::string> names;
    if (fs::exists(root)) for (const auto& item : fs::directory_iterator(root))
        if (item.is_directory() && fs::exists(item.path()/"configuration.toml")) names.push_back(item.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}
void listVms() {
    auto names = vmNames();
    if (names.empty()) { std::cout << "No VMs yet. Create one with aero create linux.\n"; return; }
    for (const auto& name : names) {
        auto options = loadVm(name);
        std::cout << name << "\t" << (SystemControl::isRunning(options.profileDirectory) ? "Running" : "Stopped")
                  << "\t" << options.virtualThreads << " vthreads\t" << options.memoryMiB << " MiB\n";
    }
}
}

int vmCommand(int argc, char** argv) {
    if (argc == 1 && std::string(argv[0]).find(".app/Contents/MacOS/") != std::string::npos) {
        auto name = chooseVm(vmNames());
        if (name.empty()) return 0;
        auto options = loadVm(name);
        validateSystemOptions(options);
        return runSystem(options);
    }
    std::string command = argc < 2 ? "list" : argv[1];
    if (command == "list") {
        if (argc > 2) throw std::runtime_error("usage: aero list");
        listVms(); return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "--help") {
        std::cout << "Usage: aero " << command << " NAME [options]\n";
        printSystemHelp(); return 0;
    }
    if (argc < 3) throw std::runtime_error("usage: aero " + command + " NAME [options]");
    std::string name = argv[2];
    auto directory = vmDirectory(name);
    if (command == "stop") {
        bool force = argc == 4 && std::string(argv[3]) == "--force";
        if (argc != 3 && !force) throw std::runtime_error("usage: aero stop NAME [--force]");
        SystemControl::stop(directory.string(), force);
        std::cout << (force ? "Forced stop" : "Shutdown") << " requested for " << name << '\n';
        return 0;
    }
    SystemOptions options;
    if (command == "create") {
        options.name = name;
        options.macAddress = makeMac();
        options.display = SystemOptions::Display::window;
        options.commandLine = "console=tty0 console=hvc0 loglevel=4";
        if (argc == 3) {
            std::cout << "Kernel Image path: " << std::flush;
            if (!std::getline(std::cin, options.kernel)) throw std::runtime_error("setup cancelled");
            std::cout << "Initramfs path (blank for none): " << std::flush;
            if (!std::getline(std::cin, options.initrd)) throw std::runtime_error("setup cancelled");
            std::cout << "Raw disk path (blank for none): " << std::flush;
            if (!std::getline(std::cin, options.disk)) throw std::runtime_error("setup cancelled");
        }
    } else options = loadVm(name);
    if (command == "info") {
        if (argc != 3) throw std::runtime_error("usage: aero info NAME");
        std::cout << name << '\n' << "State: " << (SystemControl::isRunning(options.profileDirectory) ? "Running" : "Stopped")
                  << "\nVirtual threads: " << options.virtualThreads << "\nMemory: " << options.memoryMiB
                  << " MiB\nNetwork: " << (options.network ? "NAT" : "Off") << "\nMAC: " << options.macAddress
                  << "\nKernel: " << options.kernel << "\nInitramfs: " << options.initrd << "\nDisk: " << options.disk
                  << "\nDisk access: " << (options.readOnly ? "Read-only" : "Read-write")
                  << "\nKernel arguments: " << options.commandLine << "\nConfiguration: " << (directory/"configuration.toml").string() << '\n';
        return 0;
    }
    parseSystemOptions(options, argc, argv, 3);
    if (command == "create" || command == "configure") {
        saveVm(options, command == "create");
        std::cout << "Saved " << name << ". Start it with: aero start " << name << '\n';
        return 0;
    }
    validateSystemOptions(options);
    return runSystem(options);
}
