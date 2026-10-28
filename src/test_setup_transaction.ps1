# Exercise setup orchestration without elevation or real registry writes.
param([string]$SetupPath=(Join-Path $PSScriptRoot 'setup.ps1'))
$ErrorActionPreference='Stop'
$fixtureRoot=Join-Path $PSScriptRoot ('build\setup-transaction-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixtureRoot
$script:registrations=@{}
$script:FailAfterRenderer=$false
$script:FailAfterLegacy=$false
function Require([bool]$value,[string]$message){if(!$value){throw $message}}
function Get-Process { return $null }
function Test-Path {
    param([string]$LiteralPath,[string]$Path)
    $candidate=if($LiteralPath){$LiteralPath}else{$Path}
    if($candidate.StartsWith('Registry::')){return $true}
    return Microsoft.PowerShell.Management\Test-Path -LiteralPath $candidate
}
$replacements=@{
'RegisteredPath'=@'
function RegisteredPath([string]$id,[string]$hive='HKEY_LOCAL_MACHINE') {
    return $script:registrations["$hive|$id"]
}
'@
'SaveRegistrationSnapshot'=@'
function SaveRegistrationSnapshot([string]$directory) {
    $null=New-Item -ItemType Directory -Path $directory -Force
    foreach($component in (@($taskComponents)+@{file=$taskFilterAx;ids=$taskFilterIds})){foreach($id in $component.ids){
        $key="HKEY_LOCAL_MACHINE|$id"
        @{path="HKLM\SOFTWARE\Classes\CLSID\$id";key=$key;value=$script:registrations[$key];existed=$script:registrations.ContainsKey($key);file='mock'}
    }}
}
'@
'RestoreBackup'=@'
function RestoreBackup($state,[string]$directory=(Split-Path $taskManifest),[switch]$IncludeLegacy) {
    foreach($entry in $state.entries){if($entry.key){
        if(!$IncludeLegacy -and @($taskFilterIds | Where-Object {$entry.key -like "*$_*"}).Count){continue}
        if($entry.existed){$script:registrations[$entry.key]=$entry.value}else{$script:registrations.Remove($entry.key)}
    }}
}
'@
'RemoveLegacyFilter'=@'
function RemoveLegacyFilter {
    foreach($hive in @('HKEY_LOCAL_MACHINE','HKEY_CURRENT_USER')){foreach($id in $taskFilterIds){$script:registrations.Remove("$hive|$id")}}
    if($script:FailAfterLegacy){throw 'Injected post-migration failure'}
}
'@
'Register'=@'
function Register([bool]$remove) {
    $modules=@($taskComponents)
    if($remove){[array]::Reverse($modules)}
    $index=0
    foreach($component in $modules){
        foreach($id in $component.ids){
            $key="HKEY_LOCAL_MACHINE|$id"
            if($remove){$script:registrations.Remove($key)}else{$script:registrations[$key]=$component.file}
        }
        $index++
        if(!$remove -and $script:FailAfterRenderer){throw 'Injected renderer registration failure'}
    }
}
'@
}
try {
    $source=[IO.File]::ReadAllText($SetupPath)
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'Setup syntax errors'}
    $functions=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $replacements.ContainsKey($n.Name)},$true)
    foreach($function in @($functions | Sort-Object {$_.Extent.StartOffset} -Descending)){
        $source=$source.Substring(0,$function.Extent.StartOffset)+$replacements[$function.Name]+$source.Substring($function.Extent.EndOffset)
    }
    $source=$source -replace '(?m)^\s*\$taskAdmin=.*$', '$taskAdmin=$true'
    $source=$source.Replace('exit 1',"throw 'Expected fixture setup failure'").Replace('exit 0','return')
    $fixtureSetup=Join-Path $fixtureRoot 'setup.ps1'
    Set-Content -LiteralPath $fixtureSetup -Value $source
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'FrankyRenderer.ax') -Value 'fixture'
    $script:registrations['HKEY_LOCAL_MACHINE|unrelated']='untouched'
    $script:FailAfterRenderer=$true
    $failed=$false
    try {. $fixtureSetup -Action Install} catch {$failed=$true}
    Require $failed 'Partial installation must fail'
    Require ($script:registrations.Count -eq 1) 'Failed fresh install must roll back both components'
    $script:FailAfterRenderer=$false
    $script:registrations["HKEY_LOCAL_MACHINE|$($taskFilterIds[0])"]=$taskFilterAx
    . $fixtureSetup -Action Install
    Require ($script:registrations.Count -eq 3) 'Install must register only renderer/page and retire the old DV filter'
    $script:registrations["HKEY_LOCAL_MACHINE|$($taskFilterIds[0])"]=$taskFilterAx
    $expected=@{};foreach($key in $script:registrations.Keys){$expected[$key]=$script:registrations[$key]}
    $script:FailAfterLegacy=$true
    $failed=$false
    try {. $fixtureSetup -Action Install} catch {$failed=$true}
    Require $failed 'Failed reinstall must report failure'
    Require ($script:registrations.Count -eq $expected.Count) 'Failed reinstall must restore the working installation'
    foreach($key in $expected.Keys){Require ($script:registrations[$key] -eq $expected[$key]) 'Pre-attempt paths must survive failed reinstall'}
    $script:FailAfterRenderer=$false
    $script:FailAfterLegacy=$false
    . $fixtureSetup -Action Remove
    Require ($script:registrations.Count -eq 1 -and $script:registrations['HKEY_LOCAL_MACHINE|unrelated'] -eq 'untouched') 'Uninstall must preserve unrelated registrations'
    . $fixtureSetup -Action Install
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $taskManifest
    . $fixtureSetup -Action Remove
    Require ($script:registrations.Count -eq 1) 'Missing backup must not prevent owned uninstall'
    . $fixtureSetup -Action Remove
    Require ($script:registrations.Count -eq 1) 'Repeated uninstall without backup must remain safe'
    $script:registrations["HKEY_LOCAL_MACHINE|$($taskIds[0])"]='C:\OtherFranky\FrankyRenderer.ax'
    $failed=$false
    try {. $fixtureSetup -Action Remove} catch {$failed=$true}
    Require $failed 'Missing backup must not permit uninstalling another installation'
    Require ($script:registrations["HKEY_LOCAL_MACHINE|$($taskIds[0])"] -eq 'C:\OtherFranky\FrankyRenderer.ax') 'Foreign registration must remain untouched'
    Write-Output 'PASS setup transaction: install rollback, reinstall restore, uninstall with/without backup, repeated uninstall and foreign registration protection'
} finally {
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    $allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build'))+[IO.Path]::DirectorySeparatorChar
    if(!$resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe fixture cleanup path'}
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $resolved -Recurse -Force
}
