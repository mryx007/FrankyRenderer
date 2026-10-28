@echo off
setlocal
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE test_renderer_variants.cpp /Fobuild\obj\test_renderer_variants.obj /Febuild\bin\test_renderer_variants.exe /link ole32.lib advapi32.lib strmiids.lib uuid.lib
