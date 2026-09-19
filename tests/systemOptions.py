"""Reject malformed system arguments before reaching the host backend."""
import subprocess
import sys

for args, expected in [
    ([], "requires --kernel"),
    (["--unknown"], "unknown system option"),
    (["--cpus", "2"], "unknown system option"),
    (["--vcpus", "2"], "unknown system option"),
    (["--console", "--headless"], "choose one"),
    (["--kernel"], "requires a value"),
    (["--memory", "-1"], "positive integer"),
    (["--vthreads", "0"], "positive integer"),
    (["--vthreads", "4294967296"], "positive integer"),
    (["--memory", "18446744073709551615"], "positive integer"),
    (["--memory", "512MB"], "positive integer"),
    (["--kernel", ""], "nonempty value"),
    (["--kernel", "missing-kernel", "--read-only"], "requires --disk"),
    (["--kernel", "missing-kernel"], "not a regular file"),
]:
    result = subprocess.run([sys.argv[1], "--system"] + args, capture_output=True, text=True)
    assert result.returncode != 0 and expected in result.stderr, (args, result)
print("System option diagnostics passed")
