"""Build the pinned FFmpeg inputs on a clean Windows runner."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "src/build/ci-dv"


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "src/deps/dv-ffmpeg.lock.json").read_text())
    source = WORK / "FFmpeg"
    if not (source / ".git").exists():
        subprocess.run(["git", "init", str(source)], check=True)
    subprocess.run(["git", "-C", str(source), "fetch", "--depth=1", lock["source"], lock["commit"]], check=True)
    subprocess.run(["git", "-C", str(source), "checkout", "--detach", lock["commit"]], check=True)
    subprocess.run([sys.executable, str(ROOT / "src/Tools/prepare_dv_dependencies.py"), "--ffmpeg", str(source)],
                   check=True)


if __name__ == "__main__":
    main()
