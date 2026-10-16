@echo off
call "D:\VSStudio2026\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /MT /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I. /IGameData /Iimgui /FoTools\ /FeTools\pbr_material_probe.exe Tools\pbr_material_probe.cpp LitRender.cpp GameData\PropMaterials.cpp Model.cpp Shader.cpp Texture.cpp TextureCache.cpp MeshBuffer.cpp CameraBase.cpp imgui\imgui.cpp imgui\imgui_widgets.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp /link /LIBPATH:. user32.lib >Tools\pbr_material_probe_build.log 2>&1
exit /b %errorlevel%
