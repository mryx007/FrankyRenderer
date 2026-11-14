param(
    [ValidateSet('low', 'med', 'high', 'veryHigh')]
    [string]$Quality,
    [string]$Ax = (Join-Path $PSScriptRoot '..\build\bin\FrankyRenderer.ax'),
    [string]$OutputRoot = (
        Join-Path $PSScriptRoot '..\..\work\ngu-quality-product-720'
    ),
    [string]$Media = 'C:\tmp\ngu\still720.avi',
    [string]$TargetRoot,
    [string]$ReferenceRoot = (
        'E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture\' +
        'quality_refs_product_20261115'
    ),
    [string]$Player = 'E:\OpenMadVR\work\play_grab_compare2.exe'
)

$ErrorActionPreference = 'Stop'
$captureRoot = 'E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture'
if (-not $TargetRoot) {
    $TargetRoot = if ($Quality -eq 'veryHigh') {
        Join-Path $captureRoot 'quality_ps4576_luma_vh_20261115\720\veryHigh'
    } else {
        Join-Path $captureRoot "quality_targets_20261115\720\$Quality"
    }
}

$axPath = [IO.Path]::GetFullPath($Ax)
$output = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) $Quality
$bitmap = Join-Path $output "franky_$Quality.bmp"
$handoff = Join-Path $output 'franky_ps4576.rgba16'
$summaryPath = Join-Path $output 'summary.json'
New-Item -ItemType Directory -Force -Path $output | Out-Null

$qualityValue = @{
    low = 0
    med = 1
    high = 2
    veryHigh = 3
}[$Quality]

function Write-FixedString([IO.BinaryWriter]$Writer, [string]$Text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Text)
    if ($bytes.Length -ge 64) {
        throw "scaling string too long: $Text"
    }
    $Writer.Write($bytes)
    $Writer.Write((New-Object byte[] (64 - $bytes.Length)))
}

function Write-Scaler([IO.BinaryWriter]$Writer,
                      [string]$Filter,
                      [float]$QualityOrZero,
                      [uint32]$Overrides) {
    Write-FixedString $Writer $Filter
    Write-FixedString $Writer 'auto'
    Write-FixedString $Writer 'auto'
    Write-FixedString $Writer 'auto'
    $Writer.Write($QualityOrZero)
    for ($index = 1; $index -lt 9; ++$index) {
        $Writer.Write([float]0)
    }
    $Writer.Write($Overrides)
    $Writer.Write([int]-1)
}

$registryRoot = "Software\FrankyRendererTest\NguQuality-$Quality-$(Get-Date -Format yyyyMMddHHmmssfff)"
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($registryRoot)
try {
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        Write-Scaler $writer 'ngu_sharp' 3 512
        Write-Scaler $writer 'mitchell' 0 0
        Write-Scaler $writer 'ngu_sharp' $qualityValue 512
        Write-Scaler $writer 'auto' 0 0
        Write-Scaler $writer 'auto' 0 0
        $writer.Write([float]-1)
        $bytes = $stream.ToArray()
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
    if ($bytes.Length -ne 1504) {
        throw "ScalingV1 size mismatch: $($bytes.Length)"
    }
    $key.SetValue('ScalingV1', $bytes, [Microsoft.Win32.RegistryValueKind]::Binary)
    $key.SetValue('DitherMethod', 3, [Microsoft.Win32.RegistryValueKind]::DWord)
    $key.SetValue('DitherTemporal', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
}
finally {
    $key.Close()
}

$env:PLACEBO_RENDERER_TEST_ROOT = $registryRoot
$env:PLAY_GRAB_FRANKY_AX = $axPath
$env:PLAY_GRAB_DPI_AWARE = '1'
$env:PLAY_GRAB_WINDOW_WIDTH = '1280'
$env:PLAY_GRAB_WINDOW_HEIGHT = '720'
$env:FRANKY_NGU_TRACE = '1'
$env:FRANKY_NGU_PS4576_DUMP = $handoff
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Player
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Arguments = (@(
        'franky',
        "`"$Media`"",
        "`"$bitmap`"",
        '200',
        '0xb8',
        '0',
        '0'
    ) -join ' ')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (-not $process.WaitForExit(60000)) {
            & taskkill.exe /PID $process.Id /T /F *> $null
            throw "Franky product run exceeded 60 seconds"
        }
        $process.WaitForExit()
        $stdout.Result | Set-Content `
            -LiteralPath (Join-Path $output 'play.log') -Encoding utf8
        $stderr.Result | Set-Content `
            -LiteralPath (Join-Path $output 'play.err.log') -Encoding utf8
        if ($process.ExitCode -ne 0) {
            throw "Franky product run failed: $($process.ExitCode)"
        }
    }
    finally {
        $process.Dispose()
    }
}
finally {
    foreach ($name in @(
        'PLACEBO_RENDERER_TEST_ROOT',
        'PLAY_GRAB_FRANKY_AX',
        'PLAY_GRAB_DPI_AWARE',
        'PLAY_GRAB_WINDOW_WIDTH',
        'PLAY_GRAB_WINDOW_HEIGHT',
        'FRANKY_NGU_TRACE',
        'FRANKY_NGU_PS4576_DUMP'
    )) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registryRoot, $false)
}

if (-not (Test-Path -LiteralPath $bitmap)) {
    throw "Franky output missing: $bitmap"
}
if (-not (Test-Path -LiteralPath $handoff)) {
    throw "Franky PS_4576 dump missing: $handoff"
}
if ((Get-Item -LiteralPath $handoff).Length -ne 7372800) {
    throw "Franky PS_4576 dump has the wrong size"
}
$target = Get-ChildItem -LiteralPath $TargetRoot -File `
    -Filter 'draw_*_PS_4576_b7830945_rt0_1280x720_*.bin' |
    Sort-Object Name | Select-Object -First 1
if (-not $target) {
    throw "Target PS_4576 oracle missing: $TargetRoot"
}
$actualHash = (Get-FileHash -LiteralPath $handoff -Algorithm SHA256).Hash
$targetHash = (Get-FileHash -LiteralPath $target.FullName -Algorithm SHA256).Hash
if ($actualHash -ne $targetHash) {
    throw "PS_4576 mismatch: product=$actualHash target=$targetHash"
}

$reference = Join-Path (Join-Path $ReferenceRoot $Quality) "madvr_ref_$Quality.bmp"
$comparison = @()
if (Test-Path -LiteralPath $reference) {
    $comparison = & python (
        Join-Path $PSScriptRoot '..\Tools\compare_bmp32.py'
    ) $bitmap $reference 2>&1
    $comparison | Set-Content `
        -LiteralPath (Join-Path $output 'final-frame-delta.log') -Encoding utf8
}

$summary = [ordered]@{
    quality = $Quality
    ax = $axPath
    ps4576_target = $target.FullName
    ps4576_product = $handoff
    ps4576_sha256 = $actualHash
    ps4576_exact = $true
    full_frame_bmp = $bitmap
    full_frame_bmp_sha256 = (Get-FileHash -LiteralPath $bitmap -Algorithm SHA256).Hash
    final_frame_delta = $comparison
}
$summary | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Output "PASS_PRODUCT $Quality DirectShow PS_4576 exact $actualHash"
Write-Output "Full-frame render check: $bitmap"
