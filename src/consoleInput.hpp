#pragma once
#include <atomic>
#include <thread>

// Relay stdin without interpreting terminal escape sequences. Ctrl-] belongs to
// the host; all other bytes, including Ctrl-C and paste, belong to the guest.
class ConsoleInput {
    int guestFd = -1;
    int hostFd = -1;
    bool interactive;
    std::atomic<bool> finished{false};
    std::atomic<int> requests{0};
    std::thread worker;
    void relay();
public:
    explicit ConsoleInput(bool isInteractive);
    ~ConsoleInput();
    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;
    int descriptor() const { return guestFd; }
    int stopCount() const { return requests.load(); }
};
