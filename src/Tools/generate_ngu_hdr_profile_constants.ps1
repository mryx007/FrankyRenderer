param(
    [Parameter(Mandatory = $true)][string] $CaptureRoot,
    [Parameter(Mandatory = $true)][ValidateSet(720, 1080)][int] $Geometry,
    [Parameter(Mandatory = $true)][string] $Suffix, [string] $OutputRoot = (Join - Path $PSScriptRoot '..\Shaders\Ngu'))

    $ErrorActionPreference = 'Stop'

                             function Find
                             - Snapshot([string] $Shader)
{
    $matches = @(Get - ChildItem - LiteralPath $CaptureRoot - File - Filter "const_snapshot_*_$Shader.f32" |
                 Sort -
                     Object {
                         $match = [regex] ::Match($_.Name, '^const_snapshot_draw_(\d+)_') if ($match.Success)
                         {
                             [int] $match.Groups[1].Value
                         }
                         else
                         {
                             [int] ::MaxValue
                         }
                     }) if ($matches.Count - eq 0){throw "missing $Shader constant snapshot in $CaptureRoot"} return,
    ([IO.File] ::ReadAllBytes($matches[0].FullName))
}

function Join - Rows([byte[]] $Bytes, [int[]] $Rows)
{
    $result = [byte[]] ::new ($Rows.Count * 16) for ($i = 0; $i - lt $Rows.Count; ++$i)
    {
        [Array] ::Copy($Bytes, $Rows[$i] * 16, $result, $i * 16, 16)
    }
    return $result
}

function Write -
        Asset([string] $Name, [byte[]] $Bytes){[IO.File] ::WriteAllBytes((Join - Path $OutputRoot $Name), $Bytes)}

        New
        - Item - ItemType Directory - Force - Path $OutputRoot
    | Out - Null $main = Find - Snapshot
                         'PS_4412_c39b49cf' $base = Find - Snapshot 'PS_4576_b7830945' $prefix = if (
                                                        $Geometry - eq 1080){'superres_1080_asm'} else {'superres_asm'}

if ($Geometry - eq 1080)
{
    Write - Asset "$($prefix)_main_constants_$Suffix.bin"($main[0..31])
}

else {$floats = [float[]] ::new ($main.Length / 4)[Buffer] ::BlockCopy($main, 0, $floats, 0, $main.Length) $bytes =
          [ byte[] ] ::new (8 * 4)[Buffer] ::BlockCopy($floats, 0, $bytes, 0, $bytes.Length) Write - Asset
          "$($prefix)_main_constants_$Suffix.bin" $bytes}

$selected = Join - Rows $base @(0, 1, 4, 5, 6, 7) Write - Asset "$($prefix)_base_constants_$Suffix.bin" $selected
