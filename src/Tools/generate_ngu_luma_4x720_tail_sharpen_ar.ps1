param(
    [string]$InputHlsl = (
        'E:\OpenMadVR\artifacts_20261031\hlsl_all\SharpenAR(1,1,1.hlsl'
    ),
    [string]$OutputPath = (
        Join-Path $PSScriptRoot (
            '..\Shaders\Ngu\luma_4x720_tail_sharpen_ar.hlsl')
    )
)

$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $InputHlsl).Path)
$marker = 'float4 PS(float4 v0 : TEXCOORD0) : COLOR {'
$index = $source.IndexOf($marker, [StringComparison]::Ordinal)
if ($index -lt 0) {
    throw "PS entry point was not found in $InputHlsl"
}
$body = $source.Substring($index)
$body = $body.Replace($marker, @'
float4 PSMain(VSOut input) : SV_Target0 {
  const int2 pixel = (int2)floor(input.position.xy);
  const float4 v0 = float4(
      UvLut.Load(int3(pixel.x, 0, 0)).x,
      UvLut.Load(int3(pixel.y, 1, 0)).y,
      0.0,
      1.0);
'@)
$body = $body.Replace(
    'tex2D(Source1Sampler, ',
    'SourceTex.Sample(SourceSampler, ')
$body = $body.Replace(
    'U0.xxyw',
    'float4(U0.x, U0.x, U0.y, U0.w)')

$header = @'
Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> UvLut : register(t1);
SamplerState SourceSampler : register(s0);
cbuffer Params : register(b0) { float4 U0; };
struct VSOut { float4 position : SV_Position; };
VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

'@

$output = [IO.Path]::GetFullPath(
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $OutputPath))
[IO.File]::WriteAllText(
    $output,
    $header + $body,
    [Text.UTF8Encoding]::new($false))
Get-FileHash -Algorithm SHA256 $output
