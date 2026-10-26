# FrankyRenderer native setup ownership checks; GPL-3.0-or-later.
# Loads decision functions only; no installer or real registry writes.
$ErrorActionPreference='Stop'
$tokens=$null
$errors=$null
$source=Join-Path $PSScriptRoot 'setup.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors | Out-String)}
foreach($name in @('ReadInstallationState','AssertOwnership')) {
    $function=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if(!$function){throw "Missing setup function: $name"}
    Invoke-Expression $function.Extent.Text
}
function RegisteredPath([string]$id,[string]$hive='HKEY_LOCAL_MACHINE') {
    return $script:registrations["$hive|$id"]
}
function Require([bool]$condition,[string]$message) {if(!$condition){throw $message}}
$taskRoot=Join-Path $PSScriptRoot ('build\setup-test-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $taskRoot
$taskManifest=Join-Path $taskRoot 'state.json'
$taskAx=Join-Path $taskRoot 'FrankyRenderer.ax'
$taskIds=@('{B864E92A-1BDF-4DA1-9004-CBF467DF239A}','{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}')
$compat='{71F080AA-8661-4093-B15E-4F6903E77D0A}'
try {
    Require (!([IO.File]::ReadAllText($source).Contains($compat))) 'Native installer must not reference the MPCVR CLSID'
    $script:registrations=@{}
    $script:registrations["HKEY_LOCAL_MACHINE|$compat"]='C:\OriginalMPCVR\MpcVideoRenderer64.ax'
    AssertOwnership
    foreach($id in $taskIds){$script:registrations["HKEY_LOCAL_MACHINE|$id"]=$taskAx}
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
    Write-Output 'PASS native setup: dedicated CLSID, original MPCVR ignored, foreign Franky registration and backup protected'
} finally {
    Remove-Item -LiteralPath $taskManifest -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $taskRoot -ErrorAction SilentlyContinue
}
