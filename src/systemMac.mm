#include "system.hpp"
#include "systemControl.hpp"
#include "consoleInput.hpp"
#import <Virtualization/Virtualization.h>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

@interface AeroVmDelegate : NSObject <VZVirtualMachineDelegate, NSWindowDelegate, NSApplicationDelegate>
@property bool finished;
@property int exitCode;
@property int stopLevel;
@end
@implementation AeroVmDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    if (self.finished) return YES;
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"Shut down this VM?";
    alert.informativeText = @"Request a clean shutdown, or force power off if the guest is unresponsive. Forcing it off can lose unsaved guest data.";
    [alert addButtonWithTitle:@"Shut Down"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Force Off"];
    auto response = [alert runModal];
    if (response == NSAlertFirstButtonReturn) self.stopLevel = 1;
    if (response == NSAlertThirdButtonReturn) self.stopLevel = 2;
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if (self.finished) return NSTerminateNow;
    [self windowShouldClose:sender.keyWindow];
    return NSTerminateCancel;
}
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

std::string chooseVm(const std::vector<std::string>& names) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"Aero";
        if (names.empty()) {
            alert.informativeText = @"No virtual machines yet. Create one in Terminal with aero create linux, then open Aero again.";
            [alert runModal];
            return "";
        }
        alert.informativeText = @"Choose a virtual machine to start.";
        auto picker = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 300, 28) pullsDown:NO];
        for (const auto& name : names) [picker addItemWithTitle:[NSString stringWithUTF8String:name.c_str()]];
        alert.accessoryView = picker;
        [alert addButtonWithTitle:@"Start"];
        [alert addButtonWithTitle:@"Cancel"];
        if ([alert runModal] != NSAlertFirstButtonReturn) return "";
        return names.at(static_cast<std::size_t>(picker.indexOfSelectedItem));
    }
}

