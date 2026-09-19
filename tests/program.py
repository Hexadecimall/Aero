"""Execute genuine Linux ELF fixtures and verify loader/execution boundaries."""
import pathlib
import struct
import subprocess
import sys

aero, clang, linker, assembly, output = sys.argv[1:]
output = pathlib.Path(output)
output.mkdir(parents=True, exist_ok=True)
obj = output / "echo.o"
elf = output / "linux-program"
subprocess.run([clang, "--target=aarch64-linux-gnu", "-c", assembly, "-o", str(obj)], check=True)
subprocess.run([linker, "-m", "aarch64elf", "-static", "-e", "_start", str(obj), "-o", str(elf)], check=True)
checks = 0


def run(path, args=(), expected=b"", status=0, diagnostic=None):
    global checks
    result = subprocess.run([aero, "--program", str(path), *args], capture_output=True, timeout=10)
    assert result.returncode == status, (result.returncode, result.stderr)
    assert result.stdout == expected, (result.stdout, expected)
    if diagnostic:
        assert diagnostic in result.stderr, result.stderr
    checks += 1


run(elf, ["Hello from Aero"], b"Hello from Aero\n")
run(elf, [], b"\n")
run(elf, ["one", "two", "three"], b"one two three\n")
run(elf, ["", "two", ""], b" two \n")
run(elf, ["a" * 16000], b"a" * 16000 + b"\n")
run(elf, ["hello \u4e16\u754c"], "hello \u4e16\u754c\n".encode())
run(output / "missing", status=1, diagnostic=b"cannot open program")

original = elf.read_bytes()
entry = struct.unpack_from("<Q", original, 24)[0]
phoff = struct.unpack_from("<Q", original, 32)[0]
phnum = struct.unpack_from("<H", original, 56)[0]
codeOffset = None
for i in range(phnum):
    h = phoff + 56*i
    kind, flags, offset, address, _, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", original, h)
    if kind == 1 and address <= entry < address + filesz:
        codeOffset = offset + entry - address
assert codeOffset is not None


def changed(name, edits):
    data = bytearray(original)
    for at, value in edits:
        data[at:at+len(value)] = value
    path = output / name
    path.write_bytes(data)
    return path


def instructions(name, ops):
    return changed(name, [(codeOffset, struct.pack("<" + "I"*len(ops), *ops))])


# MOVZ x0,37; MOVZ x8,93; SVC 0: propagate a guest-selected exit code.
run(instructions("exit37", [0xd28004a0, 0xd2800ba8, 0xd4000001]), status=37)
# Unsupported guest calls must not silently become successful host calls.
run(instructions("syscall", [0xd2800c88, 0xd4000001]), status=1, diagnostic=b"unsupported Linux syscall: 100")
run(instructions("invalid-op", [0]), status=1, diagnostic=b"unsupported AArch64 instruction")
# write(1, 0, 1) -> -EFAULT, then exit with that result (low byte = 242).
run(instructions("bad-buffer", [0xd2800020, 0xd2800001, 0xd2800022,
                               0xd2800808, 0xd4000001, 0xd2800ba8, 0xd4000001]), status=242)
run(changed("wrong-machine", [(18, struct.pack("<H", 62))]), status=1, diagnostic=b"AArch64")
run(changed("bad-entry", [(24, struct.pack("<Q", 0))]), status=1, diagnostic=b"invalid guest memory")
run(changed("bad-table", [(32, struct.pack("<Q", 0xffffffffffffffff))]), status=1, diagnostic=b"header table")
run(changed("dynamic", [(phoff, struct.pack("<I", 3))]), status=1, diagnostic=b"dynamic runtime")
truncated = output / "truncated"
truncated.write_bytes(original[:20])
run(truncated, status=1, diagnostic=b"ELF")
print(f"{checks} program checks passed")

# Compile once, then prove the exported binary runs without its source ELF.
native = output / "native-echo"
subprocess.run([aero, "--program", str(elf), "-o", str(native)], check=True, timeout=60)
hidden = output / "hidden-elf"
elf.rename(hidden)
try:
    for args in [[], ["Hello from Aero"], ["one", "two"], ["", "two", ""], ["a" * 16000], ["hello \u4e16\u754c"]]:
        result = subprocess.run([str(native), *args], capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == (" ".join(args) + "\n").encode(), result.stdout
        assert result.stderr == b"", result.stderr
finally:
    hidden.rename(elf)

nativeExit = output / "native-exit"
subprocess.run([aero, "--program", str(output / "exit37"), "-o", str(nativeExit)], check=True, timeout=60)
assert subprocess.run([str(nativeExit)], timeout=10).returncode == 37
# A failed export must preserve any existing output file.
before = native.read_bytes()
failed = subprocess.run([aero, "--program", str(truncated), "-o", str(native)], capture_output=True, timeout=10)
assert failed.returncode != 0
assert native.read_bytes() == before
# -- allows a literal -o to reach the emulated program.
run(elf, ["--", "-o", "example"], b"-o example\n")
print("9 native-export and option checks passed")
