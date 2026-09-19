"""Opt-in hardware test: real ARM64 Linux boot, init execution, and poweroff.

Usage: python3 tests/systemBoot.py AERO CLANG LLD KERNEL OUTPUT_DIRECTORY
Requires a local uncompressed ARM64 Linux Image with built-in virtio console.
No downloads; never use this test with an existing writable guest disk.
"""
import pathlib
import subprocess
import sys


def addEntry(archive, name, data=b"", mode=0o100755, major=0, minor=0):
    nameBytes = name.encode() + b"\0"
    fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(nameBytes), 0]
    archive.extend(("070701" + "".join(f"{value:08x}" for value in fields)).encode())
    archive.extend(nameBytes)
    archive.extend(b"\0" * (-len(archive) % 4))
    archive.extend(data)
    archive.extend(b"\0" * (-len(archive) % 4))


def main():
    aero, clang, lld, kernel, output = sys.argv[1:]
    outputDir = pathlib.Path(output).resolve()
    outputDir.mkdir(parents=True, exist_ok=True)
    initObject = outputDir / "systemInit.o"
    initBinary = outputDir / "systemInit"
    subprocess.run([clang, "--target=aarch64-linux-gnu", "-O2", "-ffreestanding",
                    "-fno-stack-protector", "-c", str(pathlib.Path(__file__).with_name("systemInit.c")),
                    "-o", str(initObject)], check=True)
    subprocess.run([lld, "-static", "-e", "_start", str(initObject), "-o", str(initBinary)], check=True)
    archive = bytearray()
    addEntry(archive, "init", initBinary.read_bytes())
    addEntry(archive, "dev", mode=0o40755)
    addEntry(archive, "dev/console", mode=0o20600, major=5, minor=1)
    addEntry(archive, "TRAILER!!!", mode=0)
    initramfs = outputDir / "systemInit.cpio"
    initramfs.write_bytes(archive)
    command = [aero, "--system", "--kernel", kernel, "--initrd", str(initramfs),
               "--cmdline", "console=hvc0 panic=-1", "--memory", "512"]
    diskImage = outputDir / "test.raw"
    with diskImage.open("wb") as diskFile:
        diskFile.truncate(4 * 1024 * 1024)
    for label, extra, marker in [
        ("boot", [], b"AERO_SYSTEM_BOOT_OK"),
        ("diskWrite", ["--disk", str(diskImage), "--network"], b"AERO_DISK_WRITE_OK"),
        ("diskReadOnly", ["--disk", str(diskImage), "--read-only"], b"AERO_DISK_READ_ONLY_OK"),
    ]:
        result = subprocess.run(command + extra, input=b"", stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=60)
        (outputDir / (label + ".log")).write_bytes(result.stdout + result.stderr)
        if result.returncode or marker not in result.stdout:
            raise RuntimeError(f"{label} failed ({result.returncode}): " +
                               (result.stdout + result.stderr).decode(errors="replace"))
        print(f"{label}: Linux boot, guest check, and poweroff passed")
    assert diskImage.read_bytes().startswith(b"AERO_DISK_OK")



if __name__ == "__main__":
    main()
