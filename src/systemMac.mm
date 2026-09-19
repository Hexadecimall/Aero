#include "system.hpp"
#import <Virtualization/Virtualization.h>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

@interface AeroVmDelegate : NSObject <VZVirtualMachineDelegate>
@property bool finished;
@property int exitCode;
@end
@implementation AeroVmDelegate
- (void)guestDidStopVirtualMachine:(VZVirtualMachine *)machine {
    (void)machine;
    self.finished = true;
}
- (void)virtualMachine:(VZVirtualMachine *)machine didStopWithError:(NSError *)error {
    (void)machine;
    std::cerr << "aero: guest stopped: " << error.localizedDescription.UTF8String << '\n';
    self.exitCode = 1;
    self.finished = true;
}
@end

namespace {
volatile sig_atomic_t stopRequests = 0;
void requestStop(int) { if (stopRequests < 2) stopRequests = stopRequests + 1; }
struct ConsoleGuard {
    termios saved{};
    bool changed = false;
    int inputFlags = fcntl(STDIN_FILENO, F_GETFL);
    int outputFlags = fcntl(STDOUT_FILENO, F_GETFL);
    struct sigaction oldTerm{}, oldInt{};
    explicit ConsoleGuard(bool rawMode) {
        stopRequests = 0;
        struct sigaction action{};
        action.sa_handler = requestStop;
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, &oldTerm);
        sigaction(SIGINT, &action, &oldInt);
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
            changed = true;
            auto raw = saved;
            cfmakeraw(&raw);
            if (rawMode) tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
    }
    ~ConsoleGuard() {
        if (changed) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        if (inputFlags >= 0) fcntl(STDIN_FILENO, F_SETFL, inputFlags);
        if (outputFlags >= 0) fcntl(STDOUT_FILENO, F_SETFL, outputFlags);
        sigaction(SIGTERM, &oldTerm, nullptr);
        sigaction(SIGINT, &oldInt, nullptr);
    }
};
NSURL* fileUrl(const std::string& path) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}
void failWithError(NSError* error) {
    throw std::runtime_error(error ? error.localizedDescription.UTF8String : "virtualization operation failed");
}
}

int runSystem(const SystemOptions& options) {
    ConsoleGuard console(!options.check);
    @autoreleasepool {
        if (![VZVirtualMachine isSupported])
            throw std::runtime_error("hardware virtualization is unavailable on this Mac");
        auto config = [VZVirtualMachineConfiguration new];
        config.CPUCount = options.cpuCount;
        config.memorySize = options.memoryMiB * 1024 * 1024;
        config.platform = [[VZGenericPlatformConfiguration alloc] init];
        auto boot = [[VZLinuxBootLoader alloc] initWithKernelURL:fileUrl(options.kernel)];
        if (!options.initrd.empty()) boot.initialRamdiskURL = fileUrl(options.initrd);
        boot.commandLine = [NSString stringWithUTF8String:options.commandLine.c_str()];
        config.bootLoader = boot;
        auto serial = [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
        serial.attachment = [[VZFileHandleSerialPortAttachment alloc]
            initWithFileHandleForReading:[NSFileHandle fileHandleWithStandardInput]
            fileHandleForWriting:[NSFileHandle fileHandleWithStandardOutput]];
        config.serialPorts = @[serial];
        config.entropyDevices = @[[[VZVirtioEntropyDeviceConfiguration alloc] init]];
        config.memoryBalloonDevices = @[[[VZVirtioTraditionalMemoryBalloonDeviceConfiguration alloc] init]];
        if (!options.disk.empty()) {
            NSError* error = nil;
            auto attachment = [[VZDiskImageStorageDeviceAttachment alloc]
                initWithURL:fileUrl(options.disk) readOnly:options.readOnly error:&error];
            if (!attachment) failWithError(error);
            config.storageDevices = @[[[VZVirtioBlockDeviceConfiguration alloc] initWithAttachment:attachment]];
        }
        if (options.network) {
            auto network = [[VZVirtioNetworkDeviceConfiguration alloc] init];
            network.attachment = [[VZNATNetworkDeviceAttachment alloc] init];
            network.MACAddress = [VZMACAddress randomLocallyAdministeredAddress];
            config.networkDevices = @[network];
        }
        NSError* error = nil;
        if (![config validateWithError:&error]) failWithError(error);
        if (options.check) {
            std::cerr << "aero: system configuration valid (macOS hardware virtualization)\n";
            return 0;
        }
        auto delegate = [AeroVmDelegate new];
        auto machine = [[VZVirtualMachine alloc] initWithConfiguration:config];
        machine.delegate = delegate;
        std::cerr << "aero: starting Linux guest, " << options.cpuCount << " CPUs, "
                  << options.memoryMiB << " MiB RAM\n";
        [machine startWithCompletionHandler:^(NSError* startError) {
            if (startError) {
                std::cerr << "aero: cannot start guest: " << startError.localizedDescription.UTF8String << '\n';
                delegate.exitCode = 1;
                delegate.finished = true;
            }
        }];
        int handledRequests = 0;
        while (!delegate.finished) {
            @autoreleasepool {
                [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                if (stopRequests > handledRequests && machine.canRequestStop && !handledRequests) {
                    handledRequests = 1;
                    NSError* stopError = nil;
                    if (![machine requestStopWithError:&stopError])
                        std::cerr << "aero: shutdown request failed: " << stopError.localizedDescription.UTF8String << '\n';
                    else std::cerr << "aero: shutdown requested; send another signal to force stop\n";
                }
                if (stopRequests >= 2 && handledRequests < 2 && machine.canStop) {
                    handledRequests = 2;
                    [machine stopWithCompletionHandler:^(NSError* stopError) {
                        delegate.exitCode = stopError ? 1 : 130;
                        delegate.finished = true;
                    }];
                }
            }
        }
        return delegate.exitCode;
    }
}
