param([ValidateSet('Install','Remove','Check')][string]$Action='Install',[switch]$Quiet)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskAx=[IO.Path]::GetFullPath((Join-Path $taskRoot 'PlaceboRenderer.ax'))
$taskIds=@('{71F080AA-8661-4093-B15E-4F6903E77D0A}','{B864E92A-1BDF-4DA1-9004-CBF467DF239A}','{6B35EF45-9C5E-40B4-8EAC-DE2C5161174D}','{7D274B8B-7C98-43D7-B448-782CBA0C6A33}','{5F6FAAC4-5B83-4DBE-AB84-C04A1C5753A6}','{72EB4D7A-7D02-4080-A85E-6E65CB4223D1}','{AF0D638A-478B-43B0-81D0-E7B0FA46A3E1}','{4C64B3B7-5485-4230-AE73-84C7907E4F3B}','{10DD7ADE-F6D7-4A4D-A1E9-5D88613A4EF2}','{60585D47-776F-4B48-A357-242D8B8A3F42}','{C5B9232E-DD14-43BB-B8D9-917552C0A768}','{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}')
$taskManifest=Join-Path $taskRoot 'registration-backup\state.json'
function RegisteredPath([string]$id,[string]$hive='HKEY_LOCAL_MACHINE') {
    $key="Registry::$hive\SOFTWARE\Classes\CLSID\$id\InprocServer32"
    if(Test-Path -LiteralPath $key){return (Get-Item -LiteralPath $key).GetValue('')}
    return $null
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
        $taskArgs='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Action '+$Action
        if($Quiet){$taskArgs+=' -Quiet'}
        $p=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $taskArgs -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        exit $p.ExitCode
    }
    # A running player can retain the old DLL even after registration changes.
    $taskPlayers=Get-Process -Name PotPlayerMini64,PotPlayer64 -ErrorAction SilentlyContinue
    if($taskPlayers){throw 'Please close PotPlayer first. No changes were made.'}
    if(Test-Path -LiteralPath $taskManifest){
        $state=Get-Content -LiteralPath $taskManifest -Raw | ConvertFrom-Json
        if(![string]::Equals($state.renderer,$taskAx,[StringComparison]::OrdinalIgnoreCase)){throw 'The registration backup belongs to another directory.'}
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
        try {Register $false;foreach($id in $taskIds){if(!(RegisteredPath $id) -or ![string]::Equals((RegisteredPath $id),$taskAx,[StringComparison]::OrdinalIgnoreCase)){throw "Registered path does not match: $id"}}}
        catch {$taskFailure=$_;try {Register $true;RestoreBackup $state}catch {throw "Installation and rollback failed: $taskFailure / $_"};throw $taskFailure}
        $message='Installed. Start PotPlayer normally and select "MPC Video Renderer" under Video Renderer. The active filter is named "FrankyRenderer". Keep this directory in its permanent location.'
    } else {Register $true;RestoreBackup $state;$message='Renderer removed. PotPlayer can now use another renderer.'}
    @{action=$Action;success=$true;renderer=$taskAx;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $taskManifest) 'result.json') -Encoding UTF8
    if($Quiet){Write-Output $message}else{Add-Type -AssemblyName System.Windows.Forms;[Windows.Forms.MessageBox]::Show($message,'FrankyRenderer') | Out-Null}
} catch {
    if($Quiet){Write-Error $_.Exception.Message -ErrorAction Continue}else{Add-Type -AssemblyName System.Windows.Forms;[Windows.Forms.MessageBox]::Show($_.Exception.Message,'FrankyRenderer - Error','OK','Error') | Out-Null}
    exit 1
}
