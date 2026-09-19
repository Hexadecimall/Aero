#pragma once
#include <string>

class SystemControl {
    int lockFd = -1;
    int socketFd = -1;
    std::string socketPath;
    void closeControl();
public:
    explicit SystemControl(const std::string& directory);
    ~SystemControl();
    SystemControl(const SystemControl&) = delete;
    SystemControl& operator=(const SystemControl&) = delete;
    int receive();
    static bool isRunning(const std::string& directory);
    static void stop(const std::string& directory, bool force);
};
