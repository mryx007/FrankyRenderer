param(
    [string]$CaptureDirectory = (
        'E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture\' +
        'luma_720_4k_20261101'
    ),
    [string]$OutputDirectory = (
        Join-Path $PSScriptRoot '..\Shaders\Ngu'
    )
)

$ErrorActionPreference = 'Stop'
$capture = (Resolve-Path -LiteralPath $CaptureDirectory).Path
$output = [IO.Path]::GetFullPath(
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $OutputDirectory))
New-Item -ItemType Directory -Force $output | Out-Null

function Copy-Capture([string]$name, [string]$outputName, [int]$bytes) {
    $source = Join-Path $capture $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing capture asset: $source"
    }
    $data = [IO.File]::ReadAllBytes($source)
    if ($data.Length -ne $bytes) {
        throw "$name has $($data.Length) bytes, expected $bytes"
    }
    [IO.File]::WriteAllBytes((Join-Path $output $outputName), $data)
}

function Join-Capture(
    [string[]]$names,
    [string]$outputName,
    [int]$itemBytes
) {
    $all = [Collections.Generic.List[byte]]::new()
    foreach ($name in $names) {
        $source = Join-Path $capture $name
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Missing capture asset: $source"
        }
        $data = [IO.File]::ReadAllBytes($source)
        if ($data.Length -ne $itemBytes) {
            throw "$name has $($data.Length) bytes, expected $itemBytes"
        }
        $all.AddRange($data)
    }
    [IO.File]::WriteAllBytes((Join-Path $output $outputName), $all.ToArray())
}

Copy-Capture 'const_snapshot_draw_00219_PS_520_4a9505a4.f32' `
    'luma_4x720_scale_x_constants.bin' 32
Copy-Capture 'const_snapshot_draw_00220_PS_520_4ee2414c.f32' `
    'luma_4x720_scale_y_constants.bin' 32
Copy-Capture 'const_snapshot_draw_00221_PS_1412_aad01c2f.f32' `
    'luma_4x720_initial_constants.bin' 512
Copy-Capture 'scale_update_18_0x203c7bf8b40_0x203c7bf88a0.bin' `
    'luma_4x720_scale_table_x.bin' 163840
Copy-Capture 'scale_update_19_0x203c7bf9b00_0x203c7bf8c80.bin' `
    'luma_4x720_scale_table_y.bin' 92160

$layers = 222..236 | ForEach-Object {
    'const_snapshot_draw_{0:D5}_PS_3588_80fb311e.f32' -f $_
}
Join-Capture $layers 'luma_4x720_low_layer_constants.bin' 1776

Get-ChildItem $output -File |
    Where-Object Name -Like 'luma_4x720_*' |
    Sort-Object Name |
    Select-Object Name, Length
