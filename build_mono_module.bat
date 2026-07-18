@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ========================================
echo    Standalone Unity Mono Module Build
echo ========================================
echo.

set "ZIG=D:\zig-x86_64-windows-0.17.0-dev.1415+64dfaa568\zig.exe"
set "CXX=%ZIG% c++ -target x86_64-windows-gnu"
set "BUILD_DIR=build"
set "DIST_DIR=dist"
set "TARGET_DLL=%DIST_DIR%\MonoModule.dll"
set "TEMP_DLL=%BUILD_DIR%\MonoModule.tmp.dll"

if not exist "%ZIG%" (
    echo [Error] Zig compiler not found: %ZIG%
    goto error
)

call :CheckUnlocked "%TARGET_DLL%"
if errorlevel 1 goto inuse

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

set "COMMON_DEFINES=-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE"
set "CXXFLAGS=-std=c++17 -O2 -Wall %COMMON_DEFINES% -Imono_module"
set "LDFLAGS=-shared -static-libgcc -static-libstdc++"
set "LIBS=-luser32 -lkernel32"

del /q "%TEMP_DLL%" "%BUILD_DIR%\MonoModule.tmp.pdb" 2>nul

echo [MonoModule] Compiling mono_module.cpp...
%CXX% %CXXFLAGS% -c mono_module\mono_module.cpp -o "%BUILD_DIR%\mono_module.o"
if errorlevel 1 goto error

echo [MonoModule] Linking temporary DLL...
%CXX% %LDFLAGS% -o "%TEMP_DLL%" "%BUILD_DIR%\mono_module.o" %LIBS%
if errorlevel 1 goto error

call :CheckUnlocked "%TARGET_DLL%"
if errorlevel 1 (
    del /q "%TEMP_DLL%" "%BUILD_DIR%\MonoModule.tmp.pdb" 2>nul
    goto inuse
)

echo [MonoModule] Replacing %TARGET_DLL%...
move /y "%TEMP_DLL%" "%TARGET_DLL%" >nul
if errorlevel 1 goto error

:: No PDB files are part of the distribution.
del /q "%DIST_DIR%\MonoModule.pdb" "%BUILD_DIR%\MonoModule.tmp.pdb" 2>nul

echo.
echo ========================================
echo   BUILD SUCCESS!
echo ========================================
echo.
echo   %TARGET_DLL%
echo   mono_module\mono_module.h
echo.
echo   Config switch:
echo     [unity]
echo     enabled=1
echo     managed_dir=OptionalPathToManagedFolder
echo.
exit /b 0

:CheckUnlocked
set "LOCK_CHECK_PATH=%~1"
if not exist "%LOCK_CHECK_PATH%" exit /b 0
powershell -NoProfile -Command "try { $p=$env:LOCK_CHECK_PATH; $fs=[System.IO.File]::Open($p,[System.IO.FileMode]::Open,[System.IO.FileAccess]::ReadWrite,[System.IO.FileShare]::None); $fs.Close(); exit 0 } catch { exit 1 }" >nul 2>nul
if errorlevel 1 (
    echo [In Use] %LOCK_CHECK_PATH% is currently loaded or locked.
    echo [In Use] Please unload it from the game process or close the game, then build again.
    exit /b 1
)
exit /b 0

:inuse
echo.
echo ========================================
echo   BUILD SKIPPED: TARGET IN USE
echo ========================================
echo No output file was generated or replaced.
exit /b 2

:error
echo.
echo ========================================
echo   BUILD FAILED!
echo ========================================
exit /b 1
