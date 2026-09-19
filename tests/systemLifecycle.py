"""Opt-in console/control test using a prepared Alpine RAM guest.

Usage: python3 tests/systemLifecycle.py AERO KERNEL INITRAMFS OUTPUT_DIRECTORY
Pass --internet as a final argument to additionally check DHCP, DNS, and HTTP.
"""
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import sys
import termios
import time


def main():
    aero, kernel, initramfs, output = sys.argv[1:5]
    checkInternet = "--internet" in sys.argv[5:]
    outputDir = pathlib.Path(output).resolve()
    outputDir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, AERO_HOME=str(outputDir / "home"))

    def command(*args, success=True):
        result = subprocess.run([aero, *args], env=env, capture_output=True, text=True, timeout=15)
        assert (result.returncode == 0) == success, (args, result)
        return result

    name = "lifecycle"
    configPath = outputDir / "home/vms" / name / "configuration.toml"
    if not configPath.exists():
        command("create", name, "--kernel", str(pathlib.Path(kernel).resolve()),
                "--initrd", str(pathlib.Path(initramfs).resolve()), "--vthreads", "2", "--memory", "512")
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 37, 112, 0, 0))
    original = termios.tcgetattr(slave)
    process = subprocess.Popen([aero, "start", name, "--console"], env=env, stdin=slave, stdout=slave, stderr=slave)
    transcript = bytearray()

    def receiveUntil(marker, timeout=20):
        deadline = time.monotonic()+timeout
        while marker not in transcript:
            if time.monotonic() >= deadline or process.poll() is not None:
                raise RuntimeError("missing guest marker: " + repr(marker) + "\n" + transcript[-4000:].decode(errors="replace"))
            if select.select([master], [], [], .1)[0]:
                transcript.extend(os.read(master, 65536))

    try:
        receiveUntil(b"aero:~ #")
        assert "Running" in command("list").stdout
        command("start", name, "--headless", success=False)
        command("configure", name, "--memory", "1024", success=False)
        os.write(master, b"printf '\\nSHELL_%s\\n' ready; stty size; echo $TERM; sleep 30\n")
        receiveUntil(b"37 112")
        os.write(master, b"\x03printf '\\nCTRL_%s\\n' ready\n")
        receiveUntil(b"CTRL_ready")
        if checkInternet:
            os.write(master, b"udhcpc -i eth0 -n -q -t 3 -T 2; wget -T 10 -qO- http://example.com | grep -o 'Example Domain'\n")
            receiveUntil(b"lease of ")
            # The command itself is echoed, so require the actual HTML match on its own line.
            receiveUntil(b"\r\nExample Domain\r\n")
        os.write(master, b"\x1d")
        receiveUntil(b"shutdown requested")
        os.write(master, b"\x1d")
        assert process.wait(timeout=10) == 130
        restored = termios.tcgetattr(slave)
        restored[3] &= ~termios.PENDIN
        original[3] &= ~termios.PENDIN
        assert original == restored, "terminal modes were not restored"
        assert "Stopped" in command("list").stdout
    finally:
        if process.poll() is None:
            command("stop", name, "--force")
            process.wait(timeout=10)
        os.close(master)
        os.close(slave)
        (outputDir / "console.log").write_bytes(transcript)
    process = subprocess.Popen([aero, "start", name, "--headless"], env=env,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic()+15
        while "Running" not in command("list").stdout:
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("headless VM did not start")
            time.sleep(.05)
        command("stop", name, "--force")
        assert process.wait(timeout=15) == 130
        assert "Stopped" in command("list").stdout
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    print("Named VM lifecycle, console size, Ctrl-C, Ctrl-], headless stop, and terminal restoration passed")
    if checkInternet:
        print("Guest DHCP, DNS, and external HTTP passed")


if __name__ == "__main__":
    main()
