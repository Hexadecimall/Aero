# Aero

Aero is a program execution and full-system emulation project. The command-line
tool is `aero`; the planned graphical application is Aero.

## Current milestone

The C++ prototype runs static Linux AArch64 executables and self-contained PIE
executables on macOS. It supports compiler-generated integer code, recursive
calls, stack and memory operations, arithmetic flags, and relative ELF relocations.
Linux `write`, `exit`, and `exit_group` are implemented. Native export is available.

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

Building requires a C++20 compiler and CMake. Tests additionally use Python 3,
Clang's AArch64 assembler, and `ld.lld`. No dependencies are downloaded by the
build. Tests are opt-in; omit `-DBUILD_TESTING=ON` to build just the executable.

The tests assemble and link a real Linux executable from `tests/echo.S`; the
runtime executes its machine instructions, without invoking a host echo command.

`-o` translates reachable instructions into C++ operations and direct branches,
then invokes the local `c++` compiler to create an optimized host-native binary.
The exported program contains its guest image and Linux compatibility support;
it does not require Aero or the input ELF to run. Guest instruction fetch/decode
is removed; indirect returns dispatch to translated addresses. This initial
export targets the current Unix host, requires a C++20
compiler, and retains guest-memory checks. Performance has not been benchmarked.
Arguments belong to the exported program at run time, not the export command.
Use `--` before guest arguments when they include a literal `-o`.

## Limits

This is an initial interpreter, not a native-speed backend. Only the instruction
subset needed by the fixtures is implemented. Dynamic Linux executables,
including a distribution's `/usr/bin/echo`, are not supported yet. The environment
and auxiliary vector are minimal. Guest memory is capped at 64 MiB for loaded
segments and 1 MiB for the stack; execution stops after 100 million instructions.
These are prototype safeguards, not final product capacity limits.

The GUI, configuration system, full-system mode, hardware acceleration, JIT,
additional architectures, and graphics support are not implemented. The source
has not yet been validated on other hosts. This prototype is not a hardened
isolation boundary for untrusted programs.

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
