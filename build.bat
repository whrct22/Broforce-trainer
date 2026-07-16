@echo off
echo ========================================
echo    Broforce Trainer - Incremental Build
echo ========================================
echo.

set PATH=D:\TDM-GCC\bin;%PATH%

if not exist build mkdir build

set NEED_LINK=0
set COMPILE_ERRORS=0

:: Check if we need to compile ImGui
for %%f in (vendor\imgui\imgui.cpp vendor\imgui\imgui_draw.cpp vendor\imgui\imgui_tables.cpp vendor\imgui\imgui_widgets.cpp vendor\imgui\imgui_demo.cpp vendor\imgui\backends\imgui_impl_dx11.cpp vendor\imgui\backends\imgui_impl_win32.cpp) do (
    if not exist build\%%~nf.o (
        echo [ImGui] Compiling %%~nf.cpp...
        g++ -std=c++17 -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -Ivendor/imgui -Ivendor/imgui/backends -Isrc -c %%f -o build\%%~nf.o
        if errorlevel 1 set COMPILE_ERRORS=1
    )
)

:: Check if we need to compile Trainer sources
for %%f in (src\dllmain.cpp src\memory.cpp src\cheat.cpp src\gui.cpp src\hook.cpp src\config.cpp) do (
    if not exist build\%%~nf.o (
        echo [Trainer] Compiling %%~nf.cpp...
        g++ -std=c++17 -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -Ivendor/imgui -Ivendor/imgui/backends -Isrc -c %%f -o build\%%~nf.o
        if errorlevel 1 set COMPILE_ERRORS=1
        set NEED_LINK=1
    ) else (
        :: Check if source is newer than object
        for %%t in (%%f) do (
            for %%o in (build\%%~nf.o) do (
                if %%~tt GTR %%~to (
                    echo [Trainer] Recompiling %%~nf.cpp...
                    g++ -std=c++17 -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -Ivendor/imgui -Ivendor/imgui/backends -Isrc -c %%f -o build\%%~nf.o
                    if errorlevel 1 set COMPILE_ERRORS=1
                    set NEED_LINK=1
                )
            )
        )
    )
)

if %COMPILE_ERRORS%==1 goto error

:: Check if DLL needs linking
if not exist BroforceTrainer.dll set NEED_LINK=1
if %NEED_LINK%==1 (
    echo [Link] Building BroforceTrainer.dll...
    g++ -shared -static-libgcc -static-libstdc++ -o BroforceTrainer.dll build\dllmain.o build\memory.o build\cheat.o build\gui.o build\hook.o build\config.o build\imgui.o build\imgui_draw.o build\imgui_tables.o build\imgui_widgets.o build\imgui_demo.o build\imgui_impl_dx11.o build\imgui_impl_win32.o -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -lole32 -limm32 -Wl,--allow-multiple-definition
    if errorlevel 1 goto error
    echo   Done.
) else (
    echo [Link] BroforceTrainer.dll is up to date.
)

:: Check if injector needs compilation
if not exist BroforceInjector.exe (
    echo [Injector] Compiling BroforceInjector.exe...
    g++ -std=c++17 -O2 -o BroforceInjector.exe injector\injector.cpp
    if errorlevel 1 goto error
    echo   Done.
) else (
    for %%t in (injector\injector.cpp) do (
        for %%o in (BroforceInjector.exe) do (
            if %%~tt GTR %%~to (
                echo [Injector] Recompiling BroforceInjector.exe...
                g++ -std=c++17 -O2 -o BroforceInjector.exe injector\injector.cpp
                if errorlevel 1 goto error
                echo   Done.
            )
        )
    )
)

echo.
echo ========================================
echo   BUILD SUCCESS!
echo ========================================
echo.
echo   BroforceTrainer.dll
echo   BroforceInjector.exe
echo.
echo   Usage: BroforceInjector.exe BroforceTrainer.dll
echo.
pause
exit /b 0

:error
echo.
echo ========================================
echo   BUILD FAILED!
echo ========================================
pause
exit /b 1
