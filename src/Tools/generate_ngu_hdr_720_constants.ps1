param(
    [string]$Capture = 'E:\OpenMadVR\work\hdr_ngu_boundary_20261114\madvr_ngu_capture_720\events.log',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\Shaders\Ngu')
)

$ErrorActionPreference = 'Stop'

function Read-Constants([string]$Shader) {
    $line = Select-String -LiteralPath $Capture -Pattern (
        'CAPTURE_CONSTANTS .*shader=' + $Shader + ' values=(\[.*\])') |
        Select-Object -Last 1
    if (-not $line) {
        throw "missing $Shader constants in $Capture"
    }
    $match = [regex]::Match($line.Line, 'values=(\[.*\])$')
    if (-not $match.Success) {
        throw "malformed $Shader constants in $Capture"
    }
    $rows = $match.Groups[1].Value | ConvertFrom-Json
    return [float[]]$rows[0].values
}

function Write-Floats([string]$Name, [float[]]$Values) {
    $bytes = [byte[]]::new($Values.Length * 4)
    [Buffer]::BlockCopy($Values, 0, $bytes, 0, $bytes.Length)
    [IO.File]::WriteAllBytes((Join-Path $OutputRoot $Name), $bytes)
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$main = Read-Constants 'PS_4412_c39b49cf'
$base = Read-Constants 'PS_4576_b7830945'
Write-Floats 'superres_asm_main_constants_hdr.bin' $main[0..7]
$selectedBase = [float[]]($base[0..3] + $base[4..7] + $base[16..19] +
    $base[20..23] + $base[24..27] + $base[28..31])
Write-Floats 'superres_asm_base_constants_hdr.bin' $selectedBase
