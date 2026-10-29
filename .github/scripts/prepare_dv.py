"""Restore the existing pinned FFmpeg inputs on a clean Windows runner."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "src/build/ci-dv"
URL = "https://github.com/clsid2/mpc-hc/releases/download/2.8.4/MPC-HC.2.8.4.x64.zip"
SHA256 = "9feb91e27407ec048d7fe3722db0f5f06debf9841d7879bd920ab787f9b098ae"


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    archive = WORK / "mpc-hc.zip"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=120) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise ValueError("MPC-HC dependency archive checksum mismatch")
    lock = json.loads((ROOT / "src/deps/dv-ffmpeg.lock.json").read_text())
    runtime = WORK / "runtime"
    runtime.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for item in lock["runtime"]:
            data = z.read("LAVFilters64/" + item["file"])
            if hashlib.sha256(data).hexdigest() != item["sha256"]:
                raise ValueError("Pinned FFmpeg runtime mismatch: " + item["file"])
            (runtime / item["file"]).write_bytes(data)
    source = WORK / "FFmpeg"
    if not (source / ".git").exists():
        subprocess.run(["git", "init", str(source)], check=True)
    subprocess.run(["git", "-C", str(source), "fetch", "--depth=1", lock["source"], lock["commit"]], check=True)
    subprocess.run(["git", "-C", str(source), "checkout", "--detach", lock["commit"]], check=True)
    subprocess.run([sys.executable, str(ROOT / "src/prepare_dv_dependencies.py"),
                    "--ffmpeg", str(source), "--runtime", str(runtime)], check=True)


if __name__ == "__main__":
    main()
