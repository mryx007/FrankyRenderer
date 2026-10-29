@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist build\track-restart mkdir build\track-restart
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /DPLACEBO_TEST_PRESENT_PIXELS /I . /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses /c Renderer\Backend.cpp /Fobuild\track-restart\Backend.obj
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /DPLACEBO_TEST_PRESENT_PIXELS /I . /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses /c Renderer\Filter.cpp /Fobuild\track-restart\Filter.obj
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\lavffmpeg\include /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses /c DolbyVision\Processor.cpp /Fobuild\track-restart\Processor.obj
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /DPLACEBO_TEST_PRESENT_PIXELS /I . /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses Tests\test_track_restart.cpp /Fobuild\track-restart\test.obj /Febuild\track-restart\test_track_restart.exe /link /LTCG build\track-restart\Filter.obj build\track-restart\Backend.obj build\track-restart\Processor.obj build\obj\ColorManagement.obj build\obj\PixelLayout.obj build\obj\MediaFormat.obj build\obj\MediaSampleSideData.obj build\obj\SideDataAllocator.obj build\obj\Settings.obj build\obj\Direct2DSettingsHost.obj build\obj\Direct2DSettingsPage.obj build\obj\Renderer.res build\lib\BaseClasses.lib build\lib\libplacebo.lib deps\lavffmpeg\lib\avformat-lav-63.lib deps\lavffmpeg\lib\avcodec-lav-63.lib deps\lavffmpeg\lib\avutil-lav-61.lib deps\lavffmpeg\lib\libdovi.lib d3dcompiler.lib d2d1.lib dwrite.lib comdlg32.lib strmiids.lib d3d11.lib dxgi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib winmm.lib uuid.lib
