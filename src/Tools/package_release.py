"""Prepare and register the shared native ZIP."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import zipfile

from package import DEFAULT_VERSION, ROOT, main


def register_user_package(stage):
    if os.name != "nt":
        return
    setup = stage / "setup.ps1"
    if not setup.is_file():
        raise ValueError(f"User registration script is missing: {setup}")
    executable = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    subprocess.run(
        [str(executable), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(setup),
         "-Action", "RegisterUser", "-Quiet"],
        cwd=stage,
        check=True,
    )


def package_release(output, channel="stable", build_id=None, register=True):
    if channel not in ("stable", "nightly"):
        raise ValueError("Unknown release channel")
    # Older Nightlies used YYYYMMDD-<commit>; current ones add the UTC time.
    if channel == "nightly" and not re.fullmatch(r"\d{8}-\d{4}(-[0-9a-f]{7,40})?", build_id or ""):
        raise ValueError("Nightly builds need --build-id YYYYMMDD-HHMM")
    if channel == "stable" and build_id:
        raise ValueError("--build-id is only used for nightly builds")
    if output.exists():
        raise ValueError(f"Use a new output directory: {output}")
    output.mkdir(parents=True)
    result = []
    version = "nightly-" + build_id if channel == "nightly" else DEFAULT_VERSION
    name = f"FrankyRenderer-{version}-x64"
    stage = output / name
    main(stage, version=version)
    if channel == "nightly":
        instructions = stage / "INSTALL.txt"
        with instructions.open("a", encoding="utf-8") as stream:
            stream.write(f"\nNightly test build: {build_id}\n"
                         "Quote this build ID when reporting test results.\n")
        manifest_path = stage / "build-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["INSTALL.txt"] = hashlib.sha256(instructions.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    archive = output / (name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                z.write(path, name + "/" + path.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert not any(name.lower().endswith((".vbs", ".exe")) for name in z.namelist())
    result.append({"archive": archive.name,
                   "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})
    (output / "SHA256SUMS.txt").write_text("".join(f"{r['sha256']}  {r['archive']}\n" for r in result))
    (output / "packages.json").write_text(json.dumps(result, indent=2) + "\n")
    if register:
        register_user_package(stage)
        print("Registered the new package for the current Windows user.")
    print("Local ZIP prepared; nothing uploaded.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--channel", choices=("stable", "nightly"), default="stable")
    parser.add_argument("--build-id", help="Nightly identifier: YYYYMMDD-HHMM (UTC)")
    parser.add_argument("--no-register", action="store_true",
                        help="Do not register the package for the current Windows user")
    args = parser.parse_args()
    package_release(args.output.resolve(), args.channel, args.build_id, not args.no_register)
