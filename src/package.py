"""Assemble a local FrankyRenderer package in dist; does not register it."""

import argparse
import hashlib
import json
import pathlib
import shutil

import pefile

from runtime_names import (
    OBSOLETE_RUNTIME_NAMES, RUNTIME_NAMES, packaged_name, verify_runtime, write_packaged_binary
)

ROOT = pathlib.Path(__file__).resolve().parent
OUT = ROOT.parent / "dist"
SOURCE = ROOT / "build/bin"


def main(out=OUT):
    out.mkdir(parents=True, exist_ok=True)
    for obsolete in ("Install.vbs", "Uninstall.vbs", "Installieren.vbs", "Entfernen.vbs"):
        (out / obsolete).unlink(missing_ok=True)

    available = {
        p.name.lower(): p
        for p in SOURCE.iterdir()
        if p.suffix.lower() in (".dll", ".ax")
    }
    pending = ["frankyrenderer.ax"]
    seen = set()
    runtime_manifest = []
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        path = available[name]
        if path.suffix.lower() == ".dll" and name not in RUNTIME_NAMES:
            raise ValueError(f"No package name assigned to dependency: {path.name}")
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(
            directories=[
                pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
                pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"],
            ]
        )
        for table in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
            for entry in getattr(pe, table, []):
                dependency = entry.dll.decode().lower()
                if dependency in available:
                    pending.append(dependency)
        pe.close()
        destination = out / packaged_name(path.name)
        write_packaged_binary(path, destination)
        runtime_manifest.append({
            "original": path.name,
            "packaged": destination.name,
            "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "packaged_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
        })

    for old in OBSOLETE_RUNTIME_NAMES:
        (out / old).unlink(missing_ok=True)
    verify_runtime(out)

    for name in ("setup.ps1", "Install.cmd", "Uninstall.cmd", "INSTALL.txt"):
        shutil.copy2(ROOT / name, out / name)
    shutil.copytree(ROOT / "shaders", out / "shaders", dirs_exist_ok=True)

    licenses = out / "licenses"
    licenses.mkdir(exist_ok=True)
    (licenses / "runtime-names.json").write_text(json.dumps(runtime_manifest, indent=2) + "\n")
    shutil.copytree(
        ROOT / "deps/msys/ucrt64/share/licenses",
        licenses / "dependencies",
        dirs_exist_ok=True,
    )
    shutil.copy2(ROOT / "deps/packages.lock.json", licenses / "packages.lock.json")
    shutil.copy2(ROOT / "deps/libplacebo-source.lock.json", licenses / "libplacebo-source.lock.json")
    shutil.copy2(ROOT.parent / "LICENSE", out / "LICENSE")
    shutil.copy2(ROOT.parent / "THIRD_PARTY.md", out / "THIRD_PARTY.md")

    (licenses / "nvapi").mkdir(exist_ok=True)
    for name in ("LICENSE.txt", "source.json"):
        shutil.copy2(ROOT / "deps/nvapi" / name, licenses / "nvapi" / name)

    # Remove files from the retired local SDK prototype when reusing a stage.
    for name in ("VideoSuperResolution.exe", "nvngx_vsr.dll"):
        (out / name).unlink(missing_ok=True)
    obsolete_rtx_license = licenses / "rtx-video"
    if obsolete_rtx_license.is_dir():
        shutil.rmtree(obsolete_rtx_license)

    manifest = {
        str(p.relative_to(out)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in out.rglob("*")
        if p.is_file()
        and "registration-backup" not in p.parts
        and p.name != "build-manifest.json"
    }
    (out / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Prepared", out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=OUT,
        help="Package destination (default: dist)",
    )
    main(parser.parse_args().output.resolve())
