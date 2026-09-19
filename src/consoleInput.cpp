#include "consoleInput.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

ConsoleInput::ConsoleInput(bool isInteractive) : interactive(isInteractive) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) throw std::runtime_error("cannot create console relay");
    guestFd = sockets[0]; hostFd = sockets[1];
    int enabled = 1;
    setsockopt(hostFd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
    fcntl(hostFd, F_SETFL, O_NONBLOCK);
    fcntl(hostFd, F_SETFD, FD_CLOEXEC);
    fcntl(guestFd, F_SETFD, FD_CLOEXEC);
    try { worker = std::thread([this] { relay(); }); }
    catch (...) { close(guestFd); close(hostFd); throw; }
}
ConsoleInput::~ConsoleInput() {
    finished = true;
    worker.join();
    close(hostFd); close(guestFd);
}
void ConsoleInput::relay() {
    std::string pending;
    bool eof = false;
    while (!finished) {
        pollfd fds[2] = {{eof ? -1 : STDIN_FILENO, static_cast<short>(pending.size() < 65536 ? POLLIN : 0), 0},
                        {hostFd, static_cast<short>(pending.empty() ? 0 : POLLOUT), 0}};
        if (poll(fds, 2, 50) < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents & (POLLERR | POLLNVAL)) eof = true;
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char data[4096];
            auto count = read(STDIN_FILENO, data, sizeof(data));
            if (count > 0) {
                for (ssize_t i = 0; i < count; ++i) {
                    if (interactive && data[i] == 0x1d) requests.store(std::min(2, requests.load()+1));
                    else pending += data[i];
                }
            } else if (!count || (errno != EINTR && errno != EAGAIN)) eof = true;
        }
        if (!pending.empty()) {
            auto count = send(hostFd, pending.data(), pending.size(), 0);
            if (count > 0) pending.erase(0, static_cast<std::size_t>(count));
            else if (count < 0 && errno != EAGAIN && errno != EINTR) break;
        }
        if (eof && pending.empty()) { shutdown(hostFd, SHUT_WR); break; }
    }
}
