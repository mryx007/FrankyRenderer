@echo off
setlocal
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE test_subtitle_lifetime.cpp /Fobuild\obj\test_subtitle_lifetime.obj /Febuild\bin\test_subtitle_lifetime.exe /link ole32.lib strmiids.lib uuid.lib
