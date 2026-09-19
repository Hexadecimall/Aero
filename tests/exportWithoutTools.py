"""Export and run with no compiler, linker, SDK, or input ELF available at runtime."""
import os
import pathlib
import platform
import subprocess
import sys

aero, clang, linker, assembly, outputDir = sys.argv[1:]
aero = str(pathlib.Path(aero).resolve())
outputDir = pathlib.Path(outputDir).resolve()
outputDir.mkdir(parents=True, exist_ok=True)
obj = outputDir / "echo.o"
guest = outputDir / "guest"
native = outputDir / "native"
subprocess.run([clang, "--target=aarch64-linux-gnu", "-c", assembly, "-o", str(obj)], check=True)
subprocess.run([linker, "-m", "aarch64elf", "-static", "-e", "_start", str(obj), "-o", str(guest)], check=True)
emptyPath = outputDir / "empty"
emptyPath.mkdir(exist_ok=True)
environment = dict(os.environ, PATH=str(emptyPath), SDKROOT=str(emptyPath), DEVELOPER_DIR=str(emptyPath),
                   CPATH=str(emptyPath), CPLUS_INCLUDE_PATH=str(emptyPath), LIBRARY_PATH=str(emptyPath),
                   CXX=str(emptyPath / "missing-compiler"), CC=str(emptyPath / "missing-compiler"))
command = [aero, "--program", str(guest), "-o", str(native)]
subprocess.run(command, env=environment, check=True, timeout=60, cwd=emptyPath)
guest.unlink()
result = subprocess.run([str(native), "No external tools"], env=environment,
                        capture_output=True, check=True, timeout=10, cwd=emptyPath)
assert result.stdout == b"No external tools\n" and not result.stderr, result
assert not list(outputDir.glob(".aero-build-*")), "export left temporary files"
print("Export and standalone execution passed with tools and SDK paths unavailable")

if platform.system() == "Darwin":
    # The child sandbox can be unavailable inside an outer CI/agent sandbox.
    # Still require the PATH/SDK-isolated check above on every run.
    subprocess.run([linker, "-m", "aarch64elf", "-static", "-e", "_start", str(obj), "-o", str(guest)], check=True)
    result = subprocess.run(["/usr/bin/sandbox-exec", "-p", "(version 1)(allow default)(deny process-fork)",
                             *command], env=environment, capture_output=True, timeout=60)
    if result.returncode == 71 and b"sandbox_apply: Operation not permitted" in result.stderr:
        print("Child-process-denial check unavailable inside this outer sandbox")
    else:
        assert result.returncode == 0, result.stderr
        print("Export passed with child-process creation denied by macOS")
