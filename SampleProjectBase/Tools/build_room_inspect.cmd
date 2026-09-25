@echo off
call "D:\VSStudio2026\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /MD /I. Tools\inspect_room_import.cpp /FoTools\inspect_room_import.obj /FeTools\inspect_room_import.exe /link assimp\x64\Release\assimp-vc142-mt.lib
if errorlevel 1 exit /b 1
set "PATH=%CD%;%PATH%"
Tools\inspect_room_import.exe
