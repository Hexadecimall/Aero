# Building and installing Aero

Aero currently provides a command-line prototype. There is no GUI installer or
stable release yet. Use and redistribution are governed by the [Aero License](../LICENSE).

## Requirements

For the CLI: CMake 3.20 or newer and a C++20 compiler. Unix builds also require
matching LLVM, Clang, and LLD development packages (version 18 or newer), including
Clang's Tooling/CodeGen libraries and LLD's native linker library. Aero's build
preprocesses the runtime headers once and embeds them. The build downloads nothing
automatically. Export needs the dynamically linked runtime libraries but does
not launch or require compiler/SDK tools.

For the full tests on Unix: Python 3, Clang with an AArch64 assembler, and LLD
(`ld.lld`). Native export uses library APIs inside Aero, with no subprocesses.

Typical tools to install if absent:

| Host | Tools |
| --- | --- |
| macOS | Xcode Command Line Tools, CMake, LLVM and LLD development libraries; Python for tests |
| Linux | C++ development tools, CMake, LLVM/Clang/LLD development libraries; Python for tests |
| Windows | Visual Studio C++ build tools and CMake; native export is not yet supported |

macOS is locally verified. CI checks Linux execution and Windows compilation.
The in-process export backend targets macOS and glibc Linux on AArch64/x86-64;
Windows export and other hosts remain future work. Native exports use OS-provided
runtime libraries and inherit their ABI compatibility requirements.

## Build

From an extracted source archive or repository checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The Unix executable is `build/aero`. With Visual Studio it is normally
`build/Release/aero.exe`.

If LLVM/Clang/LLD are not in CMake's search path, set `CMAKE_PREFIX_PATH` to their
installation prefixes, or set `LLVM_DIR`, `Clang_DIR`, and `LLD_DIR` to their
CMake package directories. All three must come from a matching toolchain.
For Homebrew installations, for example:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm);$(brew --prefix lld)"
```

## Install

The source installer checks for missing dependencies, installs them using the
system package manager, builds Aero, and installs it. It does not bundle private
copies of LLVM/Clang/LLD. Supported automatic installers are Homebrew on macOS
and APT on Debian/Ubuntu (LLVM 18 or newer is required).

```sh
bash scripts/install.sh --check
bash scripts/install.sh --prefix "$HOME/.local"
```

System dependency installation may require the package manager's normal
administrator permissions. `--skip-deps` disables dependency installation;
`--build-dir` selects a build directory. The installer does not silently install
a package manager or the Apple SDK. A custom Aero prefix changes only Aero's
location; dependencies stay in their standard system locations.

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

The exported executable runs on the target host platform and no longer needs
Aero, the input ELF, compiler tools, or an SDK. Its system C++ runtime is still
required. Export uses in-process compiler/linker APIs even with an empty PATH.
The test suite includes an empty-PATH/SDK check and, when the host permits it,
a macOS sandbox check denying child-process creation.

## Package

```sh
cpack --config build/CPackConfig.cmake -C Release -G TGZ -B build/packages
cpack --config build/CPackConfig.cmake -C Release -G ZIP -B build/packages
cpack --config build/CPackSourceConfig.cmake -G TGZ -B build/packages
```

Binary archives contain `bin/aero` (or `aero.exe`) and documentation. Aero's
dynamically linked LLVM/Clang/LLD libraries must be installed through the system
package manager in versions compatible with that build. These archives do not
bundle those dependencies. Prefer the source installer when an exact binary ABI
match is unavailable. Third-party libraries retain their own licenses.
Source
archives exclude build output and repository metadata. Both include the Aero
license; its conditions apply to use and redistribution. Packaging does not sign,
notarize, or publish a release. Validate the intended target platforms before
announcing a release.
