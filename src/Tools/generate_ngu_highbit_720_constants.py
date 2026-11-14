#!/usr/bin/env python3
"""Extract the captured P010 720p Double NGU constants from a Frida event log."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


DRAW_TO_ASSET = {
    186: ("luma_720_highbit_6140_constants.bin", 220),
    187: ("luma_720_highbit_4280_0_constants.bin", 148),
    188: ("luma_720_highbit_4280_1_constants.bin", 148),
    189: ("luma_720_highbit_4280_2_constants.bin", 148),
    190: ("luma_720_highbit_1528_constants.bin", 35),
    233: ("luma_720_highbit_to_rgb_constants.bin", 2),
}

FULL_TRACE_ASSETS = {
    417: ("ngu_highbit_720_1672_constants.bin", 672),
    446: ("ngu_highbit_720_dense_constants.bin", 1072),
    405: ("ngu_highbit_720_add_0_constants.bin", 32),
    414: ("ngu_highbit_720_add_1_constants.bin", 32),
    399: ("luma_720_highbit_1412_0_constants.bin", 512),
    408: ("luma_720_highbit_1412_1_constants.bin", 512),
}


def extract(path: Path, draw: int, expected_vectors: int) -> bytes:
    pattern = re.compile(
        rf"^CAPTURE_CONSTANTS draw={draw} shader=([^ ]+) values=(\[.*\])$"
    )
    match = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        candidate = pattern.match(line)
        if candidate:
            match = candidate
            break
    if match is None:
        raise RuntimeError(f"captured constants for draw {draw} are missing")

    records = json.loads(match.group(2))
    values: list[float] = []
    for record in records:
        start = int(record["start"])
        record_values = [float(value) for value in record["values"]]
        if start != len(values):
            raise RuntimeError(
                f"draw {draw}: expected contiguous constants at {len(values)}, "
                f"found start={start}"
            )
        values.extend(record_values)

    expected_values = expected_vectors * 4
    if len(values) != expected_values:
        raise RuntimeError(
            f"draw {draw}: expected {expected_values} floats, found {len(values)}"
        )
    return struct.pack(f"<{expected_values}f", *values)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--events", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--full-trace", type=Path)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    for draw, (name, vectors) in DRAW_TO_ASSET.items():
        payload = extract(args.events, draw, vectors)
        output = args.out / name
        output.write_bytes(payload)
        print(f"{name} {len(payload)} {hashlib.sha256(payload).hexdigest().upper()}")

    if args.full_trace is not None and args.full_trace.is_file():
        full_trace = args.full_trace.parent
        for draw, (name, expected_bytes) in FULL_TRACE_ASSETS.items():
            matches = sorted(
                full_trace.glob(f"const_snapshot_draw_{draw:05d}_*.f32")
            )
            if len(matches) != 1:
                raise RuntimeError(
                    f"expected one constants snapshot for draw {draw}, "
                    f"found {len(matches)}"
                )
            payload = matches[0].read_bytes()
            if len(payload) != expected_bytes:
                raise RuntimeError(
                    f"draw {draw}: expected {expected_bytes} bytes, "
                    f"found {len(payload)}"
                )
            output = args.out / name
            output.write_bytes(payload)
            print(
                f"{name} {len(payload)} "
                f"{hashlib.sha256(payload).hexdigest().upper()}"
            )

        for layer in range(28):
            draw = 418 + layer
            matches = sorted(
                full_trace.glob(f"const_snapshot_draw_{draw:05d}_*.f32")
            )
            if len(matches) != 1:
                raise RuntimeError(
                    f"expected one layer snapshot for draw {draw}, "
                    f"found {len(matches)}"
                )
            payload = matches[0].read_bytes()
            if len(payload) != 2352:
                raise RuntimeError(
                    f"draw {draw}: expected 2352 bytes, found {len(payload)}"
                )
            name = f"ngu_highbit_720_layer_{layer:02d}_constants.bin"
            output = args.out / name
            output.write_bytes(payload)
            print(
                f"{name} {len(payload)} "
                f"{hashlib.sha256(payload).hexdigest().upper()}"
            )


if __name__ == "__main__":
    main()
