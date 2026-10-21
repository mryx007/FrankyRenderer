@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
for %%I in ("%~dp0..\MPCVideoRenderer") do set "MPC_SRC=%%~fI"
msbuild "%MPC_SRC%\external\BaseClasses.vcxproj" /nologo /m /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=%MPC_SRC%/" "/p:OutDir=%~dp0build\lib/" "/p:IntDir=%~dp0build\obj\base/"