int runSystem(const SystemOptions& options) {
    bool interactive = options.display == SystemOptions::Display::console;
    bool graphical = options.display == SystemOptions::Display::window;
    ConsoleGuard console(interactive && !options.check);
    SystemControl control(options.check ? "" : options.profileDirectory);
    @autoreleasepool {
        if (![VZVirtualMachine isSupported])
            throw std::runtime_error("hardware virtualization is unavailable on this Mac");
        auto config = [VZVirtualMachineConfiguration new];
        config.CPUCount = options.virtualThreads;
        config.memorySize = options.memoryMiB * 1024 * 1024;
        config.platform = [[VZGenericPlatformConfiguration alloc] init];
        auto boot = [[VZLinuxBootLoader alloc] initWithKernelURL:fileUrl(options.kernel)];
        if (!options.initrd.empty()) boot.initialRamdiskURL = fileUrl(options.initrd);
        auto kernelArguments = options.commandLine;
        if (interactive) {
            winsize size{};
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row && size.ws_col)
                kernelArguments += " aero.rows=" + std::to_string(size.ws_row) + " aero.columns=" + std::to_string(size.ws_col);
        }
        boot.commandLine = [NSString stringWithUTF8String:kernelArguments.c_str()];
        config.bootLoader = boot;
        if (graphical) {
            auto graphics = [[VZVirtioGraphicsDeviceConfiguration alloc] init];
            graphics.scanouts = @[[[VZVirtioGraphicsScanoutConfiguration alloc] initWithWidthInPixels:1280 heightInPixels:800]];
            config.graphicsDevices = @[graphics];
            config.keyboards = @[[[VZUSBKeyboardConfiguration alloc] init]];
            config.pointingDevices = @[[[VZUSBScreenCoordinatePointingDeviceConfiguration alloc] init]];
        }
        std::unique_ptr<ConsoleInput> input;
        NSFileHandle* inputHandle = nil;
        NSFileHandle* outputHandle = nil;
        if (interactive && !options.check) {
            input = std::make_unique<ConsoleInput>(isatty(STDIN_FILENO));
            inputHandle = [[NSFileHandle alloc] initWithFileDescriptor:input->descriptor() closeOnDealloc:NO];
            outputHandle = [NSFileHandle fileHandleWithStandardOutput];
        } else if (!options.check) {
            std::string logPath = options.profileDirectory.empty() ? "/dev/null" : options.profileDirectory + "/console.log";
            int fd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if (fd < 0) throw std::runtime_error("cannot open guest console log: " + logPath);
            outputHandle = [[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
        }
        auto serial = [[VZVirtioConsoleDeviceSerialPortConfiguration alloc] init];
        serial.attachment = [[VZFileHandleSerialPortAttachment alloc]
            initWithFileHandleForReading:inputHandle fileHandleForWriting:outputHandle];
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
            network.MACAddress = options.macAddress.empty() ? [VZMACAddress randomLocallyAdministeredAddress] :
                [[VZMACAddress alloc] initWithString:[NSString stringWithUTF8String:options.macAddress.c_str()]];
            if (!network.MACAddress) throw std::runtime_error("invalid VM MAC address");
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
        NSWindow* window = nil;
        if (graphical) {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            NSApp.delegate = delegate;
            auto menu = [NSMenu new];
            auto appItem = [NSMenuItem new];
            [menu addItem:appItem];
            auto appMenu = [NSMenu new];
            [appMenu addItemWithTitle:@"Quit Aero" action:@selector(terminate:) keyEquivalent:@"q"];
            appItem.submenu = appMenu;
            NSApp.mainMenu = menu;
            window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1100, 700)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered defer:NO];
            window.releasedWhenClosed = NO;
            window.title = options.name.empty() ? @"Aero" :
                [@"Aero — " stringByAppendingString:[NSString stringWithUTF8String:options.name.c_str()]];
            window.delegate = delegate;
            window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
            auto view = [[VZVirtualMachineView alloc] initWithFrame:window.contentView.bounds];
            view.virtualMachine = machine;
            view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            view.capturesSystemKeys = NO;
            window.contentView = view;
            [window center];
            [NSApp finishLaunching];
            [window makeKeyAndOrderFront:nil];
            [window makeFirstResponder:view];
            [NSApp activateIgnoringOtherApps:YES];
        }
        std::cerr << "aero: starting Linux guest, " << options.virtualThreads << " virtual threads, "
                  << options.memoryMiB << " MiB RAM" << (interactive ? "\r\n" : "\n");
        if (interactive && isatty(STDIN_FILENO))
            std::cerr << "aero: Ctrl-] requests shutdown; press twice to force off. Ctrl-C goes to Linux.\r\n";
        if (!interactive && !options.profileDirectory.empty())
            std::cerr << "aero: console log: " << options.profileDirectory << "/console.log\n";
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
                if (graphical) {
                    auto event = [NSApp nextEventMatchingMask:NSEventMaskAny
                        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05] inMode:NSDefaultRunLoopMode dequeue:YES];
                    if (event) [NSApp sendEvent:event];
                    [NSApp updateWindows];
                } else [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                int stopLevel = std::max(static_cast<int>(stopRequests), delegate.stopLevel);
                if (input) stopLevel = std::max(stopLevel, input->stopCount());
                int command = control.receive();
                if (command) { delegate.stopLevel = std::max(delegate.stopLevel, command); stopLevel = std::max(stopLevel, command); }
                if (stopLevel > handledRequests && machine.canRequestStop && !handledRequests) {
                    handledRequests = 1;
                    NSError* stopError = nil;
                    if (![machine requestStopWithError:&stopError])
                        std::cerr << "aero: shutdown request failed: " << stopError.localizedDescription.UTF8String << '\n';
                    else std::cerr << "aero: shutdown requested; Ctrl-] again or aero stop NAME --force to force off\r\n";
                }
                if (stopLevel >= 2 && handledRequests < 2 && machine.canStop) {
                    handledRequests = 2;
                    [machine stopWithCompletionHandler:^(NSError* stopError) {
                        if (stopError) std::cerr << "aero: force stop failed: " << stopError.localizedDescription.UTF8String << '\n';
                        delegate.exitCode = stopError ? 1 : 130;
                        delegate.finished = true;
                    }];
                }
            }
        }
        if (window) { window.delegate = nil; [window close]; NSApp.delegate = nil; }
        return delegate.exitCode;
    }
}
