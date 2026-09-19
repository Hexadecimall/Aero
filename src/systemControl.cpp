#include "systemControl.hpp"
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
sockaddr_un controlAddress(const std::string& directory) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    auto path = directory + "/control.sock";
    if (path.size() >= sizeof(address.sun_path)) throw std::runtime_error("VM control socket path is too long; use a shorter AERO_HOME");
    std::memcpy(address.sun_path, path.c_str(), path.size()+1);
    return address;
}
}
SystemControl::SystemControl(const std::string& directory) {
    if (directory.empty()) return;
    auto address = controlAddress(directory);
    try {
        lockFd = open((directory + "/run.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lockFd < 0) throw std::runtime_error("cannot open VM lock");
        if (flock(lockFd, LOCK_EX | LOCK_NB) < 0) throw std::runtime_error("VM is already running or locked");
        socketFd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (socketFd < 0) throw std::runtime_error("cannot create VM control socket");
        socketPath = address.sun_path;
        unlink(socketPath.c_str());
        if (bind(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::runtime_error("cannot bind VM control socket");
        if (chmod(socketPath.c_str(), 0600) < 0 || fcntl(socketFd, F_SETFL, O_NONBLOCK) < 0)
            throw std::runtime_error("cannot configure VM control socket");
        fcntl(socketFd, F_SETFD, FD_CLOEXEC);
    } catch (...) { closeControl(); throw; }
}
void SystemControl::closeControl() {
    if (socketFd >= 0) close(socketFd);
    if (!socketPath.empty()) unlink(socketPath.c_str());
    if (lockFd >= 0) close(lockFd);
}
SystemControl::~SystemControl() { closeControl(); }
int SystemControl::receive() {
    if (socketFd < 0) return 0;
    char message[16];
    auto count = recv(socketFd, message, sizeof(message), 0);
    if (count == 4 && std::memcmp(message, "stop", 4) == 0) return 1;
    if (count == 5 && std::memcmp(message, "force", 5) == 0) return 2;
    return 0;
}
bool SystemControl::isRunning(const std::string& directory) {
    int fd = open((directory + "/run.lock").c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool running = flock(fd, LOCK_EX | LOCK_NB) < 0;
    close(fd);
    return running;
}
void SystemControl::stop(const std::string& directory, bool force) {
    if (!isRunning(directory)) throw std::runtime_error("VM is not running");
    auto address = controlAddress(directory);
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) throw std::runtime_error("cannot open VM control socket");
    fcntl(fd, F_SETFL, O_NONBLOCK);
    const char* message = force ? "force" : "stop";
    auto sent = sendto(fd, message, std::strlen(message), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    close(fd);
    if (sent < 0) throw std::runtime_error("VM control is unavailable; it may still be starting");
}
#else
SystemControl::SystemControl(const std::string&) {}
SystemControl::~SystemControl() = default;
void SystemControl::closeControl() {}
int SystemControl::receive() { return 0; }
bool SystemControl::isRunning(const std::string&) { return false; }
void SystemControl::stop(const std::string&, bool) { throw std::runtime_error("VM control is not implemented on this host"); }
#endif
