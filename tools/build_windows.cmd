@echo off
rem Build the ISS DELUXE'd Windows final build (launcher + online) in build-win:
rem   tools\build_windows.cmd
rem   "C:\Program Files\Git\bin\sh.exe" tools/make_windows_zip.sh
rem Needs Visual Studio 2022 (or Build Tools) with "C++ Clang tools" (clang-cl,
rem matching the clang the AppImage uses), Git, Python 3, and the SDL2 VC
rem development package (SDL2-devel-2.x-VC.zip) unpacked at %SDL2_DIR%, by
rem default ..\win-deps\SDL2-2.30.9 beside the clone. Reconfigures on every run so the
rem online version always carries the current commit hash: build Windows and
rem the AppImage from the same commit or they cannot play each other online.
setlocal
set "ROOT=%~dp0.."
if not defined SDL2_DIR set "SDL2_DIR=%ROOT%\..\win-deps\SDL2-2.30.9"
if not exist "%SDL2_DIR%\lib\x64\SDL2.lib" (echo SDL2 not found at %SDL2_DIR% ^(set SDL2_DIR^) 1>&2 & exit /b 1)
if not defined VSCMD_ARG_TGT_ARCH (
    for /f "usebackq delims=" %%v in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%v"
)
if defined VSDIR call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%ROOT%" || exit /b 1
for /f %%h in ('git rev-parse --short HEAD') do set ISSD_GIT_HASH=%%h
for /f "delims=" %%p in ('where python') do if not defined PYTHON set "PYTHON=%%p"
rem -Wno-implicit-function-declaration: as on Linux; clang 16+ otherwise
rem rejects a debug-only call in the engine (cpu_state.c).
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
    "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS -Wno-implicit-function-declaration" ^
    -DISSD_FINAL_BUILD=ON -DISSD_GIT_HASH=%ISSD_GIT_HASH% ^
    "-DSDL2_ROOT=%SDL2_DIR%" "-DSDL2_LIBRARY=%SDL2_DIR%\lib\x64\SDL2.lib" ^
    "-DPython3_EXECUTABLE=%PYTHON%" || exit /b 1
cmake --build build-win || exit /b 1
echo Online version: %ISSD_GIT_HASH%
