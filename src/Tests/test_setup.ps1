# FrankyRenderer native setup ownership checks; GPL-3.0-or-later.
# Loads decision functions only; no installer or real registry writes.
param([string]$SetupPath=(Join-Path $PSScriptRoot '..\Tools\setup.ps1'),[ValidateSet('native','potplayer')][string]$Variant='native')
$SourceRoot=Split-Path $PSScriptRoot -Parent
$ErrorActionPreference='Stop'
$tokens=$null
$errors=$null
$source=$SetupPath
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors | Out-String)}
foreach($name in @('ReadInstallationState','AssertOwnership','RegistrationPaths')) {
    $function=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if(!$function){throw "Missing setup function: $name"}
    Invoke-Expression $function.Extent.Text
}
function RegisteredPath([string]$id,[string]$hive='HKEY_LOCAL_MACHINE') {
    return $script:registrations["$hive|$id"]
}
function Require([bool]$condition,[string]$message) {if(!$condition){throw $message}}
$taskRoot=Join-Path $SourceRoot ('build\setup-test-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $taskRoot
$taskManifest=Join-Path $taskRoot 'state.json'
$taskAx=Join-Path $taskRoot 'FrankyRenderer.ax'
if($Variant -eq 'potplayer'){
    $taskIds=@('{71F080AA-8661-4093-B15E-4F6903E77D0A}','{98F20BC7-42A9-4FBA-B171-39CA08651FCA}')
    $compat='{B864E92A-1BDF-4DA1-9004-CBF467DF239A}'
}else{
    $taskIds=@('{B864E92A-1BDF-4DA1-9004-CBF467DF239A}','{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}')
    $compat='{71F080AA-8661-4093-B15E-4F6903E77D0A}'
}
$taskFilterAx=Join-Path $taskRoot 'DolbyVisionFilter.ax'
$taskFilterIds=@($(if($Variant -eq 'potplayer'){'{6A4027EB-542F-450D-A3E0-D7899D2B54C4}'}else{'{40F090C8-E82B-4CE9-931B-A34760E6D49E}'}))
$taskComponents=@(@{file=$taskAx;ids=$taskIds})
try {
    Require (!([IO.File]::ReadAllText($source).Contains($compat))) 'Installer must not reference the other renderer identity'
    foreach($id in $taskIds){Require ([IO.File]::ReadAllText($source).Contains($id)) 'Installer must contain its own variant IDs'}
    $script:registrations=@{}
    $script:registrations["HKEY_LOCAL_MACHINE|$compat"]='C:\OriginalMPCVR\MpcVideoRenderer64.ax'
    AssertOwnership
    foreach($component in $taskComponents){foreach($id in $component.ids){$script:registrations["HKEY_LOCAL_MACHINE|$id"]=$component.file}}
    AssertOwnership
    @{renderer=$taskAx;entries=@()} | ConvertTo-Json | Set-Content -LiteralPath $taskManifest
    Require ((ReadInstallationState).renderer -eq $taskAx) 'Current native backup must be retained'
    $script:registrations["HKEY_LOCAL_MACHINE|$($taskIds[0])"]='C:\OtherFranky\FrankyRenderer.ax'
    $rejected=$false
    try {AssertOwnership} catch {$rejected=$true}
    Require $rejected 'Another FrankyRenderer installation must not be overwritten'
    @{renderer='C:\OtherFranky\FrankyRenderer.ax'} | ConvertTo-Json | Set-Content -LiteralPath $taskManifest
    $rejected=$false
    try {$null=ReadInstallationState} catch {$rejected=$true}
    Require $rejected 'Foreign installation backup must not be adopted'
    $script:registrations["HKEY_LOCAL_MACHINE|$($taskIds[0])"]=$taskAx
    $script:registrations["HKEY_CURRENT_USER|$($taskFilterIds[0])"]='C:\OtherFilter\DolbyVisionFilter.ax'
    $rejected=$false
    try {AssertOwnership} catch {$rejected=$true}
    Require $rejected 'A user-level foreign DV filter registration must not be overwritten'
    $script:registrations.Remove("HKEY_CURRENT_USER|$($taskFilterIds[0])")
    Require (@(RegistrationPaths).Count -eq 7) 'Backup must include active renderer entries and both legacy-filter registry views'
    Write-Output "PASS $Variant setup: correct IDs, other variant ignored, foreign registration and backup protected"
} finally {
    Remove-Item -LiteralPath $taskManifest -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $taskRoot -ErrorAction SilentlyContinue
}
