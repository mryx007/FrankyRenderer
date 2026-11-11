param(
    [string]$InputHlsl = (
        'E:\OpenMadVR\artifacts_20261031\hlsl_all\SSimRT.hlsl'
    ),
    [string]$OutputPath = (
        Join-Path $PSScriptRoot (
            '..\Shaders\Ngu\luma_4x720_tail_ssim_rt.hlsl')
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
  const float2 v0 = float2(
      UvLut.Load(int3(pixel.x, 0, 0)).x,
      UvLut.Load(int3(pixel.y, 1, 0)).y);
'@)
$body = $body.Replace(
    'tex2D(SourceSampler, ',
    'SourceTex.Sample(SourceSampler, ')
$body = $body.Replace(
    'float4 e46 = (1.0 / w45.yyyy);',
    'float4 e46 = rcp(w45.yyyy);')
$body = $body.Replace(
    'float4 e50 = (1.0 / w49.zzzz);',
    'float4 e50 = rcp(w49.zzzz);')
$body = $body.Replace(
    'float4 w2 = (U0.xyxy * w1.xyzy + v0.xyxy);',
    'float4 w2 = mad(U0.xyxy, w1.xyzy, v0.xyxy);')
$body = $body.Replace(
    'float4 w5 = (U0.xyxy * w1.yxzx + v0.xyxy);',
    'float4 w5 = mad(U0.xyxy, w1.yxzx, v0.xyxy);')
$body = $body.Replace(
    'float4 w9 = (U0.xyxy * w1.yzxz + v0.xyxy);',
    'float4 w9 = mad(U0.xyxy, w1.yzxz, v0.xyxy);')
$body = $body.Replace(
    'float4 e16 = (w15.xyyx * DL1.xxxx + w14);',
    'float4 e16 = mad(w15.xyyx, DL1.xxxx, w14);')
$body = $body.Replace(
    'float4 e24 = (w23 * DL1.yyyy + w16.zyww);',
    'float4 e24 = mad(w23, DL1.yyyy, w16.zyww);')
$body = $body.Replace(
    'float4 e28 = (w27.xxxx * w27.xxxx + w26.wwww);',
    'float4 e28 = mad(w27.xxxx, w27.xxxx, w26.wwww);')
$body = $body.Replace(
    'float4 e30 = (w29.xxxx * w29.xxxx + w28.wwww);',
    'float4 e30 = mad(w29.xxxx, w29.xxxx, w28.wwww);')
$body = $body.Replace(
    'float4 e32 = (w31.xxxx * w31.xxxx + w30.wwww);',
    'float4 e32 = mad(w31.xxxx, w31.xxxx, w30.wwww);')
$body = $body.Replace(
    'float4 e36 = (w35.xxxx * DL1.xxxx + w33.wwww);',
    'float4 e36 = mad(w35.xxxx, DL1.xxxx, w33.wwww);')
$body = $body.Replace(
    'float4 e40 = (w39.zzzz * w39.zzzz + w39.xxxx);',
    'float4 e40 = mad(w39.zzzz, w39.zzzz, w39.xxxx);')
$body = $body.Replace(
    'float4 e42 = (w41.yyyy * w41.yyyy + w40.xxxx);',
    'float4 e42 = mad(w41.yyyy, w41.yyyy, w40.xxxx);')
$body = $body.Replace(
    'float4 e44 = (w43.xxxx * w43.xxxx + w42.yyyy);',
    'float4 e44 = mad(w43.xxxx, w43.xxxx, w42.yyyy);')
$body = $body.Replace(
    'float4 e45 = (w44.yyyy * DL1.yyyy + w44.wwww);',
    'float4 e45 = mad(w44.yyyy, DL1.yyyy, w44.wwww);')
$body = $body.Replace(
    'float4 e48 = (w47.zzzz * w47.wwww + DL2.zzzz);',
    'float4 e48 = mad(w47.zzzz, w47.wwww, DL2.zzzz);')

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
