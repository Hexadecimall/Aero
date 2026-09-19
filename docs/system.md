# Running a Linux VM

`aero --system` boots a complete Linux kernel on macOS through Apple's
Virtualization framework. Guest CPU instructions execute with hardware
virtualization. This is separate from `--program` and its instruction interpreter.
Performance has not yet been benchmarked against other hypervisors.

## Boot

Supply a Linux kernel built for the host architecture: ARM64 on Apple Silicon,
x86-64 on Intel. ARM64 uses an uncompressed Linux `Image`, not an ELF `vmlinux`,
compressed kernel, ISO, or EFI executable. The kernel needs virtio console support;
virtio block and network drivers are needed for the corresponding devices.
Early-boot drivers must be built in or present in the initramfs.

```sh
aero --system --kernel ./Image --initrd ./initramfs.cpio.gz \
  --cpus 4 --memory 2048 --cmdline 'console=hvc0'
```

The guest serial console uses the current terminal. Keyboard input, including
Ctrl-C, goes to the guest. Run `poweroff` inside Linux to exit. From another
terminal, send SIGTERM to the Aero process to request shutdown. A second SIGTERM
forces it off and can lose unsaved guest data. Terminal settings are restored
when Aero exits normally or handles these signals.

`--memory` is in MiB; defaults are 1024 MiB and 2 CPUs. The framework validates
CPU and memory limits against the host. `--check` validates the configuration
without booting; it does not prove a kernel or root filesystem will boot.

## Storage and networking

```sh
aero --system --kernel ./Image --initrd ./initramfs.cpio.gz \
  --disk ./linux.raw --network \
  --cmdline 'console=hvc0 root=/dev/vda rw'
```

`--disk` attaches an existing raw disk as a virtio block device. Writes persist;
use `--read-only` to protect the image. Root partitions may instead be `/dev/vda1`
or another partition; use the arguments required by the distribution. QCOW2,
VMDK, ISO installation, and automatic disk creation are not supported yet.
Do not attach the same writable disk to multiple running VMs.

Networking is off by default. `--network` provides a virtio network adapter with
NAT; configure DHCP inside the guest. The MAC address is generated each launch.
Aero also provides virtio entropy and a memory balloon device. There is no host
folder sharing, graphics window, snapshot support, or automatic image download.

## Build and platform support

The macOS build links Apple's system Virtualization and Foundation frameworks.
CMake ad-hoc signs the CLI with `com.apple.security.virtualization`; a paid
signing identity is not needed for local builds. This is separate from Developer
ID signing and notarization for distribution. Build on macOS 13 or later with a
matching SDK. Hardware virtualization must be available; some sandboxes and
nested virtual machines prevent access.

Other hosts keep program mode available and report that their full-system
backend is not implemented. This backend virtualizes the host CPU architecture;
it does not emulate a different CPU architecture.

## Hardware boot test

The ordinary test suite needs no guest kernel or virtualization access. To test
real hardware boot on an ARM64 Mac, supply a local Linux Image with built-in
virtio console, virtio block, and devtmpfs support:

```sh
python3 tests/systemBoot.py ./build/aero /path/to/clang /path/to/ld.lld \
  /path/to/Image ./build/system-fixtures
```

The test creates a small original initramfs and disposable `test.raw` in the
output directory. It checks Linux PID 1 execution, guest poweroff, persistent
block writes, and read-only access over three separate boots. It does not download
or package a guest OS. Use a dedicated test output directory; its fixtures are
replaced on each run. Hardware tests are opt-in because hosted CI does not always
provide nested virtualization.
