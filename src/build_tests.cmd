@echo off
setlocal
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses test_filter.cpp /Fobuild\obj\test_filter.obj /Febuild\bin\test_filter.exe /link /LTCG build\lib\BaseClasses.lib strmiids.lib d3d11.lib user32.lib gdi32.lib ole32.lib oleaut32.lib winmm.lib uuid.lib
