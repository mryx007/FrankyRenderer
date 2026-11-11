param(
    [string]$InputDirectory = (
        Join-Path $PSScriptRoot '..\Shaders\Ngu'
    ),
    [string]$OutputDirectory = (
        Join-Path $PSScriptRoot '..\Shaders\Ngu'
    )
)

$ErrorActionPreference = 'Stop'
$input = (Resolve-Path -LiteralPath $InputDirectory).Path
$output = [IO.Path]::GetFullPath(
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $OutputDirectory))
New-Item -ItemType Directory -Force -Path $output | Out-Null

foreach ($axis in 'x', 'y') {
    $sourcePath = Join-Path $input "luma_2160_up_$axis.hlsl"
    $source = [IO.File]::ReadAllText($sourcePath)
    $uvExpression = if ($axis -eq 'x') {
        'float2(U[0].y, sourceHeight)'
    } else {
        'float2(sourceWidth, U[0].y)'
    }
    $oldUvExpression = if ($axis -eq 'x') {
        'float2(2.0 * sourceWidth, sourceHeight)'
    } else {
        'float2(sourceWidth, 2.0 * sourceHeight)'
    }
    $replacement = $source.Replace(
        "float2 uv = input.position.xy / $oldUvExpression;",
        "// c0.y is the captured output-axis size. The 4x tail is not 2x.`n" +
        "    float2 uv = input.position.xy / $uvExpression;")
    if ($replacement -eq $source) {
        throw "2x UV normalization was not found in $sourcePath"
    }
    $outputPath = Join-Path $output "luma_4x720_tail_chroma_$axis.hlsl"
    [IO.File]::WriteAllText(
        $outputPath,
        $replacement,
        [Text.UTF8Encoding]::new($false))
    Get-FileHash -Algorithm SHA256 $outputPath
}
