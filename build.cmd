@echo off
call "%~dp0src\Tools\build.cmd" %*
exit /b %errorlevel%
