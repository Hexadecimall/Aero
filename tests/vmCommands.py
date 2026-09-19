"""Saved VM operations without virtualization privileges or guest images."""
import os
import pathlib
import subprocess
import sys
import tempfile
import tomllib

executable = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="aero-vms-", dir=sys.argv[2]) as temporary:
    root = pathlib.Path(temporary)
    env = dict(os.environ, AERO_HOME=str(root))
    kernel = root / 'kernel # "quoted"'
    kernel.write_bytes(b"fixture")

    def run(*arguments, success=True):
        result = subprocess.run([executable, *arguments], env=env, text=True, capture_output=True)
        assert (result.returncode == 0) == success, (arguments, result)
        return result

    assert "No VMs" in run("list").stdout
    run("create", "linux", "--kernel", str(kernel), "--vthreads", "4", "--memory", "2048")
    path = root / "vms/linux/configuration.toml"
    config = tomllib.loads(path.read_text())
    assert config["virtualThreads"] == 4 and config["memoryMiB"] == 2048 and config["network"]
    assert config["kernel"] == str(kernel)
    mac = config["macAddress"]
    assert mac.startswith("02:")
    assert "4 vthreads" in run("list").stdout
    assert "Kernel: " + str(kernel) in run("info", "linux").stdout
    run("configure", "linux", "--vthreads", "3", "--no-network")
    config = tomllib.loads(path.read_text())
    assert config["virtualThreads"] == 3 and not config["network"] and config["macAddress"] == mac
    run("create", "linux", "--kernel", str(kernel), success=False)
    run("create", "../escape", "--kernel", str(kernel), success=False)
    run("start", "missing", success=False)
    run("stop", "linux", success=False)
    run("start", "linux", "--console", "--headless", success=False)
    run("start", "linux", "--cpus", "4", success=False)
    assert config == tomllib.loads(path.read_text())
    path.write_text(path.read_text()+"memoryMiB = 128\n")
    assert "duplicate" in run("info", "linux", success=False).stderr
print("Saved VM configuration, validation, quoting, and lifecycle diagnostics passed")
