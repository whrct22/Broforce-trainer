@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ========================================
echo    Dojo NTR Trainer - Transparent Build
echo ========================================
echo.

set "ZIG=D:\zig-x86_64-windows-0.17.0-dev.1415+64dfaa568\zig.exe"
set "CXX=%ZIG% c++ -target x86_64-windows-gnu"
set "BUILD_DIR=build"
set "DIST_DIR=dist"
set "TARGET_DLL=%DIST_DIR%\DojoNTRTrainer.dll"
set "TARGET_EXE=%DIST_DIR%\DojoNTRInjector.exe"
set "TEMP_DLL=%BUILD_DIR%\DojoNTRTrainer.tmp.dll"
set "TEMP_EXE=%BUILD_DIR%\DojoNTRInjector.tmp.exe"

if not exist "%ZIG%" (
    echo [Error] Zig compiler not found: %ZIG%
    goto error
)

call :CheckUnlocked "%TARGET_DLL%"
if errorlevel 1 goto inuse
call :CheckUnlocked "%TARGET_EXE%"
if errorlevel 1 goto inuse

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

set "COMMON_DEFINES=-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE"
set "INCLUDES=-Ivendor/imgui -Ivendor/imgui/backends -Isrc"
set "CXXFLAGS=-std=c++17 -O2 -Wall %COMMON_DEFINES% %INCLUDES%"
set "DLL_LDFLAGS=-shared -static-libgcc -static-libstdc++"
set "EXE_LDFLAGS=-static"
set "DLL_LIBS=-ld3d11 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -lole32 -limm32"

set "IMGUI_SOURCES=vendor\imgui\imgui.cpp vendor\imgui\imgui_draw.cpp vendor\imgui\imgui_tables.cpp vendor\imgui\imgui_widgets.cpp vendor\imgui\backends\imgui_impl_dx11.cpp vendor\imgui\backends\imgui_impl_win32.cpp"
set "TRAINER_SOURCES=src\dllmain.cpp src\memory.cpp src\cheat.cpp src\gui.cpp src\hook.cpp src\config.cpp"
set "DLL_OBJECTS=build\dllmain.o build\memory.o build\cheat.o build\gui.o build\hook.o build\config.o build\imgui.o build\imgui_draw.o build\imgui_tables.o build\imgui_widgets.o build\imgui_impl_dx11.o build\imgui_impl_win32.o"

set NEED_LINK=0
set NEED_EXE=0
set COMPILE_ERRORS=0
set REBUILD_ALL=0
set "BUILD_FLAGS=%CXXFLAGS% ; %DLL_LDFLAGS% ; %EXE_LDFLAGS% ; dist-output-no-pdb"

> "%BUILD_DIR%\build_flags.new" echo %BUILD_FLAGS%
if not exist "%BUILD_DIR%\build_flags.txt" (
    set REBUILD_ALL=1
) else (
    fc /b "%BUILD_DIR%\build_flags.txt" "%BUILD_DIR%\build_flags.new" >nul
    if errorlevel 1 set REBUILD_ALL=1
)
del /q "%BUILD_DIR%\build_flags.new" 2>nul

if %REBUILD_ALL%==1 (
    echo [Config] Build flags changed; recompiling all objects for transparent output...
    del /q "%BUILD_DIR%\*.o" 2>nul
    set NEED_LINK=1
    set NEED_EXE=1
)

:: Compile ImGui without imgui_demo.cpp for the default trainer UI build.
for %%f in (%IMGUI_SOURCES%) do (
    if not exist "%BUILD_DIR%\%%~nf.o" (
        echo [ImGui] Compiling %%~nf.cpp...
        %CXX% %CXXFLAGS% -c %%f -o "%BUILD_DIR%\%%~nf.o"
        if errorlevel 1 set COMPILE_ERRORS=1
        set NEED_LINK=1
    ) else (
        for %%t in (%%f) do (
            for %%o in ("%BUILD_DIR%\%%~nf.o") do (
                if %%~tt GTR %%~to (
                    echo [ImGui] Recompiling %%~nf.cpp...
                    %CXX% %CXXFLAGS% -c %%f -o "%BUILD_DIR%\%%~nf.o"
                    if errorlevel 1 set COMPILE_ERRORS=1
                    set NEED_LINK=1
                )
            )
        )
    )
)

