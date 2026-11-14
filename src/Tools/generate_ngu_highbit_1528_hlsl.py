#!/usr/bin/env python3
"""Translate the captured PS_1528 ps_3_0 register program to PS_5_0 HLSL."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


COMPONENTS = "xyzw"


def split_operands(text: str) -> list[str]:
    return [part.strip() for part in text.split(",")]


def parse_destination(text: str) -> tuple[str, str]:
    if "." in text:
        name, mask = text.split(".", 1)
    else:
        name, mask = text, COMPONENTS
    return name, mask


def parse_source(text: str) -> tuple[str, str, bool, bool]:
    text = text.strip()
    negative = text.startswith("-")
    if negative:
        text = text[1:]
    absolute = "_abs" in text
    text = text.replace("_abs", "")
    if "." in text:
        name, swizzle = text.split(".", 1)
    else:
        name, swizzle = text, COMPONENTS
    return name, swizzle, negative, absolute


def source_component(
    source: tuple[str, str, bool, bool],
    destination_component: str,
    snapshots: dict[str, str],
) -> str:
    name, swizzle, negative, absolute = source
    if name.startswith("r") and name[1:].isdigit():
        name = snapshots.get(name, name)
    component = (
        swizzle
        if len(swizzle) == 1
        else swizzle[COMPONENTS.index(destination_component)]
    )
    value = f"{name}.{component}"
    if absolute:
        value = f"abs({value})"
    if negative:
        value = f"-({value})"
    return value


def source_vector(
    source: tuple[str, str, bool, bool],
    count: int,
    snapshots: dict[str, str],
) -> str:
    values = []
    name, swizzle, negative, absolute = source
    if name.startswith("r") and name[1:].isdigit():
        name = snapshots.get(name, name)
    if len(swizzle) == 1:
        swizzle *= count
    values = [f"{name}.{component}" for component in swizzle[:count]]
    if absolute:
        values = [f"abs({value})" for value in values]
    if negative:
        values = [f"-({value})" for value in values]
    return f"float{count}({', '.join(values)})"


def translate(asm: Path, output: Path) -> None:
    definitions: list[tuple[str, str]] = []
    instructions: list[tuple[str, str]] = []
    for line in asm.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        definition = re.match(r"def\s+(c\d+),\s*(.*)", text)
        if definition:
            values = [value.strip() for value in definition.group(2).split(",")][:4]
            values += ["0"] * (4 - len(values))
            definitions.append((definition.group(1), ", ".join(values)))
            continue
        if (
            not text
            or text.startswith("//")
            or text.startswith("ps_")
            or text.startswith("dcl_")
        ):
            continue
        match = re.match(r"([a-z][a-z0-9_]*)\s+(.*)", text)
        if match:
            instructions.append((match.group(1), match.group(2)))

    registers = sorted(
        {
            match.group(0)
            for _, text in instructions
            for match in re.finditer(r"\br\d+\b", text)
        },
        key=lambda name: int(name[1:]),
    )

    lines = [
        "// Generated from PS_1528_f0e4bb92.bin. Do not edit by hand.",
        "Texture2D<float4> NN1 : register(t0);",
        "Texture2D<float4> NN2 : register(t2);",
        "",
        "cbuffer NguHighBit1528 : register(b0)",
        "{",
        "    float4 weights[32];",
        "    float4 bias;",
        "    float4 pixSize;",
        "    float4 dstSize;",
        "};",
        "",
        "struct VSOut { float4 position : SV_Position; };",
        "",
        "VSOut VSMain(uint vertex : SV_VertexID)",
        "{",
        "    VSOut output;",
        "    const float2 corner = float2((vertex << 1) & 2, vertex & 2);",
        "    output.position = float4(corner * float2(2.0, -2.0) +",
        "        float2(-1.0, 1.0), 0.0, 1.0);",
        "    return output;",
        "}",
        "",
        "float4 Fetch(Texture2D<float4> tex, float2 uv)",
        "{",
        "    int width, height;",
        "    tex.GetDimensions(width, height);",
        "    int2 pixel = (int2)floor(uv * float2(width, height));",
        "    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));",
        "    return tex.Load(int3(pixel, 0));",
        "}",
        "",
        "float4 PSMain(VSOut input) : SV_Target0",
        "{",
        "    float4 v0 = float4(input.position.xy / dstSize.xy, 0.0, 1.0);",
    ]
    for register in registers:
        lines.append(f"    float4 {register} = 0.0;")
    lines.append("    float4 oC0 = 0.0;")
    for index in range(32):
        lines.append(f"    const float4 c{index} = weights[{index}];")
    lines.append("    const float4 c32 = bias;")
    lines.append("    const float4 c33 = pixSize;")
    lines.append("    const float4 c34 = dstSize;")
    for name, values in definitions:
        lines.append(f"    const float4 {name} = float4({values});")
    lines.append("")

    for opcode, text in instructions:
        operands = split_operands(text)
        destination, mask = parse_destination(operands[0])
        sources = [parse_source(operand) for operand in operands[1:]]
        snapshots = {
            source[0]: f"prev_{source[0]}"
            for source in sources
            if source[0] == destination
        }
        if snapshots:
            lines.append("    {")
            for name, snapshot in snapshots.items():
                lines.append(f"        const float4 {snapshot} = {name};")

        if opcode == "texld":
            coordinate = source_vector(sources[0], 2, snapshots)
            texture = {"s0": "NN1", "s2": "NN2"}[sources[1][0]]
            lines.append(
                f"        {destination} = Fetch({texture}, {coordinate});"
            )
        elif opcode == "dp4":
            a = source_vector(sources[0], 4, snapshots)
            b = source_vector(sources[1], 4, snapshots)
            for component in mask:
                lines.append(
                    f"        {destination}.{component} = dot({a}, {b});"
                )
        else:
            for component in mask:
                a = source_component(sources[0], component, snapshots)
                b = (
                    source_component(sources[1], component, snapshots)
                    if len(sources) > 1
                    else ""
                )
                c = (
                    source_component(sources[2], component, snapshots)
                    if len(sources) > 2
                    else ""
                )
                if opcode == "mov":
                    value = a
                elif opcode == "add":
                    value = f"({a} + {b})"
                elif opcode == "mul":
                    value = f"({a} * {b})"
                elif opcode == "mad":
                    value = f"mad({a}, {b}, {c})"
                else:
                    raise RuntimeError(f"unsupported opcode {opcode}")
                lines.append(f"        {destination}.{component} = {value};")
        if snapshots:
            lines.append("    }")

    lines.extend(["    return oC0;", "}", ""])
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asm", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    translate(args.asm, args.out)


if __name__ == "__main__":
    main()
