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

function Copy-CapturePrefix([string]$name, [string]$outputName, [int]$bytes) {
    $source = Join-Path $capture $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing capture asset: $source"
    }
    $data = [IO.File]::ReadAllBytes($source)
    if ($data.Length -lt $bytes) {
        throw "$name has $($data.Length) bytes, expected at least $bytes"
    }
    $prefix = [byte[]]::new($bytes)
    [Array]::Copy($data, $prefix, $bytes)
    [IO.File]::WriteAllBytes((Join-Path $output $outputName), $prefix)
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

$upsampleLayers = 237..239 | ForEach-Object {
    'const_snapshot_draw_{0:D5}_PS_4920_9257dfe6.f32' -f $_
}
Join-Capture $upsampleLayers 'luma_4x720_upsample_constants.bin' 3136

$convertLayers = 240..242 | ForEach-Object {
    'const_snapshot_draw_{0:D5}_PS_912_86f81c64.f32' -f $_
}
Join-Capture $convertLayers 'luma_4x720_convert_constants.bin' 32

$highLayers = 243..248 | ForEach-Object {
    'const_snapshot_draw_{0:D5}_PS_3588_80fb311e.f32' -f $_
}
Join-Capture $highLayers 'luma_4x720_high_layer_constants.bin' 1776

Copy-Capture 'const_snapshot_draw_00249_PS_2100_ba9671a7.f32' `
    'luma_4x720_dense_constants.bin' 816
Copy-Capture 'const_snapshot_draw_00250_PS_564_500335c8.f32' `
    'luma_4x720_add_constants.bin' 32
Copy-CapturePrefix 'const_snapshot_draw_00252_PS_1200_ea811ad5.f32' `
    'luma_4x720_tail_down_x_constants.bin' 32
Copy-Capture 'scale_update_20_0x21521480b20_0x2152147fcc0.bin' `
    'luma_4x720_tail_down_x_table.bin' 184320
Copy-CapturePrefix 'const_snapshot_draw_00253_PS_1176_487f5b4d.f32' `
    'luma_4x720_tail_down_y_constants.bin' 32
Copy-Capture 'scale_update_21_0x2152147dfe0_0x21521481700.bin' `
    'luma_4x720_tail_down_y_table.bin' 103680
Copy-CapturePrefix 'const_snapshot_draw_00254_PS_1208_b99fedc2.f32' `
    'luma_4x720_tail_ssim_rt_constants.bin' 32
Copy-CapturePrefix 'const_snapshot_draw_00255_PS_1052_f4319416.f32' `
    'luma_4x720_tail_ssim_final_constants.bin' 32
Copy-Capture 'const_snapshot_draw_00256_PS_5868_0cb2f4f9.f32' `
    'luma_4x720_tail_sharpen_ar_constants.bin' 4096

Get-ChildItem $output -File |
    Where-Object Name -Like 'luma_4x720_*' |
    Sort-Object Name |
    Select-Object Name, Length
