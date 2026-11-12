"""Compare two uncompressed 32-bit BMP files in display orientation."""

import argparse
import hashlib
from pathlib import Path
import struct


def load(path: Path) -> tuple[int, int, bytes, bool]:
    data = path.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise ValueError(f"{path}: not a BMP file")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or height == 0 or bits != 32 or compression != 0:
        raise ValueError(f"{path}: requires uncompressed 32-bit BMP")
    bottom_up = height > 0
    pixels = data[offset:]
    if len(pixels) != width * abs(height) * 4:
        raise ValueError(f"{path}: unexpected pixel payload size")
    return width, abs(height), pixels, bottom_up


def row_offset(y: int, width: int, height: int, bottom_up: bool) -> int:
    source_y = height - 1 - y if bottom_up else y
    return source_y * width * 4


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("actual", type=Path)
    parser.add_argument("expected", type=Path)
    args = parser.parse_args()

    aw, ah, actual, actual_bottom_up = load(args.actual)
    ew, eh, expected, expected_bottom_up = load(args.expected)
    if (aw, ah) != (ew, eh):
        raise ValueError("BMP dimensions differ")

    different = maximum = over_one = 0
    coordinates = []
    for y in range(ah):
        actual_row = row_offset(y, aw, ah, actual_bottom_up)
        expected_row = row_offset(y, ew, eh, expected_bottom_up)
        for x in range(aw):
            ao = actual_row + x * 4
            eo = expected_row + x * 4
            pixel_over_one = False
            for channel in range(3):
                delta = abs(actual[ao + channel] - expected[eo + channel])
                if delta:
                    different += 1
                maximum = max(maximum, delta)
                if delta > 1:
                    pixel_over_one = True
            if pixel_over_one:
                over_one += 1
                if len(coordinates) < 32:
                    coordinates.append((x, y))

    print(f"width={aw} height={ah}")
    print(
        f"actual_sha256={hashlib.sha256(args.actual.read_bytes()).hexdigest()}"
    )
    print(
        f"expected_sha256={hashlib.sha256(args.expected.read_bytes()).hexdigest()}"
    )
    print(
        f"different_components={different} maximum={maximum} "
        f"diff_gt_1_pixels={over_one}"
    )
    print("coordinates=" + ",".join(f"({x},{y})" for x, y in coordinates))
    return 0 if maximum <= 1 and over_one == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
