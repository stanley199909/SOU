# Build the Release exe and pack a standalone FORGE folder that runs without Visual Studio.
#
# Usage (PowerShell):   powershell -ExecutionPolicy Bypass -File Tools\PackRelease.ps1
#   -NoBuild     skip the MSBuild step (pack the exe that is already built)
#   -NoShortcut  do not create the desktop shortcut
#
# Output:  <folder above the git repo>\Build\FORGE\   (outside git, so nothing to ignore)
#            FORGE.exe                 (the Release exe, icon embedded via app.rc)
#            assimp-vc142-mt.dll       (model loader, needed at run time)
#            Assets\                   (only what the game loads; see the exclusions below)
#            CREDITS.txt
#          + a "FORGE" shortcut on the desktop (working directory = the FORGE folder,
#            because the game reads "Assets/..." relative to the working directory).
#
# Kept ASCII-only on purpose (Windows PowerShell 5.1 reads scripts in the system code page).
param([switch]$NoBuild, [switch]$NoShortcut)
$ErrorActionPreference = 'Stop'

$proj    = Split-Path $PSScriptRoot -Parent          # ...\SampleProjectBase
$solDir  = Split-Path $proj -Parent                  # ...\SPGYSampleBase (solution folder)
$repo    = Split-Path $solDir -Parent                # folder ABOVE the git repo: Build\ lands outside git
$sln     = Join-Path $solDir 'SampleProjectBase.sln'
$msbuild = 'D:\VSStudio2026\MSBuild\Current\Bin\MSBuild.exe'
$exe     = Join-Path $solDir 'x64\Release\SampleProjectBase.exe'
$dist    = Join-Path $repo 'Build\FORGE'

# 1) Build Release
if (-not $NoBuild) {
    & $msbuild $sln /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /v:m /clp:ErrorsOnly
    if ($LASTEXITCODE -ne 0) { throw "Release build failed (MSBuild exit $LASTEXITCODE)" }
}
if (-not (Test-Path $exe)) { throw "Release exe not found: $exe" }

# 2) Fresh output folder
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null

Copy-Item $exe (Join-Path $dist 'FORGE.exe')
Copy-Item (Join-Path $proj 'assimp-vc142-mt.dll') $dist
Copy-Item (Join-Path $proj 'forge.ico') $dist   # the shortcut points at this file (see 4)
$credits = Join-Path $solDir 'CREDITS.txt'   # in the git repo (solution folder)
if (Test-Path $credits) { Copy-Item $credits $dist }

# 3) Assets. robocopy exit codes 0..7 = success, 8+ = failure.
function Copy-Tree($src, $dst, $extra) {
    $rcArgs = @($src, $dst, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP') + $extra
    & robocopy @rcArgs | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $src" }
}
$assets = Join-Path $proj 'Assets'
$out    = Join-Path $dist 'Assets'
# Source files / tools the game never loads at run time:
#   .blend/.psd (source art), .svg (icon sources, the game uses .png), .py (tools),
#   .mp3/.m4a (original downloads, the game plays the converted .wav), video/ and
#   debug_detail/ (not read by the game), imgui.ini/debug.txt (per-machine state).
$skipFiles = @('*.blend', '*.blend1', '*.psd', '*.svg', '*.py', '*.mp3', '*.m4a', '*.zip', 'imgui.ini', 'debug.txt')
Copy-Tree $assets $out (@('/XD', (Join-Path $assets 'MM_Blacksmith_Pack'), (Join-Path $assets 'video'), (Join-Path $assets 'debug_detail'), '/XF') + $skipFiles)
# The blacksmith pack: the engine uses only its BaseColor maps (Normal/ORM/Mask ~360 MB are unused).
#   Only inside this pack: the cottage materials DO use their normal maps, so the filter is not global.
Copy-Tree (Join-Path $assets 'MM_Blacksmith_Pack') (Join-Path $out 'MM_Blacksmith_Pack') (@('/XF', '*Normal*', '*_ORM*', '*_Mask*') + $skipFiles)

# 4) Desktop shortcut
if (-not $NoShortcut) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $shell   = New-Object -ComObject WScript.Shell
    $lnk     = $shell.CreateShortcut((Join-Path $desktop 'FORGE.lnk'))
    $lnk.TargetPath       = Join-Path $dist 'FORGE.exe'
    $lnk.WorkingDirectory = $dist
    # Icon from the .ico file, not from the exe: Windows caches a shortcut's icon by its source path,
    # so after the exe's embedded icon changed, an exe-based shortcut kept showing the old cached one.
    $lnk.IconLocation     = (Join-Path $dist 'forge.ico') + ',0'
    $lnk.Save()
}

$mb = [math]::Round(((Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "Packed: $dist  ($mb MB)"