:: Compile Trainer sources.
for %%f in (%TRAINER_SOURCES%) do (
    if not exist "%BUILD_DIR%\%%~nf.o" (
        echo [Trainer] Compiling %%~nf.cpp...
        %CXX% %CXXFLAGS% -c %%f -o "%BUILD_DIR%\%%~nf.o"
        if errorlevel 1 set COMPILE_ERRORS=1
        set NEED_LINK=1
    ) else (
        for %%t in (%%f) do (
            for %%o in ("%BUILD_DIR%\%%~nf.o") do (
                if %%~tt GTR %%~to (
                    echo [Trainer] Recompiling %%~nf.cpp...
                    %CXX% %CXXFLAGS% -c %%f -o "%BUILD_DIR%\%%~nf.o"
                    if errorlevel 1 set COMPILE_ERRORS=1
                    set NEED_LINK=1
                )
            )
        )
    )
)

if %COMPILE_ERRORS%==1 goto error

if not exist "%TARGET_DLL%" set NEED_LINK=1
if %NEED_LINK%==1 (
    call :CheckUnlocked "%TARGET_DLL%"
    if errorlevel 1 goto inuse
    del /q "%TEMP_DLL%" "%BUILD_DIR%\DojoNTRTrainer.tmp.pdb" 2>nul
    echo [Link] Building temporary trainer DLL...
    %CXX% %DLL_LDFLAGS% -o "%TEMP_DLL%" %DLL_OBJECTS% %DLL_LIBS%
    if errorlevel 1 goto error
    call :CheckUnlocked "%TARGET_DLL%"
    if errorlevel 1 (
        del /q "%TEMP_DLL%" "%BUILD_DIR%\DojoNTRTrainer.tmp.pdb" 2>nul
        goto inuse
    )
    echo [Link] Replacing %TARGET_DLL%...
    move /y "%TEMP_DLL%" "%TARGET_DLL%" >nul
    if errorlevel 1 goto error
    echo   Done.
) else (
    echo [Link] %TARGET_DLL% is up to date.
)

if not exist "%TARGET_EXE%" set NEED_EXE=1
if %NEED_EXE%==0 (
    for %%t in (injector\injector.cpp) do (
        for %%o in ("%TARGET_EXE%") do (
            if %%~tt GTR %%~to set NEED_EXE=1
        )
    )
)
if %NEED_EXE%==1 (
    call :CheckUnlocked "%TARGET_EXE%"
    if errorlevel 1 goto inuse
    del /q "%TEMP_EXE%" "%BUILD_DIR%\DojoNTRInjector.tmp.pdb" 2>nul
    echo [Injector] Building temporary injector EXE...
    %CXX% -std=c++17 -O2 -Wall %COMMON_DEFINES% %EXE_LDFLAGS% -o "%TEMP_EXE%" injector\injector.cpp
    if errorlevel 1 goto error
    call :CheckUnlocked "%TARGET_EXE%"
    if errorlevel 1 (
        del /q "%TEMP_EXE%" "%BUILD_DIR%\DojoNTRInjector.tmp.pdb" 2>nul
        goto inuse
    )
    echo [Injector] Replacing %TARGET_EXE%...
    move /y "%TEMP_EXE%" "%TARGET_EXE%" >nul
    if errorlevel 1 goto error
    echo   Done.
) else (
    echo [Injector] %TARGET_EXE% is up to date.
)

> "%BUILD_DIR%\build_flags.txt" echo %BUILD_FLAGS%

:: No PDB files are part of the distribution.
del /q "%DIST_DIR%\*.pdb" "%BUILD_DIR%\*.tmp.pdb" 2>nul

echo.
echo ========================================
echo   BUILD SUCCESS!
echo ========================================
echo.
echo   %TARGET_DLL%
echo   %TARGET_EXE%
echo.
echo   Notes:
echo   - Outputs are written to .\dist only.
echo   - PDB files are not copied to dist.
echo   - If a target is in use, the build stops before replacing it.
echo.
echo   Usage: %TARGET_EXE%
echo.
pause
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
pause
exit /b 2

:error
echo.
echo ========================================
echo   BUILD FAILED!
echo ========================================
pause
exit /b 1
