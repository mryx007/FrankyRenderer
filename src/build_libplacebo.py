"""Build the pinned upstream libplacebo locally; do not install/register a renderer.

Requires Windows x64 and CPython 3.14. Compiler packages and Python wheels are
verified against checked-in SHA-256 locks. No global toolchain is modified.
"""

import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent
WORK = ROOT / "build" / "libplacebo-runtime"
CACHE = ROOT / "deps" / "packages"
SDK = ROOT / "deps" / "msys" / "ucrt64"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cached(url, sha256, name=None):
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / (name or url.rsplit("/", 1)[1])
    if not path.exists():
        temporary = path.with_suffix(path.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        if digest(temporary) != sha256:
            raise ValueError("SHA-256 mismatch: " + path.name)
        temporary.replace(path)
    if digest(path) != sha256:
        raise ValueError("SHA-256 mismatch: " + path.name)
    return path


def short_path(path):
    # pkgconf's includedir output and windres cannot reliably handle spaces.
    # Use existing Windows 8.3 names rather than editing upstream source.
    text = ctypes.create_unicode_buffer(32768)
    if not ctypes.windll.kernel32.GetShortPathNameW(str(path), text, len(text)):
        raise OSError("Cannot obtain short build path: " + str(path))
    if " " in text.value:
        raise RuntimeError("Build from a directory without spaces or with Windows 8.3 names enabled")
    return Path(text.value)


def main():
    if sys.platform != "win32" or sys.version_info[:2] != (3, 14):
        raise RuntimeError("The pinned libplacebo build tools require Windows x64 and CPython 3.14")
    lock_paths = [ROOT / "deps" / name for name in (
        "libplacebo-source.lock.json", "libplacebo-build.lock.json", "libplacebo-python.lock.json")]
    source, tools, wheels = [json.loads(p.read_text()) for p in lock_paths]
    key = hashlib.sha256(b"".join(p.read_bytes() for p in lock_paths)
                         + (ROOT / "deps" / "packages.lock.json").read_bytes()
                         + Path(__file__).read_bytes()).hexdigest()
    WORK.mkdir(parents=True, exist_ok=True)
    stage = WORK / "stage"
    stamp = WORK / "completed.json"
    library = stage / "bin" / f"libplacebo-{source['api']}.dll"
    if stamp.exists() and library.exists():
        record = json.loads(stamp.read_text())
        if record.get("key") == key and record.get("sha256") == digest(library):
            deploy(stage, source)
            print("Restored pinned libplacebo", source["version"], source["commit"][:12])
            return

    compiler = WORK / "toolchain"
    for item in tools:
        package = cached(item["url"], item["sha256"])
        with tarfile.open(package, "r:zst") as archive:
            archive.extractall(compiler, filter="data")
        print("Verified build tool", item["package"], flush=True)
    # Runtime headers/import libraries are supplied by fetch_dependencies.py.
    shutil.copytree(SDK, compiler / "ucrt64", dirs_exist_ok=True)

    python_tools = WORK / "python-tools"
    wheel_cache = WORK / "wheels"
    wheel_cache.mkdir(exist_ok=True)
    requirements = []
    for item in wheels:
        release = json.load(urllib.request.urlopen(
            f"https://pypi.org/pypi/{item['name']}/{item['version']}/json", timeout=60))
        match = next(p for p in release["urls"] if p["filename"] == item["filename"])
        path = cached(match["url"], item["sha256"], item["filename"])
        shutil.copy2(path, wheel_cache / path.name)
        requirements.append(f"{item['name']}=={item['version']} --hash=sha256:{item['sha256']}")
    requirement_file = WORK / "python-requirements.txt"
    requirement_file.write_text("\n".join(requirements) + "\n")
    subprocess.run([sys.executable, "-m", "pip", "install", "--disable-pip-version-check", "--no-index",
                    "--find-links", str(wheel_cache), "--require-hashes", "--upgrade", "--target",
                    str(python_tools), "-r", str(requirement_file)], check=True)

    source_archive = cached(source["url"], source["sha256"], f"libplacebo-{source['commit']}.tar.gz")
    with tarfile.open(source_archive, "r:gz") as archive:
        archive.extractall(WORK, filter="data")
    work = short_path(WORK)
    prefix = work / "toolchain" / "ucrt64"
    environment = dict(os.environ)
    environment.update(PATH=str(prefix / "bin") + os.pathsep + environment.get("PATH", ""),
                       PYTHONPATH=str(work / "python-tools"), CC="gcc", CXX="g++",
                       GIT_CEILING_DIRECTORIES=str(WORK),
                       PKG_CONFIG=str(prefix / "bin" / "pkgconf.exe"),
                       PKG_CONFIG_PATH=str(prefix / "lib" / "pkgconfig"))
    # Meson build directories are tied to their original source directory.
    # Keep separate directories when updating the pinned upstream commit.
    build = work / ("compiled-" + source["commit"][:12])
    command = [sys.executable, "-m", "mesonbuild.mesonmain", "setup"]
    if (build / "meson-private" / "coredata.dat").exists():
        command.append("--reconfigure")
    command += [str(build), str(work / ("libplacebo-" + source["commit"])), "--prefix", str(work / "stage"),
                "--buildtype", "release", "-Ddefault_library=shared", "-Ddemos=false", "-Dtests=true",
                "-Dbench=false", "-Dxxhash=enabled", "-Dglslang=disabled",
                "-Dvulkan-registry=" + str(prefix / "share" / "vulkan" / "registry" / "vk.xml")]
    subprocess.run(command, env=environment, check=True)
    ninja = str(prefix / "bin" / "ninja.exe")
    subprocess.run([ninja, "-C", str(build), "-j", "4"], env=environment, check=True)
    subprocess.run([ninja, "-C", str(build), "install"], env=environment, check=True)
    stamp.write_text(json.dumps(dict(key=key, sha256=digest(library), commit=source["commit"]), indent=2))
    deploy(stage, source)
    print("Built pinned libplacebo", source["version"], source["commit"][:12])


def deploy(stage, source):
    shutil.copytree(stage / "include" / "libplacebo", SDK / "include" / "libplacebo", dirs_exist_ok=True)
    name = f"libplacebo-{source['api']}.dll"
    shutil.copy2(stage / "bin" / name, SDK / "bin" / name)
    shutil.copy2(stage / "lib" / "libplacebo.dll.a", SDK / "lib" / "libplacebo.dll.a")
    shutil.copy2(stage / "lib" / "pkgconfig" / "libplacebo.pc", SDK / "lib" / "pkgconfig" / "libplacebo.pc")
    notices = SDK / "share" / "licenses" / "libplacebo"
    notices.mkdir(parents=True, exist_ok=True)
    original = WORK / ("libplacebo-" + source["commit"])
    for name in ("LICENSE", "COPYING"):
        if (original / name).exists():
            shutil.copy2(original / name, notices / name)
    shutil.copy2(WORK / "toolchain/ucrt64/share/licenses/xxhash/LICENSE", notices / "xxhash-LICENSE")
    shutil.copy2(WORK / "python-tools/glad2-2.0.8.dist-info/LICENSE", notices / "glad-LICENSE")


if __name__ == "__main__":
    main()
