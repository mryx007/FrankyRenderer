#!/usr/bin/env python3
"""Import target-captured luma quality constants into native asset files."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

CONSTANTS = re.compile(
    r"^CAPTURE_CONSTANTS draw=(\d+) shader=(PS_\S+) values=(.*)$"
)

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = Path(r"E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture")

SOURCES = {
    "low": {
        "events": CAPTURE / "luma_quality_targets_720_v11/low/events.log",
        "tail_events": CAPTURE / "luma_quality_targets_720_v10/low/events.log",
        "initial": (140, "PS_1412_aad01c2f", 32),
        "mixer": (141, "PS_6140_e8f1d399", 220),
        "first_layers": [(draw, "PS_4280_d591580d", 148) for draw in range(142, 145)],
        "upsample": [(draw, "PS_3516_f481ce19", 132) for draw in range(145, 147)],
        "convert": [(draw, "PS_912_86f81c64", 2) for draw in range(147, 149)],
        "second_layers": [(149, "PS_4280_d591580d", 148)],
        "dense": (150, "PS_1528_f0e4bb92", 35),
        "tail_x": (146, "PS_1416_6ae3afa3", 2),
        "tail_y": (147, "PS_1404_7b76a575", 2),
    },
    "med": {
        "events": CAPTURE / "luma_quality_targets_720_v4/med/events.log",
        "tail_events": CAPTURE / "luma_quality_targets_720_v10/med/events.log",
        "second_events": CAPTURE / "luma_quality_targets_720_v10/med/events.log",
        "initial": (132, "PS_1412_aad01c2f", 32),
        "mixer": None,
        "first_layers": [(draw, "PS_3588_80fb311e", 111) for draw in range(133, 148)],
        "upsample": [(draw, "PS_4920_9257dfe6", 196) for draw in range(148, 150)],
        "convert": [(draw, "PS_912_86f81c64", 2) for draw in range(150, 152)],
        "second_layers": [(draw, "PS_4280_d591580d", 148) for draw in range(152, 154)],
        "dense": (154, "PS_1528_f0e4bb92", 35),
        "tail_x": (156, "PS_1416_6ae3afa3", 2),
        "tail_y": (157, "PS_1404_7b76a575", 2),
    },
    "high": {
        "events": CAPTURE / "luma_quality_targets_720_v8/high/events.log",
        "tail_events": CAPTURE / "luma_quality_targets_720_v16/high/events.log",
        "initial": (136, "PS_1412_aad01c2f", 32),
        "mixer": None,
        "first_layers": [(draw, "PS_3588_80fb311e", 111) for draw in range(137, 152)],
        "upsample": [(draw, "PS_4920_9257dfe6", 196) for draw in range(152, 155)],
        "convert": [(draw, "PS_912_86f81c64", 2) for draw in range(155, 158)],
        "second_layers": [
            (draw, "PS_3588_80fb311e", 111) for draw in range(158, 164)
        ],
        "dense": (164, "PS_2100_ba9671a7", 51),
        "tail_x": (166, "PS_1612_a3d450ae", 2),
        "tail_y": (167, "PS_1576_5e089fd4", 2),
    },
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def load_constants(
    event_path: Path, draw: int, shader: str, count: int
) -> bytes:
    for raw in event_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = CONSTANTS.match(raw)
        if not match:
            continue
        if int(match.group(1)) != draw or match.group(2) != shader:
            continue
        entries = json.loads(match.group(3))
        selected = next(
            (
                entry
                for entry in entries
                if int(entry["start"]) == 0 and int(entry["count"]) == count
            ),
            None,
        )
        if selected is None:
            break
        values = selected["values"]
        if len(values) != count * 4:
            raise ValueError(
                f"{event_path}: draw {draw} expected {count * 4} floats, "
                f"got {len(values)}"
            )
        return struct.pack("<" + "f" * len(values), *(float(v) for v in values))
    raise ValueError(
        f"{event_path}: missing draw {draw} {shader} constants n{count}"
    )


def write_asset(
    output: Path, relative: str, data: bytes, manifest: dict
) -> None:
    path = output / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    manifest[relative] = {
        "bytes": len(data),
        "sha256": sha256(path),
    }


def import_quality(
    quality: str,
    spec: dict,
    output: Path,
    report: dict,
) -> None:
    manifest = {}
    events = spec["events"]
    tail_events = spec["tail_events"]
    second_events = spec.get("second_events", events)
    prefix = f"ngu_luma_quality_{quality}"

    write_asset(
        output,
        f"Ngu/{prefix}_initial_constants.bin",
        load_constants(events, *spec["initial"]),
        manifest,
    )
    if spec["mixer"]:
        write_asset(
            output,
            f"Ngu/{prefix}_mixer_constants.bin",
            load_constants(events, *spec["mixer"]),
            manifest,
        )
    for index, item in enumerate(spec["first_layers"]):
        write_asset(
            output,
            f"Ngu/{prefix}_first_layer_{index:02d}_constants.bin",
            load_constants(events, *item),
            manifest,
        )
    for index, item in enumerate(spec["upsample"]):
        write_asset(
            output,
            f"Ngu/{prefix}_upsample_{index}_constants.bin",
            load_constants(events, *item),
            manifest,
        )
    for index, item in enumerate(spec["convert"]):
        write_asset(
            output,
            f"Ngu/{prefix}_convert_{index}_constants.bin",
            load_constants(events, *item),
            manifest,
        )
    for index, item in enumerate(spec["second_layers"]):
        write_asset(
            output,
            f"Ngu/{prefix}_second_layer_{index:02d}_constants.bin",
            load_constants(second_events, *item),
            manifest,
        )
    write_asset(
        output,
        f"Ngu/{prefix}_dense_constants.bin",
        load_constants(second_events, *spec["dense"]),
        manifest,
    )
    write_asset(
        output,
        f"Ngu/{prefix}_tail_x_constants.bin",
        load_constants(tail_events, *spec["tail_x"]),
        manifest,
    )
    write_asset(
        output,
        f"Ngu/{prefix}_tail_y_constants.bin",
        load_constants(tail_events, *spec["tail_y"]),
        manifest,
    )
    report[quality] = {
        "events": str(events),
        "tail_events": str(tail_events),
        "assets": manifest,
    }

def import_quality_tail_tables(output: Path, report: dict) -> None:
    low = CAPTURE / "luma_quality_targets_720_v11/low"
    manifest = {}
    write_asset(
        output,
        "Ngu/ngu_luma_quality_tail_x_table.bin",
        (low / "update_0003_6x3840_f115_184320.bin").read_bytes(),
        manifest,
    )
    write_asset(
        output,
        "Ngu/ngu_luma_quality_tail_y_table.bin",
        (low / "update_0004_6x2160_f115_103680.bin").read_bytes(),
        manifest,
    )
    report["quality_tail_tables"] = {
        "source": str(low),
        "assets": manifest,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "Shaders")
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    report = {}
    for quality, spec in SOURCES.items():
        import_quality(quality, spec, args.output, report)
    import_quality_tail_tables(args.output, report)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    for quality in SOURCES:
        roles = report[quality]["assets"]
        print(
            f"{quality}: assets={len(roles)} "
            f"first_layers={len(SOURCES[quality]['first_layers'])} "
            f"second_layers={len(SOURCES[quality]['second_layers'])}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
