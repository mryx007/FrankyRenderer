param(
    [string]$CaptureRoot = 'E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture\chroma_1080_full6',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\Shaders\Ngu'),
    [string]$UvProbe = 'E:\OpenMadVR\work\ps4412_uv_full_1080.bin'
)

$ErrorActionPreference = 'Stop'
$CultureInfo = [System.Globalization.CultureInfo]::InvariantCulture

function Read-Capture([string]$Name) {
    $path = Join-Path $CaptureRoot $Name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "missing NGU 1080p capture: $path"
    }
    return [IO.File]::ReadAllBytes($path)
}

function Write-Asset([string]$Name, [byte[]]$Bytes) {
    $path = Join-Path $OutputRoot $Name
    [IO.File]::WriteAllBytes($path, $Bytes)
}

function Copy-Asset([string]$Source, [string]$Name) {
    Write-Asset $Name (Read-Capture $Source)
}

function Slice-Bytes([byte[]]$Bytes, [int]$Offset, [int]$Length) {
    $result = [byte[]]::new($Length)
    [Array]::Copy($Bytes, $Offset, $result, 0, $Length)
    return $result
}

function Join-ConstantRows([byte[]]$Bytes, [int[]]$Rows) {
    $result = [byte[]]::new($Rows.Count * 16)
    for ($i = 0; $i -lt $Rows.Count; ++$i) {
        [Array]::Copy($Bytes, $Rows[$i] * 16, $result, $i * 16, 16)
    }
    return $result
}

function Write-UvLut([string]$Name, [int]$Horizontal, [int]$Vertical, [int]$TextureWidth) {
    $floats = [single[]]::new($TextureWidth * 2)
    for ($x = 0; $x -lt $Horizontal; ++$x) {
        $floats[$x * 2] = [single](($x + 0.5) / $Horizontal)
    }
    for ($y = 0; $y -lt $Vertical; ++$y) {
        $floats[(($Horizontal + $y) * 2) + 1] = [single](($y + 0.5) / $Vertical)
    }
    $bytes = [byte[]]::new($floats.Length * 4)
    [Buffer]::BlockCopy($floats, 0, $bytes, 0, $bytes.Length)
    Write-Asset $Name $bytes
}

function Write-MainUvLut1080([string]$Name, [string]$ProbePath) {
    if (-not (Test-Path -LiteralPath $ProbePath)) {
        throw "missing D3D9 1080p UV probe: $ProbePath"
    }
    $probe = [IO.File]::ReadAllBytes($ProbePath)
    if ($probe.Length -ne (1920 * 1080 * 16)) {
        throw "D3D9 1080p UV probe has an unexpected size"
    }
    $textureWidth = 4096
    $floats = [single[]]::new($textureWidth * 2)
    for ($x = 0; $x -lt 1920; ++$x) {
        $floats[$x * 2] = [BitConverter]::ToSingle($probe, $x * 16)
    }
    for ($y = 0; $y -lt 1080; ++$y) {
        $floats[(1920 + $y) * 2 + 1] =
            [BitConverter]::ToSingle($probe, ($y * 1920) * 16 + 4)
    }
    $bytes = [byte[]]::new($floats.Length * 4)
    [Buffer]::BlockCopy($floats, 0, $bytes, 0, $bytes.Length)
    Write-Asset $Name $bytes
}

function Write-ScaleTable([string]$Name,
                          [int]$Rows,
                          [double]$Denominator,
                          [double[]]$Weights,
                          [bool]$Vertical) {
    $values = [single[]]::new($Rows * 8)
    for ($row = 0; $row -lt $Rows; ++$row) {
        if ($Vertical) {
            $x0 = (2 * $row + 0.5) / $Denominator
            $x1 = (2 * $row + 1.5) / $Denominator
            $x2 = (2 * $row - 0.5) / $Denominator
            $x3 = (2 * $row + 4.5) / $Denominator
        }
        else {
            $x0 = (2 * $row + 0.5) / $Denominator
            $x1 = (2 * $row - 0.5) / $Denominator
            $x2 = (2 * $row + 1.5) / $Denominator
            $x3 = (2 * $row + 4.5) / $Denominator
        }
        $values[($row * 8)] = [single]$x0
        $values[($row * 8) + 1] = [single]$Weights[0]
        $values[($row * 8) + 2] = [single]$x1
        $values[($row * 8) + 3] = [single]$Weights[1]
        $values[($row * 8) + 4] = [single]$x2
        $values[($row * 8) + 5] = [single]$Weights[2]
        $values[($row * 8) + 6] = [single]$x3
        $values[($row * 8) + 7] = [single]$Weights[3]
    }
    $bytes = [byte[]]::new($values.Length * 4)
    [Buffer]::BlockCopy($values, 0, $bytes, 0, $bytes.Length)
    Write-Asset $Name $bytes
}

if (-not (Test-Path -LiteralPath $CaptureRoot)) {
    throw "NGU 1080p capture root does not exist: $CaptureRoot"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

# These constants and scale tables are exact target captures for the
# 1920x1080 SuperChromaRes chain. Keep them separate from the 720p assets.
Copy-Asset 'const_snapshot_draw_00146_PS_520_4a9505a4.f32' 'superres_1080_down_x_constants.bin'
Copy-Asset 'const_snapshot_draw_00147_PS_520_4ee2414c.f32' 'superres_1080_down_y_constants.bin'
Write-Asset 'superres_1080_asm_main_constants.bin' `
    (Slice-Bytes (Read-Capture 'const_snapshot_draw_00149_PS_4412_c39b49cf.f32') 0 32)
Write-Asset 'superres_1080_asm_base_constants.bin' `
    (Join-ConstantRows `
        (Read-Capture 'const_snapshot_draw_00153_PS_4576_b7830945.f32') @(0, 1, 4, 5, 6, 7))
# The runtime captures store these tables through a half4 capture conversion.
# The exact 720p tables are float32 and establish the four weights; the 1080p
# table is reconstructed from those exact weights plus the target source-axis
# coordinate formula for 1920x1080 / 960x540.
Write-ScaleTable 'superres_1080_down_x_table.bin' 960 1920 `
    @(0.4651162624359131, 0.26744183897972107,
      0.26744183897972107, 0.0) $false
Write-ScaleTable 'superres_1080_down_y_table.bin' 540 1080 `
    @(0.4456869065761566, 0.4456869065761566,
      0.12300319224596024, -0.014376997016370296) $true

# The UV LUTs are exact output-pixel-center coordinate tables for the captured
# 960x1080 / 960x540 scheduler surfaces. They are generated rather than
# borrowed from the 720p assets.
Write-UvLut 'superres_1080_down_x_uv_lut.bin' 960 1080 2040
Write-UvLut 'superres_1080_down_y_uv_lut.bin' 960 540 1500
Write-MainUvLut1080 'superres_1080_uv_lut.bin' $UvProbe
