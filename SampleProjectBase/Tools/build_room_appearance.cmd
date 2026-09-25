@echo off
call "D:\VSStudio2026\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /MT /O2 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I. /FoTools\ /FeTools\room_appearance_probe.exe Tools\room_appearance_probe.cpp Model.cpp Shader.cpp Texture.cpp TextureCache.cpp MeshBuffer.cpp CameraBase.cpp /link /LIBPATH:. user32.lib >Tools\room_appearance_probe_build.log 2>&1
exit /b %errorlevel%

