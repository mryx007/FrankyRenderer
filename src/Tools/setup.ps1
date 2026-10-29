param([ValidateSet('Install','Remove','Check')][string]$Action='Install',[switch]$Quiet,[string]$MessageFile)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskAx=[IO.Path]::GetFullPath((Join-Path $taskRoot 'FrankyRenderer.ax'))
$taskIds=@('{B864E92A-1BDF-4DA1-9004-CBF467DF239A}','{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}')
$taskFilterAx=[IO.Path]::GetFullPath((Join-Path $taskRoot 'DolbyVisionFilter.ax'))
$taskFilterIds=@('{40F090C8-E82B-4CE9-931B-A34760E6D49E}')
$taskComponents=@(@{file=$taskAx;ids=$taskIds})
# Old packages registered a separate DV filter. Retire only this variant's owned entry.
$taskManifest=Join-Path $taskRoot 'registration-backup\state.json'
function ShowSetupMessage([string]$text,[bool]$failed=$false) {
    if($MessageFile){Set-Content -LiteralPath $MessageFile -Value $text -Encoding UTF8}
    elseif($failed){Write-Host $text -ForegroundColor Red}
    else{Write-Output $text}
}
function RegisteredPath([string]$id,[string]$hive='HKEY_LOCAL_MACHINE') {
    $key="Registry::$hive\SOFTWARE\Classes\CLSID\$id\InprocServer32"
    if(Test-Path -LiteralPath $key){return (Get-Item -LiteralPath $key).GetValue('')}
    return $null
}
function ReadInstallationState {
    $state=Get-Content -LiteralPath $taskManifest -Raw | ConvertFrom-Json
    if(![string]::Equals($state.renderer,$taskAx,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'The registration backup belongs to another renderer installation.'
    }
    if($state.filter -and ![string]::Equals($state.filter,$taskFilterAx,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'The registration backup belongs to another DV filter installation.'
    }
    return $state
}
function AssertOwnership {
    $owned=@($taskComponents)+@{file=$taskFilterAx;ids=$taskFilterIds}
    foreach($component in $owned){foreach($id in $component.ids){foreach($hive in @('HKEY_LOCAL_MACHINE','HKEY_CURRENT_USER')){
        $path=RegisteredPath $id $hive
        if($path -and ![string]::Equals([IO.Path]::GetFullPath($path),$component.file,[StringComparison]::OrdinalIgnoreCase)){
            throw "Another registration exists: $id -> $path. No changes were made."
        }
    }}}
}
function RegistrationPaths {
    foreach($component in $taskComponents){
        foreach($id in $component.ids){"HKLM\SOFTWARE\Classes\CLSID\$id"}
        "HKLM\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\$($component.ids[0])"
    }
    foreach($hive in @('HKLM','HKCU')){foreach($id in $taskFilterIds){
        "$hive\SOFTWARE\Classes\CLSID\$id"
        "$hive\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\$id"
    }}
}
function RemoveLegacyFilter {
    foreach($hive in @('HKEY_LOCAL_MACHINE','HKEY_CURRENT_USER')){foreach($id in $taskFilterIds){
        $path=RegisteredPath $id $hive
        if($path -and ![string]::Equals([IO.Path]::GetFullPath($path),$taskFilterAx,[StringComparison]::OrdinalIgnoreCase)){
            throw "Another DV filter registration exists: $id -> $path"
        }
        foreach($key in @("Registry::$hive\SOFTWARE\Classes\CLSID\$id",
                         "Registry::$hive\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\$id")){
            if(Test-Path -LiteralPath $key){Remove-Item -LiteralPath $key -Recurse -Force}
        }
    }}
}
function Register([bool]$remove) {
    $modules=@($taskComponents)
    if($remove){[array]::Reverse($modules)}
    foreach($component in $modules){
        $taskArgs=if($remove){'/s /u "'+$component.file+'"'}else{'/s "'+$component.file+'"'}
        $p=Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList $taskArgs -WindowStyle Hidden -Wait -PassThru
        if($p.ExitCode -ne 0){throw "Registration failed for $($component.file). Exit code: $($p.ExitCode)"}
    }
}
function SaveRegistrationSnapshot([string]$directory) {
    $null=New-Item -ItemType Directory -Path $directory -Force
    $entries=@();$index=0
    foreach($path in @(RegistrationPaths)){
        $exists=Test-Path -LiteralPath ('Registry::'+$path.Replace('HKLM','HKEY_LOCAL_MACHINE').Replace('HKCU','HKEY_CURRENT_USER'))
        $name="key-$index.reg";$index++
        if($exists){
            & "$env:SystemRoot\System32\reg.exe" export $path (Join-Path $directory $name) /y /reg:64 | Out-Null
            if($LASTEXITCODE -ne 0){throw "Registration backup failed: $path"}
        }
        $entries+=@{path=$path;existed=$exists;file=$name}
    }
    return $entries
}
function RestoreBackup($state,[string]$directory=(Split-Path $taskManifest),[switch]$IncludeLegacy) {
    foreach($entry in $state.entries){
        $legacy=@($taskFilterIds | Where-Object {$entry.path -like "*$_*"}).Count -gt 0
        if($legacy -and !$IncludeLegacy){continue}
        if($entry.existed){
        $file=Join-Path $directory $entry.file
        & "$env:SystemRoot\System32\reg.exe" import $file /reg:64 | Out-Null
        if($LASTEXITCODE -ne 0){throw "Could not restore the registration backup: $file"}
    }}
}
try {
    foreach($component in $taskComponents){if(!(Test-Path -LiteralPath $component.file)){throw "Required file is missing: $($component.file)"}}
    AssertOwnership
    if($Action -eq 'Check') {
        foreach($component in $taskComponents){foreach($id in $component.ids){[pscustomobject]@{CLSID=$id;Machine=(RegisteredPath $id);User=(RegisteredPath $id 'HKEY_CURRENT_USER')}}}
        exit 0
    }
    $taskIdentity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $taskAdmin=([Security.Principal.WindowsPrincipal]$taskIdentity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if(!$taskAdmin){
        $taskMessagePath=[IO.Path]::GetTempFileName()
        try {
            $taskArgs='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Action '+$Action+' -MessageFile "'+$taskMessagePath+'"'
            $p=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $taskArgs -Verb RunAs -WindowStyle Hidden -Wait -PassThru
            $taskMessage=Get-Content -LiteralPath $taskMessagePath -Raw
            if($taskMessage){ShowSetupMessage $taskMessage.TrimEnd() ($p.ExitCode -ne 0)}
            elseif($p.ExitCode -ne 0){ShowSetupMessage "Setup failed. Exit code: $($p.ExitCode)" $true}
            exit $p.ExitCode
        } finally {Remove-Item -LiteralPath $taskMessagePath -ErrorAction SilentlyContinue}
    }
    # A running player can retain the old DLL even after registration changes.
    $taskPlayers=Get-Process -Name PotPlayerMini64,PotPlayer64,mpc-be64,mpc-hc64 -ErrorAction SilentlyContinue
    if($taskPlayers){throw 'Please close PotPlayer, MPC-BE and MPC-HC first. No changes were made.'}
    $state=$null
    if(Test-Path -LiteralPath $taskManifest){
        $state=ReadInstallationState
    } elseif($Action -eq 'Install') {
        foreach($component in $taskComponents){if(RegisteredPath $component.ids[0]){throw 'A component is already registered, but its backup is missing. No changes were made.'}}
        $entries=@(SaveRegistrationSnapshot (Split-Path $taskManifest))
        $state=@{renderer=$taskAx;entries=$entries;created=(Get-Date).ToString('o')}
        $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskManifest -Encoding UTF8
    }
    if($Action -eq 'Install'){
        # Extend an existing installation snapshot for newly added page classes.
        # AssertOwnership already rejects registrations belonging to other files.
        foreach($newPath in @(RegistrationPaths)){
            if(!(@($state.entries) | Where-Object { $_.path -eq $newPath })){
                $state.entries=@($state.entries)+@{path=$newPath;existed=$false;file=''}
            }
        }
        $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskManifest -Encoding UTF8
        # Keep the pre-attempt registrations too: a failed reinstall must
        # restore the working installation, not only the original empty state.
        $rollbackDirectory=Join-Path (Split-Path $taskManifest) 'rollback'
        $rollback=@{entries=@(SaveRegistrationSnapshot $rollbackDirectory)}
        try {
            Register $false
            RemoveLegacyFilter
            foreach($component in $taskComponents){
                foreach($id in $component.ids){
                    if(!(RegisteredPath $id) -or ![string]::Equals((RegisteredPath $id),$component.file,[StringComparison]::OrdinalIgnoreCase)){
                        throw "Registered path does not match: $id"
                    }
                }
                $category='Registry::HKEY_CLASSES_ROOT\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\'+$component.ids[0]
                if(!(Test-Path -LiteralPath $category)){throw 'A component is missing from the DirectShow filter category.'}
            }
        }
        catch {$taskFailure=$_;try {Register $true;RestoreBackup $rollback $rollbackDirectory -IncludeLegacy}catch {throw "Installation and rollback failed: $taskFailure / $_"};throw $taskFailure}
        $message='FrankyRenderer installed for MPC-BE.
Select "FrankyRenderer" as the video renderer.
Dolby Vision Profile 7 (FEL/MEL) is detected automatically.'
    } else {
        # Ownership was checked above. A missing snapshot must not prevent
        # unregistering this installation, but there is nothing to restore.
        Register $true
        RemoveLegacyFilter
        if($state){RestoreBackup $state}
        $message='FrankyRenderer removed. Your settings were kept.'
    }
    $null=New-Item -ItemType Directory -Path (Split-Path $taskManifest) -Force
    @{action=$Action;success=$true;renderer=$taskAx;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $taskManifest) 'result.json') -Encoding UTF8
    ShowSetupMessage $message
} catch {
    ShowSetupMessage $_.Exception.Message $true
    exit 1
}
