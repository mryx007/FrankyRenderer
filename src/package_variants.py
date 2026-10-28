"""Prepare the two local ZIPs. Never installs, commits, pushes or uploads."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from package import ROOT, main


def package_variants(output):
    if output.exists():
        raise ValueError(f"Use a new output directory: {output}")
    output.mkdir(parents=True)
    result = []
    for variant, name, source in [
        ("native", "FrankyRenderer-1.0.9-MPC-BE-x64", ROOT / "build/bin"),
        ("potplayer", "FrankyRenderer-1.0.9-PotPlayer-x64", ROOT / "build/variants/potplayer/bin"),
    ]:
        stage = output / name
        main(stage, source, variant)
        archive = output / (name + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    z.write(path, name + "/" + path.relative_to(stage).as_posix())
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert not any(name.lower().endswith((".vbs", ".exe")) for name in z.namelist())
        result.append({"variant": variant, "archive": archive.name,
                       "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})
    (output / "SHA256SUMS.txt").write_text("".join(f"{r['sha256']}  {r['archive']}\n" for r in result))
    (output / "packages.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Local ZIPs prepared; nothing uploaded.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    package_variants(parser.parse_args().output.resolve())
