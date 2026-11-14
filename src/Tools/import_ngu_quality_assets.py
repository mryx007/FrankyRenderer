#!/usr/bin/env python3
"""Import captured NGU Sharp quality constants into native D3D11 assets.

The shader bytecode stays in the capture only as provenance. FrankyRenderer
uses the already pass-verified D3D11 HLSL translations. This script imports
the exact float4 constant tables, which cannot be reconstructed from names or
from another quality level.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


QUALITY_ASSETS = {
    "low": {
        "initial": ["const_PS_1152_a9a5033a_c0_n22_21dd51ce.f32"],
        "mixer": [],
        "layer": [
            "const_PS_4280_d591580d_c0_n148_beef79b8.f32",
            "const_PS_4280_d591580d_c0_n148_6e8374f5.f32",
        ],
        "dense": ["const_PS_1528_f0e4bb92_c0_n35_d9c90267.f32"],
    },
    "med": {
        "initial": ["const_PS_1412_aad01c2f_c0_n32_43cc4ad8.f32"],
        "mixer": ["const_PS_6140_e8f1d399_c0_n220_1604abf3.f32"],
        "layer": [
            "const_PS_4280_d591580d_c0_n148_ccc06cd3.f32",
            "const_PS_4280_d591580d_c0_n148_03eeca76.f32",
            "const_PS_4280_d591580d_c0_n148_9a5d5939.f32",
        ],
        "dense": ["const_PS_1528_f0e4bb92_c0_n35_e87bf2e2.f32"],
    },
    "high": {
        "initial": ["const_PS_1412_aad01c2f_c0_n32_dd596298.f32"],
        "mixer": [],
        "layer": [
            "const_PS_3588_80fb311e_c0_n111_3eec1b55.f32",
            "const_PS_3588_80fb311e_c0_n111_d973d642.f32",
            "const_PS_3588_80fb311e_c0_n111_fbf3729d.f32",
            "const_PS_3588_80fb311e_c0_n111_6b1b7883.f32",
            "const_PS_3588_80fb311e_c0_n111_f794066d.f32",
            "const_PS_3588_80fb311e_c0_n111_b1a50e7d.f32",
            "const_PS_3588_80fb311e_c0_n111_0a9f75e1.f32",
            "const_PS_3588_80fb311e_c0_n111_525b090d.f32",
            "const_PS_3588_80fb311e_c0_n111_9fdf5aa4.f32",
            "const_PS_3588_80fb311e_c0_n111_831570f5.f32",
            "const_PS_3588_80fb311e_c0_n111_b1f2ecba.f32",
            "const_PS_3588_80fb311e_c0_n111_a2988851.f32",
            "const_PS_3588_80fb311e_c0_n111_b06779fa.f32",
            "const_PS_3588_80fb311e_c0_n111_323469d1.f32",
            "const_PS_3588_80fb311e_c0_n111_5fbc4c5f.f32",
        ],
        "dense": ["const_PS_2100_ba9671a7_c0_n51_6f5f01bb.f32"],
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def copy_role(
    source_dir: Path,
    output_dir: Path,
    quality: str,
    role: str,
    names: list[str],
    manifest: dict[str, object],
) -> None:
    if not names:
        return
    if role in ("initial", "mixer", "dense"):
        if len(names) != 1:
            raise ValueError(f"{quality}/{role}: expected one source")
        destination = output_dir / (
            f"ngu_quality_{quality}_{role}_constants.bin"
        )
        source = source_dir / names[0]
        shutil.copyfile(source, destination)
        manifest[role] = {
            "source": names[0],
            "source_sha256": sha256(source),
            "output": destination.name,
            "output_sha256": sha256(destination),
            "bytes": destination.stat().st_size,
        }
        return

    entries = []
    for index, name in enumerate(names):
        source = source_dir / name
        destination = output_dir / (
            f"ngu_quality_{quality}_layer_{index:02d}_constants.bin"
        )
        shutil.copyfile(source, destination)
        entries.append(
            {
                "source": name,
                "source_sha256": sha256(source),
                "output": destination.name,
                "output_sha256": sha256(destination),
                "bytes": destination.stat().st_size,
            }
        )
    manifest[role] = entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    report: dict[str, object] = {
        "capture_root": str(args.capture_root.resolve()),
        "qualities": {},
    }
    for quality, roles in QUALITY_ASSETS.items():
        source_dir = args.capture_root / quality
        manifest: dict[str, object] = {}
        for role, names in roles.items():
            copy_role(
                source_dir,
                args.output_dir,
                quality,
                role,
                names,
                manifest,
            )
        report["qualities"][quality] = manifest

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    for quality, roles in report["qualities"].items():
        layer_entries = roles.get("layer", [])
        print(
            f"{quality}: layers={len(layer_entries)} "
            f"initial={'initial' in roles} mixer={'mixer' in roles}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
