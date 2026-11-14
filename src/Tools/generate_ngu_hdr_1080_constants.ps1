param(
    [string]$CaptureRoot = 'E:\OpenMadVR\work\hdr_ngu_boundary_20261114\madvr_ngu_capture',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\Shaders\Ngu')
)

$ErrorActionPreference = 'Stop'

function Read-Capture([string]$Name) {
    $path = Join-Path $CaptureRoot $Name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "missing HDR NGU capture: $path"
    }
    return [IO.File]::ReadAllBytes($path)
}

function Write-Asset([string]$Name, [byte[]]$Bytes) {
    [IO.File]::WriteAllBytes((Join-Path $OutputRoot $Name), $Bytes)
}

function Join-ConstantRows([byte[]]$Bytes, [int[]]$Rows) {
    $result = [byte[]]::new($Rows.Count * 16)
    for ($i = 0; $i -lt $Rows.Count; ++$i) {
        [Array]::Copy($Bytes, $Rows[$i] * 16, $result, $i * 16, 16)
    }
    return $result
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

# PS_4412 main constants are the first two rows of the target snapshot.
$main = Read-Capture 'const_snapshot_draw_00078_PS_4412_c39b49cf.f32'
Write-Asset 'superres_1080_asm_main_constants_hdr.bin' ($main[0..31])

# PS_4576 uses the same selected rows as the SDR generator so the comparison
# isolates HDR color-domain values.
$base = Read-Capture 'const_snapshot_draw_00194_PS_4576_b7830945.f32'
Write-Asset 'superres_1080_asm_base_constants_hdr.bin' (
    Join-ConstantRows $base @(0, 1, 4, 5, 6, 7))
