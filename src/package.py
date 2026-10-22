"""Assemble a local FrankyRenderer package in dist; does not register it."""

import argparse
import hashlib
import json
import pathlib
import shutil

import pefile

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
    pending = ["placeborenderer.ax"]
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        path = available[name]
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
        shutil.copy2(path, out / path.name)

    for name in ("setup.ps1", "Install.cmd", "Uninstall.cmd", "INSTALL.txt"):
        shutil.copy2(ROOT / name, out / name)
    shutil.copytree(ROOT / "shaders", out / "shaders", dirs_exist_ok=True)

    licenses = out / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copytree(
        ROOT / "deps/msys/ucrt64/share/licenses",
        licenses / "dependencies",
        dirs_exist_ok=True,
    )
    shutil.copy2(ROOT / "deps/packages.lock.json", licenses / "packages.lock.json")
    shutil.copy2(ROOT.parent / "LICENSE", out / "LICENSE")
    shutil.copy2(ROOT.parent / "THIRD_PARTY.md", out / "THIRD_PARTY.md")

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
