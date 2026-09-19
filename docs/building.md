# Building and installing Aero

Aero currently provides a command-line prototype. There is no GUI installer or
stable release yet. Use and redistribution are governed by the [Aero License](../LICENSE).

## Requirements

For the CLI: CMake 3.20 or newer and a C++20 compiler. No Rust, Zig, Python, or
LLVM installation is required just to compile the CLI. The build downloads
nothing automatically.

For the full tests on Unix: Python 3, Clang with an AArch64 assembler, and LLD
(`ld.lld`). Native export also needs a C++20 compiler available as `c++` on PATH.

Typical tools to install if absent:

| Host | Tools |
| --- | --- |
| macOS | Xcode Command Line Tools; CMake; LLVM/LLD and Python for tests |
| Linux | C++ development tools; CMake; Clang, LLD and Python for tests |
| Windows | Visual Studio C++ build tools and CMake; native export is not yet supported |

macOS is locally verified. Linux execution tests and Windows compilation are
configured in CI but should not be described as passing until the workflow runs.
Other hosts are future targets.

## Build

From an extracted source archive or repository checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The Unix executable is `build/aero`. With Visual Studio it is normally
`build/Release/aero.exe`.

## Install

Use a writable prefix for a user installation on Unix:

```sh
cmake --install build --config Release --prefix "$HOME/.local"
"$HOME/.local/bin/aero" --version
```

Add `$HOME/.local/bin` to PATH to use `aero` directly. To stage a portable copy
without installing into the home directory, use `--prefix ./stage` instead.

On Windows, use a directory such as `--prefix "$env:LOCALAPPDATA/Aero"` in
PowerShell, then add its `bin` directory to PATH. No background service is installed.

## Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

If tools are not on PATH, pass their paths explicitly:

```sh
cmake -S . -B build -DBUILD_TESTING=ON \
  -DaeroClang=/path/to/clang -DaeroLld=/path/to/ld.lld \
  -DPython3_EXECUTABLE=/path/to/python3
```

Windows currently runs CLI smoke tests only. Unix additionally builds Linux
AArch64 fixtures and compares interpreted and native-exported results. No guest
distribution, emulator download, network access, or Linux root filesystem is
required for the suite.

## Try a program

After running the Unix test suite:

```sh
./build/aero --program ./build/fixtures/linux-program "Hello from Aero"
./build/aero --program ./build/compiledFixtures/compiledO1Pie hello world
./build/aero --program ./build/compiledFixtures/compiledO1Pie -o ./build/native-program
./build/native-program hello world
```

The exported executable runs on the host it was compiled for and no longer
needs Aero or the input ELF. Its system C++ runtime is still required.

## Package

```sh
cpack --config build/CPackConfig.cmake -C Release -G TGZ -B build/packages
cpack --config build/CPackConfig.cmake -C Release -G ZIP -B build/packages
cpack --config build/CPackSourceConfig.cmake -G TGZ -B build/packages
```

Binary archives contain `bin/aero` (or `aero.exe`) and documentation. Source
archives exclude build output and repository metadata. Both include the Aero
license; its conditions apply to use and redistribution. Packaging does not sign,
notarize, or publish a release. Validate the intended target platforms before
announcing a release.
