"""Build the pinned FFmpeg libraries the renderer uses; do not install anything.

Only what Dolby Vision processing needs is enabled: HEVC decoding (D3D11VA)
for the enhancement layer and MKV/MP4/TS reading. The options are pinned in
deps/dv-ffmpeg.lock.json and shipped with the package. The compiler comes from
the hash-verified packages that also build libplacebo; make and nasm from
deps/ffmpeg-build.lock.json. configure needs a POSIX shell: Git for Windows'.
"""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

from build_libplacebo import SDK, cached, digest, short_path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "ffmpeg-runtime"
LOCK = ROOT / "deps" / "dv-ffmpeg.lock.json"
TOOL_LOCKS = [ROOT / "deps" / "libplacebo-build.lock.json", ROOT / "deps" / "ffmpeg-build.lock.json"]


def git_bash():
    git = shutil.which("git")
    if git:
        for folder in Path(git).resolve().parents:
            if (folder / "bin" / "bash.exe").exists() and (folder / "usr" / "bin").exists():
                return folder
    raise RuntimeError("Git for Windows (bash) is required to configure FFmpeg")


def build(source):
    """Builds from the pinned source checkout; returns the folder with the DLLs."""
    lock = json.loads(LOCK.read_text())
    key = hashlib.sha256(b"".join(p.read_bytes() for p in [LOCK, *TOOL_LOCKS, Path(__file__)])).hexdigest()
    output = WORK / "bin"
    stamp = WORK / "completed.json"
    names = [Path(library).name for library in lock["libraries"]]
    if stamp.exists() and all((output / name).exists() for name in names):
        record = json.loads(stamp.read_text())
        if record.get("key") == key and record.get("sha256") == {n: digest(output / n) for n in names}:
            print("Restored pinned FFmpeg", lock["commit"][:12])
            return output

    shutil.rmtree(WORK, ignore_errors=True)
    toolchain = WORK / "toolchain"
    for lock_path in TOOL_LOCKS:
        for item in json.loads(lock_path.read_text()):
            with tarfile.open(cached(item["url"], item["sha256"]), "r:zst") as archive:
                archive.extractall(toolchain, filter="data")
            print("Verified build tool", item["package"], flush=True)
    shutil.copytree(SDK, toolchain / "ucrt64", dirs_exist_ok=True)
    # Link zlib statically: a folder holding only the static library wins.
    (WORK / "zlib").mkdir(parents=True)
    shutil.copy2(toolchain / "ucrt64" / "lib" / "libz.a", WORK / "zlib" / "libz.a")
    # Build in a copy: configure writes into the tree, and native make needs
    # relative paths inside it.
    shutil.copytree(source, WORK / "src", ignore=shutil.ignore_patterns(".git"))
    (WORK / "tmp").mkdir()

    work = short_path(WORK)
    git = git_bash()
    environment = dict(os.environ)
    environment.update(PATH=os.pathsep.join([str(work / "toolchain" / "ucrt64" / "bin"), str(git / "usr" / "bin"),
                                             environment.get("PATH", "")]),
                       TMPDIR=(work / "tmp").as_posix(), TMP=str(work / "tmp"), TEMP=str(work / "tmp"))
    options = lock["configure"] + [f"--extra-ldflags=-static-libgcc -L{(work / 'zlib').as_posix()}"]
    configure = "./configure " + " ".join("'" + option + "'" for option in options)
    bash = str(git / "bin" / "bash.exe")
    tree = work / "src"
    subprocess.run([bash, "-c", configure], cwd=tree, env=environment, check=True)
    subprocess.run([bash, "-c", f"mingw32-make -j{os.cpu_count() or 4}"], cwd=tree, env=environment, check=True)

    output.mkdir()
    for library in lock["libraries"]:
        shutil.copy2(tree / library, output / Path(library).name)
    stamp.write_text(json.dumps(dict(key=key, commit=lock["commit"],
                                     sha256={n: digest(output / n) for n in names}), indent=2))
    # Keep only the result; the build cache stays small.
    for folder in ("toolchain", "src", "tmp", "zlib"):
        shutil.rmtree(WORK / folder, ignore_errors=True)
    print("Built pinned FFmpeg", lock["commit"][:12], flush=True)
    return output
