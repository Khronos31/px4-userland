@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d %~dp0
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE bon-ccprobe.cpp /Fe:bon-ccprobe.exe /link user32.lib || exit /b 1
echo BUILD-OK
