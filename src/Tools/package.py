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

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT.parent / "dist"
SOURCE = ROOT / "build/bin"


def main(out=OUT, source=SOURCE):
    with pefile.PE(str(source / "FrankyRenderer.ax")) as binary:
        descriptions = [table.entries.get(b"FileDescription", b"")
                        for group in getattr(binary, "FileInfo", []) for item in group
                        for table in getattr(item, "StringTable", [])]
        if b"FrankyRenderer" not in descriptions:
            raise ValueError("Renderer binary does not match the native package")
    out.mkdir(parents=True, exist_ok=True)
    for obsolete in ("Install.vbs", "Uninstall.vbs", "Installieren.vbs", "Entfernen.vbs"):
        (out / obsolete).unlink(missing_ok=True)

    available = {
        p.name.lower(): p
        for p in SOURCE.iterdir()
        if p.suffix.lower() in (".dll", ".ax")
    }
    available.update({p.name.lower(): p for p in source.iterdir() if p.suffix.lower() in (".dll", ".ax")})
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

    (out / "DolbyVisionFilter.ax").unlink(missing_ok=True)
    for old in OBSOLETE_RUNTIME_NAMES:
        (out / old).unlink(missing_ok=True)
    verify_runtime(out)

    for name in ("Install.cmd", "Uninstall.cmd"):
        shutil.copy2(ROOT / "Tools" / name, out / name)
    setup = (ROOT / "Tools/setup.ps1").read_text(encoding="utf-8-sig")
    (out / "setup.ps1").write_text(setup, encoding="utf-8-sig")
    (out / "INSTALL.txt").write_text(
        f"FrankyRenderer 1.1 - Windows x64\n\n"
        "Extract this package to a permanent folder and close all video players.\n"
        "Run Install.cmd. Administrator approval may be requested.\n"
        "Run Uninstall.cmd before moving or removing the installed folder.\n"
        "No additional DV filter needs to be added in the player.\n"
        "Remove any previously configured Franky DV Filter from the player's preferred filters.\n\n"
        "Select FrankyRenderer in the player\'s video renderer settings.\n"
        "Requires PotPlayer 261030 Beta 64-bit or newer with native FrankyRenderer support,\n"
        "or MPC-BE with native FrankyRenderer support.\n"
        "PotPlayer beta: https://t1.daumcdn.net/potplayer/beta/PotPlayerSetup64.exe\n"
        "Renderer CLSID: {B864E92A-1BDF-4DA1-9004-CBF467DF239A}\n"
        "The original MPC Video Renderer may remain installed.\n\n"
        "Dolby Vision Profile 7 FEL/MEL is detected and processed automatically.\n"
        "Existing splitters and base-layer video decoders remain in use.\n"
        "Supported: local progressive UHD 3840x2160 Profile 7 files; FEL enhancement 1920x1080.\n"
        "The decoder must deliver 10-bit P010 or native D3D11 surfaces.\n"
        "SDR/HDR10 and other Dolby Vision profiles retain their existing renderer path.\n"
        "The renderer reads the same local file for enhancement data. Network streams are not supported.\n\n"
        "Existing installations with the same identity must be uninstalled first;\n"
        "the installer refuses to overwrite another installation.\n\n"
        "Tested on Windows 11 x64. Windows 10 x64 is expected to work but is untested.\n"
        "Keep all runtime DLLs and shaders beside FrankyRenderer.ax.\n"
        "See LICENSE, THIRD_PARTY.md and licenses for notices.\n"
        "Source: https://github.com/mryx007/FrankyRenderer\n", encoding="utf-8",
    )
    shutil.copytree(ROOT / "Shaders", out / "shaders", dirs_exist_ok=True)

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

    shutil.copytree(ROOT / "deps/lavffmpeg/licenses", licenses / "ffmpeg", dirs_exist_ok=True)
    shutil.copy2(ROOT / "deps/dv-ffmpeg.lock.json", licenses / "ffmpeg/source-and-runtime.json")
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
    parser.add_argument("--source", type=pathlib.Path, default=SOURCE)
    args = parser.parse_args()
    main(args.output.resolve(), args.source.resolve())
