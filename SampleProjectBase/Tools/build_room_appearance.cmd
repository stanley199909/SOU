@echo off
call "D:\VSStudio2026\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /MT /O2 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I. /IEngine /IRendering /IGameData /IScenes /FoTools\ /FeTools\room_appearance_probe.exe Tools\room_appearance_probe.cpp Engine\Model.cpp Engine\Shader.cpp Engine\Texture.cpp Engine\TextureCache.cpp Engine\MeshBuffer.cpp Engine\CameraBase.cpp /link /LIBPATH:. user32.lib >Tools\room_appearance_probe_build.log 2>&1
exit /b %errorlevel%

