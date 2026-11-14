#!/usr/bin/env python3
"""Report component-wise differences between two R16G16B16A16 dumps."""

from __future__ import annotations

import argparse
import array
import collections
import pathlib
import sys


def read_words(path: pathlib.Path) -> array.array[int]:
    words = array.array("H")
    with path.open("rb") as source:
        words.fromfile(source, path.stat().st_size // words.itemsize)
    if sys.byteorder != "little":
        words.byteswap()
    return words


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("actual", type=pathlib.Path)
    parser.add_argument("oracle", type=pathlib.Path)
    parser.add_argument("--components", type=int, default=4)
    parser.add_argument("--top", type=int, default=20)
    options = parser.parse_args()

    actual = read_words(options.actual)
    oracle = read_words(options.oracle)
    if len(actual) != len(oracle):
        parser.error(f"word count differs: actual={len(actual)} oracle={len(oracle)}")
    if options.components <= 0 or len(actual) % options.components:
        parser.error("word count is not divisible by --components")

    components = options.components
    stats = [[0, 0, 0] for _ in range(components)]  # count, signed min, signed max
    combined = collections.Counter()
    different_pixels = 0
    for base in range(0, len(actual), components):
        pixel = tuple(int(actual[base + c]) - int(oracle[base + c]) for c in range(components))
        if any(pixel[:3]):
            different_pixels += 1
            combined[pixel[:3]] += 1
        for channel, delta in enumerate(pixel):
            if delta:
                entry = stats[channel]
                entry[0] += 1
                entry[1] = min(entry[1], delta)
                entry[2] = max(entry[2], delta)

    print(f"words={len(actual)} pixels={len(actual) // components} different_pixels={different_pixels}")
    for channel, (count, minimum, maximum) in enumerate(stats):
        print(f"component[{channel}] different={count} min={minimum} max={maximum}")
    for pixel, count in combined.most_common(options.top):
        print(f"rgb_delta={pixel} pixels={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
