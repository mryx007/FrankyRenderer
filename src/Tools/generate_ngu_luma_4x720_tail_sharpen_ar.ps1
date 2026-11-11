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
$body = $body.Replace(
    'float4 e2 = (w1.xyxy * DL5.zzzz + v0.xyxy);',
    'float4 e2 = mad(w1.xyxy, DL5.zzzz, v0.xyxy);')
$body = $body.Replace(
    'float4 w9 = (w7.xyxy * DL5.xxyx + v0.xyxy);',
    'float4 w9 = mad(w7.xyxy, DL5.xxyx, v0.xyxy);')
$body = $body.Replace(
    'float4 w81 = (w79.xyxy * DL7.xyzy + v0.xyxy);',
    'float4 w81 = mad(w79.xyxy, DL7.xyzy, v0.xyxy);')
$body = $body.Replace(
    'float4 w93 = (w84.xyxy * DL7.yzxz + v0.xyxy);',
    'float4 w93 = mad(w84.xyxy, DL7.yzxz, v0.xyxy);')
$body = $body.Replace(
    'float4 w99 = (w98.xyxy * DL5.yzzx + v0.xyxy);',
    'float4 w99 = mad(w98.xyxy, DL5.yzzx, v0.xyxy);')
$body = $body.Replace(
    'float4 e158 = (w156.zzzz * w146.zzzz + w156.wwww);',
    'float4 e158 = mad(w156.zzzz, w146.zzzz, w156.wwww);')
$body = $body.Replace(
    'float4 w164 = (w160.xyxy * DL5.xzzy + v0.xyxy);',
    'float4 w164 = mad(w160.xyxy, DL5.xzzy, v0.xyxy);')
$body = $body.Replace(
    'float4 e229 = (w228.wwww * w225.zzzz + w228.zzzz);',
    'float4 e229 = mad(w228.wwww, w225.zzzz, w228.zzzz);')
$body = $body.Replace(
    'float4 e232 = (w230 * DL5 + v0);',
    'float4 e232 = mad(w230, DL5, v0);')
$body = $body.Replace(
    'float4 w235 = (w230.xyxy * DL7.yxzx + v0.xyxy);',
    'float4 w235 = mad(w230.xyxy, DL7.yxzx, v0.xyxy);')
$body = $body.Replace(
    'float4 e303 = (w302.xxxx * w299.zzzz + w302.zzzz);',
    'float4 e303 = mad(w302.xxxx, w299.zzzz, w302.zzzz);')
$body = $body.Replace(
    'float4 e323 = (w322 * DL4.zzzz + DL4.wwww);',
    'float4 e323 = mad(w322, DL4.zzzz, DL4.wwww);')

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
