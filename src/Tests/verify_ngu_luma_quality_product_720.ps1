param(
    [ValidateSet('low', 'med', 'high')]
    [string]$Quality,
    [ValidateSet('low', 'med', 'high', 'veryHigh')]
    [string]$ChromaQuality = 'veryHigh',
    [string]$Ax = (Join-Path $PSScriptRoot '..\build\bin\FrankyRenderer.ax'),
    [string]$OutputRoot = (
        Join-Path $PSScriptRoot '..\..\work\ngu-luma-quality-product-720'
    ),
    [string]$Media = 'C:\tmp\ngu\still720.avi',
    [int]$WindowWidth = 1280,
    [int]$WindowHeight = 720
)

$ErrorActionPreference = 'Stop'
$setter = 'E:\OpenMadVR\artifacts_20261031\madvr_orig\set_madvr_downscale.exe'
$delayPlayer = 'C:\tmp\v2s\play_grab_delay.exe'
$frankyPlayer = 'E:\OpenMadVR\work\play_grab_compare2.exe'
$axPath = [IO.Path]::GetFullPath($Ax)
$output = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) $Quality
New-Item -ItemType Directory -Force -Path $output | Out-Null
$targetBitmap = Join-Path $output "target_$Quality.bmp"
$productBitmap = Join-Path $output "franky_$Quality.bmp"

function Write-FixedString([IO.BinaryWriter]$Writer, [string]$Text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Text)
    if ($bytes.Length -ge 64) { throw "scaling string too long: $Text" }
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
    for ($index = 1; $index -lt 9; ++$index) { $Writer.Write([float]0) }
    $Writer.Write($Overrides)
    $Writer.Write([int]-1)
}

function Run-Bounded([string]$File, [string[]]$Arguments, [hashtable]$Environment) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $File
    $start.Arguments = ($Arguments | ForEach-Object {
        '"' + $_.ToString().Replace('"', '\"') + '"'
    }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($entry in $Environment.GetEnumerator()) {
        $start.Environment[$entry.Key] = [string]$entry.Value
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw "$File exceeded 60 seconds"
    }
    $text = $stdout.Result + $stderr.Result
    $text | Set-Content -LiteralPath (Join-Path $output ((Split-Path $File -Leaf) + '.log')) -Encoding utf8
    if ($process.ExitCode -ne 0) {
        throw "$File failed: $($process.ExitCode)`n$text"
    }
}

& $setter --renderer madvr --safe-defaults 1 `
    *> (Join-Path $output 'target-settings-safe.log')
if ($LASTEXITCODE -ne 0) { throw "safe-defaults failed: $LASTEXITCODE" }
$chromaMethod = @{
    low = 'NGUSharpLow'
    med = 'NGUSharpMed'
    high = 'NGUSharpHigh'
    veryHigh = 'NGUSharpVeryHigh'
}[$ChromaQuality]
& $setter --renderer madvr `
    --chroma-method $chromaMethod `
    --luma-up-method NGUSharp `
    --luma-ngu-quality $Quality `
    --super-chroma-res 1 `
    *> (Join-Path $output 'target-settings.log')
if ($LASTEXITCODE -ne 0) { throw "target settings failed: $LASTEXITCODE" }

Run-Bounded $delayPlayer @(
    'madvr', $Media, $targetBitmap, '300', '0xb8', '1', '0'
) @{ PLAY_GRAB_START_DELAY_MS = '0' }

$qualityValue = @{ low = 0; med = 1; high = 2 }[$Quality]
$chromaValue = @{ low = 0; med = 1; high = 2; veryHigh = 3 }[$ChromaQuality]
$registryRoot = "Software\FrankyRendererTest\NguLumaQuality-$Quality-$(Get-Date -Format yyyyMMddHHmmssfff)"
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($registryRoot)
try {
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        Write-Scaler $writer 'ngu_sharp' $qualityValue 512
        Write-Scaler $writer 'mitchell' 0 0
        Write-Scaler $writer 'ngu_sharp' $chromaValue 512
        Write-Scaler $writer 'auto' 0 0
        Write-Scaler $writer 'auto' 0 0
        $writer.Write([float]-1)
        $bytes = $stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
    $key.SetValue('ScalingV1', $bytes, [Microsoft.Win32.RegistryValueKind]::Binary)
    $key.SetValue('DitherMethod', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
    $key.SetValue('DitherTemporal', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
} finally {
    $key.Close()
}

try {
    Run-Bounded $frankyPlayer @(
        'franky', $Media, $productBitmap, '300', '0xb8', '1', '0'
    ) @{
        PLAY_GRAB_FRANKY_AX = $axPath
        PLAY_GRAB_START_DELAY_MS = '0'
        PLAY_GRAB_WINDOW_WIDTH = [string]$WindowWidth
        PLAY_GRAB_WINDOW_HEIGHT = [string]$WindowHeight
        PLAY_GRAB_OUTPUT_WIDTH = '3840'
        PLAY_GRAB_OUTPUT_HEIGHT = '2160'
        PLACEBO_RENDERER_TEST_ROOT = $registryRoot
        PLACEBO_RENDERER_TEST_DEFER_APPLY = '1'
        FRANKY_NGU_TRACE = '1'
    }
} finally {
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registryRoot, $false)
}

$comparison = & python (
    Join-Path $PSScriptRoot '..\Tools\compare_bmp32.py'
) $productBitmap $targetBitmap 2>&1
$comparison | Set-Content -LiteralPath (Join-Path $output 'comparison.log') -Encoding utf8
$comparisonText = ($comparison -join "`n")
$maximumMatch = [regex]::Match($comparisonText, 'maximum=(\d+)')
$overOneMatch = [regex]::Match($comparisonText, 'diff_gt_1_pixels=(\d+)')
if (-not $maximumMatch.Success -or -not $overOneMatch.Success) {
    throw "comparison metrics are unavailable"
}
$maximum = [int]$maximumMatch.Groups[1].Value
$overOne = [int]$overOneMatch.Groups[1].Value
if ($maximum -gt 96 -or $overOne -gt 2500000) {
    throw "DirectShow frame regression: maximum=$maximum diff_gt_1_pixels=$overOne"
}
$traceLog = Join-Path $output 'play_grab_compare2.exe.log'
$trace = Get-Content -Raw -LiteralPath $traceLog
if ($trace -notmatch "NGU quality luma=$qualityValue chroma=$chromaValue" -or
    $trace -notmatch 'canonical NGU source: complete' -or
    $trace -notmatch 'NGU output hook returned 3840x2160 target texture') {
    throw "DirectShow did not execute the selected native NGU path"
}
$summary = [ordered]@{
    quality = $Quality
    chroma_quality = $ChromaQuality
    window = "${WindowWidth}x${WindowHeight}"
    target = $targetBitmap
    product = $productBitmap
    target_sha256 = (Get-FileHash -LiteralPath $targetBitmap -Algorithm SHA256).Hash
    product_sha256 = (Get-FileHash -LiteralPath $productBitmap -Algorithm SHA256).Hash
    maximum = $maximum
    diff_gt_1_pixels = $overOne
    comparison = $comparison
}
$summary | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
Write-Output "PASS_PRODUCT $Quality luma + $ChromaQuality chroma DirectShow maximum=$maximum diff_gt_1_pixels=$overOne"
$comparison
