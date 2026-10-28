@echo off
setlocal
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include test_paused_seek.cpp /Fobuild\obj\test_paused_seek.obj /Febuild\bin\test_paused_seek.exe /link strmiids.lib user32.lib ole32.lib oleaut32.lib uuid.lib
