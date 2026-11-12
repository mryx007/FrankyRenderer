param(
    [string]$Capture = 'E:\OpenMadVR\work\ngu_10bit\target_p010_ps4576b\const_snapshot_draw_00082_PS_4576_b7830945.f32',
    [string]$Output = (Join-Path $PSScriptRoot '..\Shaders\Ngu\superres_asm_base_constants_p010.bin')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Capture)) {
    throw "Missing captured P010 PS_4576 constants: $Capture"
}
$bytes = [IO.File]::ReadAllBytes($Capture)
if ($bytes.Length -ne (32 * 16)) {
    throw "Unexpected P010 PS_4576 constant block size: $($bytes.Length)"
}

# E-032 observed P010 SuperChromaResToRGB[0][1] rows. The native ASM-facing
# shader consumes the same six rows selected for the 8-bit path.
$rows = @(0, 1, 4, 5, 6, 7)
$result = [byte[]]::new($rows.Count * 16)
for ($index = 0; $index -lt $rows.Count; ++$index) {
    [Array]::Copy($bytes, $rows[$index] * 16,
                  $result, $index * 16, 16)
}
$outputPath = [IO.Path]::GetFullPath($Output)
[IO.File]::WriteAllBytes($outputPath, $result)
[ordered]@{
    output = $outputPath
    bytes = $result.Length
    sha256 = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
} | ConvertTo-Json
