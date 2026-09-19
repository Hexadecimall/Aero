# Linux VMs

Aero boots complete Linux guests on macOS using hardware virtualization. Guest
CPU instructions execute on the host CPU. Program translation remains a separate
mode. Hardware virtualization requires matching host and guest architectures.

## Create and start

```sh
aero create linux --kernel ./Image --initrd ./initramfs.cpio \
  --vthreads 4 --memory 2048
aero start linux
```

`start` opens a native VM window with a virtio graphics device, USB keyboard and
pointing device. The guest needs the appropriate drivers and a graphical session
or framebuffer console. Aero does not install a desktop environment automatically.
An ISO of guest tools is not an operating-system installer.

Choose the interface explicitly when needed:

```sh
aero start linux --console
aero start linux --headless
aero start linux --window
```

Console mode connects the terminal to Linux. Ctrl-C goes to the guest; Ctrl-]
requests shutdown and a second Ctrl-] forces power off. Raw terminal modes and
file flags are restored when Aero exits normally or handles shutdown signals.
The prepared Alpine guest receives the terminal's initial row/column dimensions;
run `resize` inside Linux after changing the host terminal size. Other guests
configure their serial terminal through their own init/login setup.

Headless mode has no window and reads no terminal input. It runs in the foreground
so a service manager can supervise it; use ordinary shell backgrounding if desired.
Window and headless modes save serial output to the VM's `console.log`, replacing
that log on each start.

```sh
aero list
aero info linux
aero stop linux
aero stop linux --force
```

`stop` asks the guest to shut down. Linux must have a working power-button handler;
a minimal RAM guest may ignore that request. In that case use `poweroff` inside
Linux or explicitly force it off. Force-off can lose unsaved guest data. Closing
the window presents shutdown, cancel, and force-off choices.

A running profile is locked against duplicate starts and configuration edits.
The control socket is local to the profile with owner-only permissions; no
PID-based signals or network control port are used for named-VM commands.

## Settings

```sh
aero configure linux --vthreads 4 --memory 4096
aero configure linux --disk ./linux.raw
aero configure linux --read-only
aero configure linux --read-write
aero configure linux --no-network
```

`--vthreads 4` exposes four virtual CPU threads to Linux. The host schedules these
on available cores; this does not reserve four cores or refer to physical CPU
packages. `--cpus` and `--vcpus` are not accepted. RAM is specified in MiB. The
framework validates resource limits against the host.

Options on `start` override settings for that launch; `configure` saves them.
Profiles live in `~/.aero/vms/NAME/configuration.toml`. Set `AERO_HOME` to use a
different data directory. VM names contain letters, digits, hyphens and underscores.
Configuration uses a flat TOML subset: generated quoted strings (quote/backslash
escapes), positive decimal numbers and booleans. Prefer `configure` when editing.
Creating a profile references existing images; it does not copy or overwrite them.

`--disk` attaches a raw disk as a virtio block device, normally `/dev/vda`. Writes
persist unless `--read-only` is set. Supply the distribution's kernel arguments,
for example `--cmdline 'console=tty0 console=hvc0 root=/dev/vda2 rw'`. Do not attach
the same writable image to multiple VMs. QCOW2, VMDK, and EFI/ISO installation are
not supported in this milestone.

NAT networking is enabled by default. Each saved VM keeps a locally administered
MAC address across launches. `--no-network` disables the adapter. DHCP and DNS
configuration happen inside Linux; packet-socket and virtio-network support must
be built in or loaded. No bridged interface or root access is required. The local
Alpine guest has been verified obtaining a lease, resolving DNS, and fetching an
external HTTP page through NAT.

## Prepare a guest from local Alpine files

If a matching Alpine ARM64 kernel, minirootfs archive, and module initramfs are
already available, the helper assembles a small RAM guest without downloading
anything. Use an uncompressed kernel `Image`, not ELF `vmlinux` or an EFI binary.

```sh
python3 scripts/prepareAlpine.py \
  --kernel /path/to/Image \
  --rootfs /path/to/alpine-minirootfs-aarch64.tar.gz \
  --modules /path/to/matching-initramfs \
  --output "$HOME/.aero/images/linux"
aero create linux --kernel "$HOME/.aero/images/linux/Image" \
  --initrd "$HOME/.aero/images/linux/initramfs.cpio" --vthreads 4
aero start linux
```

The output directory must be new. The supplied kernel and modules must match.
The helper requires Python 3 and supports newc cpio module archives with optional
gzip compression. It copies the kernel and assembles a RAM filesystem containing
virtio drivers, DHCP startup, and graphical/serial Linux shells. It does not
install X11, Wayland, or a desktop environment. RAM filesystem changes disappear
at shutdown. The local console logs in as root for development; no SSH service or
host directory share is enabled. The input files retain their original licenses;
Aero's source and packages do not include those guest images.

Installed helpers reside under `share/aero/scripts` and `share/aero/guest`.
On macOS, `build/Aero.app` provides a saved-VM chooser; installation places that
bundle under `share/aero/Aero.app` alongside the helpers.

## One-shot boot

For tests or an unsaved VM:

```sh
aero --system --kernel ./Image --initrd ./initramfs.cpio --vthreads 4 --console
```

`--system` defaults to console mode. Unsaved VMs have no named control socket and
non-console serial output is discarded; use a saved profile for logs and `stop`.
`--check` validates a configuration without booting, but cannot prove that the
guest kernel, drivers or root filesystem will start correctly.

## Validation and limits

The regular suite requires no guest images or hardware virtualization. Opt-in
hardware checks use local files:

```sh
python3 tests/systemBoot.py ./build/aero /path/to/clang /path/to/ld.lld \
  /path/to/Image ./build/system-fixtures
python3 tests/systemLifecycle.py ./build/aero /path/to/Image \
  /path/to/prepared-initramfs.cpio ./build/lifecycle-fixtures --internet
```

The first test needs built-in virtio block, console and devtmpfs support and uses
a disposable disk. The second uses the prepared Alpine guest and exercises console
input, initial sizing, terminal restoration, named control, headless mode, and
optionally DHCP/DNS/HTTP. It does not download guest packages.

The macOS build links system frameworks and ad-hoc signs the CLI and app bundle
with the virtualization entitlement. A paid identity is not needed for local
builds; distribution signing/notarization is separate. Build on macOS 13 or newer
with a suitable SDK. Sandboxes and some nested VMs prevent virtualization access.
Other host backends, cross-architecture system emulation, 3D acceleration,
snapshots, and desktop installation remain future work. Native CPU execution
does not imply zero I/O overhead or a measured advantage over other hypervisors.
