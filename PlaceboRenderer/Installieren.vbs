Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
script = fs.BuildPath(fs.GetParentFolderName(WScript.ScriptFullName), "setup.ps1")
shell.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & script & """ -Action Install", 0, False
