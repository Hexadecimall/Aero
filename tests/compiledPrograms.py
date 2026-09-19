"""Check compiler-generated code and relocated PIE against independent results."""
import math
import pathlib
import platform
import struct
import subprocess
import sys

aero, clang, linker, sourceDir, outputDir = sys.argv[1:]
sourceDir = pathlib.Path(sourceDir)
outputDir = pathlib.Path(outputDir)
outputDir.mkdir(parents=True, exist_ok=True)
startObject = outputDir / "start.o"
subprocess.run([clang, "--target=aarch64-linux-gnu", "-c", str(sourceDir / "start.S"),
                "-o", str(startObject)], check=True)


def expectedOutput(args):
    lines = ["Aero", "34", str(math.factorial(len(args) + 5))]
    for arg in args:
        value = 5381
        for byte in arg.encode():
            value = ((value * 33) ^ byte) & ((1 << 64) - 1)
        lines.append(str(value))
    return ("\n".join(lines) + "\n").encode()


checks = 0
for optimization in ["0", "1", "2"]:
    for pie in [False, True]:
        name = "compiledO" + optimization + ("Pie" if pie else "Static")
        obj = outputDir / (name + ".o")
        elf = outputDir / name
        native = outputDir / (name + "Native")
        subprocess.run([clang, "--target=aarch64-linux-gnu", "-O" + optimization, "-fPIE",
                        "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-mgeneral-regs-only",
                        "-fno-optimize-sibling-calls", "-c", str(sourceDir / "compiledProgram.c"),
                        "-o", str(obj)], check=True)
        linkMode = ["-pie", "--no-dynamic-linker"] if pie else ["-static"]
        subprocess.run([linker, "-m", "aarch64elf", *linkMode, "-e", "_start", str(startObject),
                        str(obj), "-o", str(elf)], check=True)
        subprocess.run([aero, "--program", str(elf), "-o", str(native)], check=True, timeout=60)
        for args in [[], ["hello", "world"], ["", "a" * 80, "\u4e16\u754c"]]:
            expected = expectedOutput(args)
            for command in [[aero, "--program", str(elf)], [str(native)]]:
                result = subprocess.run([*command, *args], capture_output=True, timeout=10)
                assert result.returncode == 0, (name, command, result.stderr)
                assert result.stdout == expected, (name, command, result.stdout, expected)
                checks += 1

        if pie:
            data = bytearray(elf.read_bytes())
            # Corrupt the relocation type in SHT_RELA, leaving executable code unchanged.
            sectionTable = struct.unpack_from("<Q", data, 40)[0]
            sectionSize, sectionCount = struct.unpack_from("<HH", data, 58)
            for index in range(sectionCount):
                section = sectionTable + sectionSize * index
                if struct.unpack_from("<I", data, section + 4)[0] == 4:
                    relocationOffset = struct.unpack_from("<Q", data, section + 24)[0]
                    struct.pack_into("<Q", data, relocationOffset + 8, 9999)
                    broken = outputDir / (name + "BadRelocation")
                    broken.write_bytes(data)
                    result = subprocess.run([aero, "--program", str(broken)], capture_output=True, timeout=10)
                    assert result.returncode != 0 and b"unsupported ELF relocation" in result.stderr, result.stderr
                    checks += 1
                    break
            else:
                raise AssertionError("PIE fixture must exercise a real relocation")

print(f"{checks} compiler-generated program checks passed")

if platform.machine().lower() in ["arm64", "aarch64"]:
    oracle = outputDir / "instructionOracle"
    native = outputDir / "instructionNative"
    guest = outputDir / "instructionGuest"
    probeC = sourceDir / "instructionProbe.c"
    probeAssembly = sourceDir / "instructionProbe.S"
    subprocess.run([clang, "-DaeroHostOracle", str(probeC), str(probeAssembly), "-o", str(oracle)], check=True)
    expected = subprocess.run([str(oracle)], capture_output=True, check=True).stdout
    assert len(expected) == 24 * 8
    probeObjects = []
    for source in [probeC, probeAssembly]:
        obj = outputDir / (source.name + ".o")
        subprocess.run([clang, "--target=aarch64-linux-gnu", "-O1", "-ffreestanding", "-fno-stack-protector",
                        "-mgeneral-regs-only", "-c", str(source), "-o", str(obj)], check=True)
        probeObjects.append(str(obj))
    subprocess.run([linker, "-m", "aarch64elf", "-static", "-e", "_start", str(startObject),
                    *probeObjects, "-o", str(guest)], check=True)
    subprocess.run([aero, "--program", str(guest), "-o", str(native)], check=True, timeout=60)
    for command in [[aero, "--program", str(guest)], [str(native)]]:
        result = subprocess.run(command, capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == expected, (list(struct.iter_unpack("<Q", result.stdout)),
                                           list(struct.iter_unpack("<Q", expected)))
    print("24 instruction results match native ARM64 hardware in both execution modes")
else:
    print("Native ARM64 hardware comparison skipped on this host architecture")
