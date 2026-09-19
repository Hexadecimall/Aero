#!/usr/bin/env bash
set -euo pipefail

sourceDir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
buildDir="$sourceDir/build"
installPrefix="/usr/local"
skipDeps=false
checkOnly=false
while (($#)); do
    case "$1" in
        --prefix|--build-dir)
            if (($# < 2)); then echo "$1 requires a directory" >&2; exit 2; fi
            if [[ "$1" == --prefix ]]; then installPrefix="$2"; else buildDir="$2"; fi
            shift 2 ;;
        --skip-deps) skipDeps=true; shift ;;
        --check) checkOnly=true; shift ;;
        --help)
            echo "Usage: scripts/install.sh [--prefix DIR] [--build-dir DIR] [--skip-deps] [--check]"
            echo "Install missing system dependencies, build Aero, and install it."
            echo "--check lists missing dependencies without changing anything."
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

platform="$(uname -s)"
missingPackages=()
cmakePrefixes="${CMAKE_PREFIX_PATH:-}"
if [[ "$platform" == Darwin ]]; then
    if ! command -v brew >/dev/null; then
        echo "Homebrew is required for automatic dependency installation on macOS." >&2
        echo "Alternatively install matching LLVM/Clang/LLD development packages and build with CMake." >&2
        exit 1
    fi
    for package in cmake llvm lld; do
        if ! brew list --versions "$package" >/dev/null 2>&1; then missingPackages+=("$package"); fi
    done
elif [[ "$platform" == Linux ]] && command -v apt-get >/dev/null && command -v dpkg-query >/dev/null; then
    for package in cmake build-essential clang llvm-dev libclang-dev libclang-cpp-dev liblld-dev lld; do
        if [[ "$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)" != "install ok installed" ]]; then
            missingPackages+=("$package")
        fi
    done
else
    echo "Automatic dependency installation currently supports macOS/Homebrew and Debian/Ubuntu." >&2
    echo "Other hosts can follow docs/building.md for a manual build." >&2
    exit 1
fi

if ((${#missingPackages[@]})); then
    printf 'Missing system packages:'
    printf ' %s' "${missingPackages[@]}"
    printf '\n'
else
    echo "System dependency packages are already installed."
fi
if "$checkOnly"; then exit 0; fi
if ((${#missingPackages[@]})) && ! "$skipDeps"; then
    if [[ "$platform" == Darwin ]]; then
        brew install "${missingPackages[@]}"
    else
        privilege=()
        if [[ "$(id -u)" != 0 ]]; then
            if ! command -v sudo >/dev/null; then echo "Installing system packages requires root or sudo." >&2; exit 1; fi
            privilege=(sudo)
        fi
        "${privilege[@]}" apt-get update
        "${privilege[@]}" apt-get install -y "${missingPackages[@]}"
    fi
fi
if [[ "$platform" == Darwin ]]; then
    cmakePrefixes="$(brew --prefix llvm);$(brew --prefix lld);$cmakePrefixes"
else
    cmakePrefixes="$(llvm-config --prefix);$cmakePrefixes"
fi
cmake -S "$sourceDir" -B "$buildDir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    "-DCMAKE_PREFIX_PATH=$cmakePrefixes"
cmake --build "$buildDir" --config Release --parallel
cmake --install "$buildDir" --config Release --prefix "$installPrefix"
"$installPrefix/bin/aero" --version
