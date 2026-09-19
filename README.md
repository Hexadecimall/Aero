# Aero

Aero is a program execution and full-system emulation project. The command-line
tool is `aero`; macOS builds also provide an Aero app bundle.

## Current milestone

The C++ prototype runs static Linux AArch64 executables and self-contained PIE
executables on macOS. It supports compiler-generated integer code, recursive
calls, stack and memory operations, arithmetic flags, and relative ELF relocations.
Linux `write`, `exit`, and `exit_group` are implemented. Native export is available.

Full-system mode boots same-architecture Linux guests on macOS using hardware
virtualization, with saved VMs, a graphical window, console/headless modes,
raw disks and NAT networking.
See [Running a Linux VM](docs/system.md) for kernel requirements and commands.

See [Building and installing Aero](docs/building.md) for installation, test tools,
Windows build instructions, and portable archives.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/aero --program ./build/fixtures/linux-program "Hello from Aero"
./build/aero --program ./build/fixtures/linux-program -o ./build/native-echo
./build/native-echo "Hello from native Aero"
```

Building requires a C++20 compiler and CMake. Unix builds also require matching
LLVM, Clang, and LLD development libraries and the Clang compiler to prepare the
embedded runtime at build time. Tests additionally use Python 3 and `ld.lld`.
No dependencies are downloaded by the build. Tests are opt-in.

The tests assemble and link a real Linux executable from `tests/echo.S`; the
runtime executes its machine instructions, without invoking a host echo command.

`-o` translates reachable instructions into C++ operations and direct branches,
then uses embedded Clang and LLD libraries to compile and link in-process.
Export launches no compiler, linker, shell, or signing subprocess. Runtime header
content is prepared when Aero is built, so export needs no installed compiler,
development headers, or SDK at export time. Aero dynamically links to system
backend libraries; the installer installs missing dependencies in their normal
package-manager locations rather than bundling private copies.
The exported program contains its guest image and Linux compatibility support;
it does not require Aero or the input ELF to run. Guest instruction fetch/decode
is removed; indirect returns dispatch to translated addresses. This initial
export targets macOS or glibc Linux on AArch64/x86-64 and retains guest-memory
checks. Exported programs use the target OS's standard runtime libraries.
Performance has not been benchmarked.
Arguments belong to the exported program at run time, not the export command.
Use `--` before guest arguments when they include a literal `-o`.

## Start a VM

```sh
aero create linux --kernel ./Image --initrd ./initramfs.cpio --vthreads 4
aero start linux                 # Graphical window
aero start linux --console       # Interactive terminal
aero start linux --headless      # No window or terminal input
aero stop linux
```

NAT is on by default. Supply a same-architecture Linux guest with virtio drivers;
see [VM setup](docs/system.md) for local Alpine preparation, persistent disks,
configuration, and supported guest features. `--vthreads` replaces `--cpus`.

## Limits

This is an initial interpreter, not a native-speed backend. Only the instruction
subset needed by the fixtures is implemented. Dynamic Linux executables,
including a distribution's `/usr/bin/echo`, are not supported yet. The environment
and auxiliary vector are minimal. Guest memory is capped at 64 MiB for loaded
segments and 1 MiB for the stack; execution stops after 100 million instructions.
These are prototype safeguards, not final product capacity limits.

JIT, cross-architecture full-system emulation, non-macOS virtualization backends,
3D acceleration, and automatic desktop installation are not implemented.
CI checks macOS, Linux, and Windows builds; hardware VM boot is tested locally
on Apple Silicon. Program mode is not a hardened isolation boundary for
untrusted programs.

Native export supports the same small instruction subset and emits explicit
failure paths for unsupported instructions. Indirect targets must already be
discovered by translation; arbitrary function pointers and general Linux
applications are not supported yet. The interpreter's instruction limit
does not apply to native exports.

## License

Aero uses the custom [Aero License](LICENSE): personal noncommercial use is free;
independent developers qualify for free use for products earning less than
USD 1 million in revenue over the preceding twelve months. Other commercial use,
including business use of the application itself, requires a paid license.
Products incorporating Aero must make their use of Aero easy to spot; the
acknowledgment's placement and format are flexible.

This is a source-available license with commercial-use conditions, rather than
an [OSI open-source license](https://opensource.org/osd). The license text governs.
