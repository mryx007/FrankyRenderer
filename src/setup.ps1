param([ValidateSet('Install','Remove','Check')][string]$Action='Install',[switch]$Quiet,[string]$MessageFile)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskAx=[IO.Path]::GetFullPath((Join-Path $taskRoot 'FrankyRenderer.ax'))
$taskIds=@('{B864E92A-1BDF-4DA1-9004-CBF467DF239A}','{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}')
$taskActiveIds=$taskIds
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
    return $state
}
function AssertOwnership {
    foreach($id in $taskIds){foreach($hive in @('HKEY_LOCAL_MACHINE','HKEY_CURRENT_USER')){
        $path=RegisteredPath $id $hive
        if($path -and ![string]::Equals([IO.Path]::GetFullPath($path),$taskAx,[StringComparison]::OrdinalIgnoreCase)){
            throw "Another renderer registration exists: $id -> $path. No changes were made."
        }
    }}
}
function Register([bool]$remove) {
    $taskArgs=if($remove){'/s /u "'+$taskAx+'"'}else{'/s "'+$taskAx+'"'}
    $p=Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList $taskArgs -WindowStyle Hidden -Wait -PassThru
    if($p.ExitCode -ne 0){throw "Registration failed. regsvr32 exit code: $($p.ExitCode)"}
}
function RestoreBackup($state) {
    foreach($entry in $state.entries){if($entry.existed){
        $file=Join-Path (Split-Path $taskManifest) $entry.file
        & "$env:SystemRoot\System32\reg.exe" import $file /reg:64 | Out-Null
        if($LASTEXITCODE -ne 0){throw "Could not restore the registration backup: $file"}
    }}
}
try {
    if(!(Test-Path -LiteralPath $taskAx)){throw "Renderer file is missing: $taskAx"}
    AssertOwnership
    if($Action -eq 'Check') {
        foreach($id in $taskIds){[pscustomobject]@{CLSID=$id;Machine=(RegisteredPath $id);User=(RegisteredPath $id 'HKEY_CURRENT_USER')}}
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
    if(Test-Path -LiteralPath $taskManifest){
        $state=ReadInstallationState
    } elseif($Action -eq 'Remove') {throw 'No installation backup was found. No changes were made.'}
    else {
        if(RegisteredPath $taskIds[0]){throw 'The renderer is already registered, but its backup is missing. No changes were made.'}
        $null=New-Item -ItemType Directory -Path (Split-Path $taskManifest) -Force
        $paths=@();foreach($id in $taskIds){$paths+="HKLM\SOFTWARE\Classes\CLSID\$id"}
        $paths+="HKLM\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\$($taskIds[0])"
        $entries=@();$index=0
        foreach($path in $paths){
            $exists=Test-Path -LiteralPath ('Registry::'+$path.Replace('HKLM','HKEY_LOCAL_MACHINE'))
            $name="key-$index.reg";$index++
            if($exists){& "$env:SystemRoot\System32\reg.exe" export $path (Join-Path (Split-Path $taskManifest) $name) /y /reg:64 | Out-Null;if($LASTEXITCODE -ne 0){throw "Registration backup failed: $path"}}
            $entries+=@{path=$path;existed=$exists;file=$name}
        }
        $state=@{renderer=$taskAx;entries=$entries;created=(Get-Date).ToString('o')}
        $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskManifest -Encoding UTF8
    }
    if($Action -eq 'Install'){
        # Extend an existing installation snapshot for newly added page classes.
        # AssertOwnership already rejects registrations belonging to other files.
        foreach($id in $taskIds){
            $newPath="HKLM\SOFTWARE\Classes\CLSID\$id"
            if(!(@($state.entries) | Where-Object { $_.path -eq $newPath })){
                $state.entries=@($state.entries)+@{path=$newPath;existed=$false;file=''}
            }
        }
        $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskManifest -Encoding UTF8
        try {
            Register $false
            foreach ($id in $taskActiveIds) {
                if (!(RegisteredPath $id) -or ![string]::Equals((RegisteredPath $id), $taskAx, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Registered path does not match: $id"
                }
            }
            $taskCategoryKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\' + $taskIds[0]
            if (!(Test-Path -LiteralPath $taskCategoryKey)) {
                throw 'The renderer is missing from the DirectShow video filter category.'
            }
        }
        catch {$taskFailure=$_;try {Register $true;RestoreBackup $state}catch {throw "Installation and rollback failed: $taskFailure / $_"};throw $taskFailure}
        $message='Developer test build registered under the dedicated FrankyRenderer CLSID. Native player integration is required; the MPC Video Renderer registration is unchanged.'
    } else {Register $true;RestoreBackup $state;$message='Renderer removed. Your video player can now use another renderer.'}
    @{action=$Action;success=$true;renderer=$taskAx;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $taskManifest) 'result.json') -Encoding UTF8
    ShowSetupMessage $message
} catch {
    ShowSetupMessage $_.Exception.Message $true
    exit 1
}
