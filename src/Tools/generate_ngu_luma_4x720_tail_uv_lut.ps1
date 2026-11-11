param(
    [string]$InputLut = (
        'E:\OpenMadVR\work\d3d9_uv_lut_2880.bin'
    ),
    [int]$Height = 2880,
    [string]$OutputPath = (
        Join-Path $PSScriptRoot (
            '..\Shaders\Ngu\luma_4x720_tail_down_x_uv_lut.bin')
    )
)

$ErrorActionPreference = 'Stop'
$input = (Resolve-Path -LiteralPath $InputLut).Path
$width = 3840
$output = [IO.Path]::GetFullPath(
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $OutputPath))
$expectedBytes = $width * $Height * 2 * 4
$actualBytes = (Get-Item -LiteralPath $input).Length
if ($actualBytes -ne $expectedBytes) {
    throw "Unexpected UV LUT size: $actualBytes, expected $expectedBytes"
}

Add-Type -TypeDefinition @'
using System;
using System.IO;

public static class NguDownXUvLut
{
    public static void Pack(string inputPath, string outputPath, int height)
    {
        const int width = 3840;
        byte[] bytes = File.ReadAllBytes(inputPath);
        if (bytes.Length != width * height * 2 * sizeof(float))
            throw new InvalidDataException("Unexpected UV LUT size");

        float[] u = new float[width];
        float[] v = new float[height];
        for (int x = 0; x < width; ++x)
            u[x] = BitConverter.ToSingle(bytes, x * 2 * sizeof(float));
        for (int y = 0; y < height; ++y)
            v[y] = BitConverter.ToSingle(
                bytes, ((y * width) * 2 + 1) * sizeof(float));

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int offset = (y * width + x) * 2 * sizeof(float);
                if (BitConverter.ToSingle(bytes, offset) != u[x] ||
                    BitConverter.ToSingle(bytes, offset + sizeof(float)) != v[y])
                    throw new InvalidDataException(
                        "UV LUT is not separable at " + x + "," + y);
            }
        }

        using FileStream stream = File.Create(outputPath);
        using BinaryWriter writer = new BinaryWriter(stream);
        foreach (float value in u)
        {
            writer.Write(value);
            writer.Write(0.0f);
        }
        for (int i = 0; i < width; ++i)
        {
            writer.Write(0.0f);
            writer.Write(i < height ? v[i] : 0.0f);
        }
    }
}
'@

[NguDownXUvLut]::Pack($input, $output, $Height)

Get-FileHash -Algorithm SHA256 $output
