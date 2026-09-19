"""Prepare a RAM guest entirely from local Alpine files. Downloads nothing."""
import argparse
import gzip
import pathlib
import shutil
import stat
import tarfile


def addEntry(archive, name, data, mode, major=0, minor=0):
    nameBytes = name.encode() + b"\0"
    fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(nameBytes), 0]
    archive.extend(("070701" + "".join(f"{value:08x}" for value in fields)).encode())
    archive.extend(nameBytes)
    archive.extend(b"\0" * (-len(archive) % 4))
    archive.extend(data)
    archive.extend(b"\0" * (-len(archive) % 4))


def safeName(name):
    while name.startswith("./"):
        name = name[2:]
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("unsafe archive member: " + name)
    return str(path)


def readModules(path):
    data = path.read_bytes()
    if data.startswith(b"\x1f\x8b"):
        data = gzip.decompress(data)
    offset = 0
    entries = {}
    while offset + 110 <= len(data):
        header = data[offset:offset+110]
        if header[:6] != b"070701":
            raise ValueError("module initramfs must use newc cpio (optionally gzip compressed)")
        fields = [int(header[6+i*8:14+i*8], 16) for i in range(13)]
        mode, size, nameSize = fields[1], fields[6], fields[11]
        nameStart = offset+110
        contentStart = (nameStart+nameSize+3) & ~3
        if nameSize < 1 or contentStart+size > len(data):
            raise ValueError("truncated module initramfs")
        name = safeName(data[nameStart:nameStart+nameSize-1].decode())
        if name == "TRAILER!!!":
            break
        if name.startswith("lib/modules/") or name == "lib/modules":
            entries[name] = (mode, data[contentStart:contentStart+size])
        offset = (contentStart+size+3) & ~3
    if not entries:
        raise ValueError("no kernel modules in the supplied initramfs")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=pathlib.Path, required=True)
    parser.add_argument("--rootfs", type=pathlib.Path, required=True, help="local Alpine minirootfs tar archive")
    parser.add_argument("--modules", type=pathlib.Path, required=True, help="local initramfs matching the kernel")
    parser.add_argument("--output", type=pathlib.Path, required=True, help="new output directory")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output directory already exists; choose a new directory")
    if not args.kernel.is_file():
        parser.error("kernel is not a regular file")
    entries = {}
    with tarfile.open(args.rootfs) as archive:
        for item in archive:
            name = safeName(item.name)
            if name == ".":
                continue
            if item.isdir():
                entries[name] = (stat.S_IFDIR | item.mode, b"")
            elif item.issym():
                entries[name] = (stat.S_IFLNK | item.mode, item.linkname.encode())
            elif item.isfile() or item.islnk():
                entries[name] = (stat.S_IFREG | item.mode, archive.extractfile(item).read())
    entries.update(readModules(args.modules))
    sourceRoot = pathlib.Path(__file__).resolve().parent.parent
    entries["init"] = (0o100755, (sourceRoot/"guest/init").read_bytes())
    entries["etc/profile.d/aero.sh"] = (0o100644, (sourceRoot/"guest/profile.sh").read_bytes())
    for name in ("dev", "proc", "sys", "run", "tmp", "etc/profile.d"):
        entries.setdefault(name, (0o40755, b""))
    output = bytearray()
    for name, (mode, data) in sorted(entries.items()):
        addEntry(output, name, data, mode)
    addEntry(output, "dev/console", b"", 0o20600, 5, 1)
    addEntry(output, "TRAILER!!!", b"", 0)
    args.output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.kernel, args.output/"Image")
    (args.output/"initramfs.cpio").write_bytes(output)
    print("Prepared local RAM guest in", args.output)
    print("The matching kernel modules must include virtio GPU, network, and packet sockets.")
    print("This guest provides a Linux shell; desktop packages are not installed.")


if __name__ == "__main__":
    main()
