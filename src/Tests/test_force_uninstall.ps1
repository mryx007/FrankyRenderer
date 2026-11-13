# Force-uninstall registry cleanup checks without touching the real registry.
param([string]$SetupPath=(Join-Path $PSScriptRoot '..\Tools\setup.ps1'))
$SourceRoot=Split-Path $PSScriptRoot -Parent
$ErrorActionPreference='Stop'
$taskIds=@(
    '{B864E92A-1BDF-4DA1-9004-CBF467DF239A}',
    '{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}'
)
$taskFilterIds=@('{40F090C8-E82B-4CE9-931B-A34760E6D49E}')
$source=[IO.File]::ReadAllText($SetupPath)
$tokens=$null
$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput(
    $source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors | Out-String)}
foreach($name in @('IsFrankyId','RemoveAllFrankyRegistrations')){
    $function=$ast.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $name
    },$true)
    if(!$function){throw "Missing setup function: $name"}
    Invoke-Expression $function.Extent.Text
}
function Require([bool]$condition,[string]$message){
    if(!$condition){throw $message}
}
$category='{083863F1-70DE-11D0-BD40-00A0C911CE86}'
$rendererId=$taskIds[0]
$unrelatedId='{11111111-2222-3333-4444-555555555555}'
$unrelatedCategory="HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$category\Instance\Unrelated"
$frankyCategory="HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$category\Instance\UnaliasedFranky"
$script:existing=New-Object 'System.Collections.Generic.HashSet[string]' (
    [StringComparer]::OrdinalIgnoreCase
)
$script:removed=New-Object 'System.Collections.Generic.List[string]'
$script:children=@{}
$hklmCategory="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$category\Instance"
$hklmRenderer="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$rendererId"
$hkcuRenderer="Registry::HKEY_CURRENT_USER\SOFTWARE\Classes\CLSID\$rendererId"
$wowFilter="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\WOW6432Node\CLSID\$($taskFilterIds[0])"
$unrelated="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$unrelatedId"
foreach($path in @(
    $hklmCategory,$hklmRenderer,$hkcuRenderer,$wowFilter,$unrelated,
    ('Registry::'+$frankyCategory),('Registry::'+$unrelatedCategory)
)){[void]$script:existing.Add($path)}
$frankyChild=[pscustomobject]@{Name=$frankyCategory;Clsid=$rendererId}
$frankyChild | Add-Member ScriptMethod GetValue {
    param($name)
    if($name -eq 'CLSID'){return $this.Clsid}
    return $null
}
$unrelatedChild=[pscustomobject]@{Name=$unrelatedCategory;Clsid=$unrelatedId}
$unrelatedChild | Add-Member ScriptMethod GetValue {
    param($name)
    if($name -eq 'CLSID'){return $this.Clsid}
    return $null
}
$script:children[$hklmCategory]=@($frankyChild,$unrelatedChild)
function Test-Path {
    [CmdletBinding()]
    param([string]$LiteralPath,[string]$Path)
    $candidate=if($LiteralPath){$LiteralPath}else{$Path}
    return $script:existing.Contains($candidate)
}
function Get-ChildItem {
    [CmdletBinding()]
    param([string]$LiteralPath)
    if($script:children.ContainsKey($LiteralPath)){
        return @($script:children[$LiteralPath])
    }
    return @()
}
function Remove-Item {
    [CmdletBinding()]
    param([string]$LiteralPath,[switch]$Recurse,[switch]$Force)
    [void]$script:removed.Add($LiteralPath)
    [void]$script:existing.Remove($LiteralPath)
}
$removed=RemoveAllFrankyRegistrations
Require ($removed -ge 4) (
    "Known registry views and category aliases must be removed; " +
    "removed=$removed paths=$($script:removed -join ';')")
Require (!$script:existing.Contains($hklmRenderer)) 'Machine renderer registration must be removed'
Require (!$script:existing.Contains($hkcuRenderer)) 'User renderer registration must be removed'
Require (!$script:existing.Contains($wowFilter)) '32-bit legacy filter registration must be removed'
Require (!$script:existing.Contains('Registry::'+$frankyCategory)) 'Category alias must be removed'
Require ($script:existing.Contains($unrelated)) 'Unrelated CLSID must remain'
Require ($script:existing.Contains('Registry::'+$unrelatedCategory)) 'Unrelated category must remain'
$cmd=(Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Tools\ForceUninstall.cmd') -Raw)
Require ($cmd.Contains('-Action ForceRemove')) 'ForceUninstall.cmd must call ForceRemove'
Write-Output 'PASS force uninstall: all known registry views removed, unrelated registrations kept'
