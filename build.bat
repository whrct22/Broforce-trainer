@echo off
setlocal EnableExtensions

echo ========================================
echo    Broforce Trainer - Transparent Build
echo ========================================
echo.

set "ZIG=D:\zig-x86_64-windows-0.17.0-dev.1415+64dfaa568\zig.exe"
set "CXX=%ZIG% c++ -target x86_64-windows-gnu"

if not exist "%ZIG%" (
    echo [Error] Zig compiler not found: %ZIG%
    goto error
)

if not exist build mkdir build

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
set COMPILE_ERRORS=0
set REBUILD_ALL=0
set "BUILD_FLAGS=%CXXFLAGS% ; %DLL_LDFLAGS% ; %EXE_LDFLAGS%"

> build\build_flags.new echo %BUILD_FLAGS%
if not exist build\build_flags.txt (
    set REBUILD_ALL=1
) else (
    fc /b build\build_flags.txt build\build_flags.new >nul
    if errorlevel 1 set REBUILD_ALL=1
)
del /q build\build_flags.new 2>nul

if %REBUILD_ALL%==1 (
    echo [Config] Build flags changed; recompiling all objects for transparent output...
    del /q build\*.o 2>nul
    set NEED_LINK=1
)

:: Compile ImGui without imgui_demo.cpp for the default trainer UI build.
for %%f in (%IMGUI_SOURCES%) do (
    if not exist build\%%~nf.o (
        echo [ImGui] Compiling %%~nf.cpp...
        %CXX% %CXXFLAGS% -c %%f -o build\%%~nf.o
        if errorlevel 1 set COMPILE_ERRORS=1
        set NEED_LINK=1
    ) else (
        for %%t in (%%f) do (
            for %%o in (build\%%~nf.o) do (
                if %%~tt GTR %%~to (
                    echo [ImGui] Recompiling %%~nf.cpp...
                    %CXX% %CXXFLAGS% -c %%f -o build\%%~nf.o
                    if errorlevel 1 set COMPILE_ERRORS=1
                    set NEED_LINK=1
                )
            )
        )
    )
)

:: Compile Trainer sources.
for %%f in (%TRAINER_SOURCES%) do (
    if not exist build\%%~nf.o (
        echo [Trainer] Compiling %%~nf.cpp...
        %CXX% %CXXFLAGS% -c %%f -o build\%%~nf.o
        if errorlevel 1 set COMPILE_ERRORS=1
        set NEED_LINK=1
    ) else (
        for %%t in (%%f) do (
            for %%o in (build\%%~nf.o) do (
                if %%~tt GTR %%~to (
                    echo [Trainer] Recompiling %%~nf.cpp...
                    %CXX% %CXXFLAGS% -c %%f -o build\%%~nf.o
                    if errorlevel 1 set COMPILE_ERRORS=1
                    set NEED_LINK=1
                )
            )
        )
    )
)

if %COMPILE_ERRORS%==1 goto error

:: Check if DLL needs linking.
if not exist BroforceTrainer.dll set NEED_LINK=1
if %NEED_LINK%==1 (
    echo [Link] Building BroforceTrainer.dll...
    %CXX% %DLL_LDFLAGS% -o BroforceTrainer.dll %DLL_OBJECTS% %DLL_LIBS%
    if errorlevel 1 goto error
    echo   Done.
) else (
    echo [Link] BroforceTrainer.dll is up to date.
)

:: Check if injector needs compilation.
if not exist BroforceInjector.exe (
    echo [Injector] Compiling BroforceInjector.exe...
    %CXX% -std=c++17 -O2 -Wall %COMMON_DEFINES% %EXE_LDFLAGS% -o BroforceInjector.exe injector\injector.cpp
    if errorlevel 1 goto error
    echo   Done.
) else if %REBUILD_ALL%==1 (
    echo [Injector] Recompiling BroforceInjector.exe...
    %CXX% -std=c++17 -O2 -Wall %COMMON_DEFINES% %EXE_LDFLAGS% -o BroforceInjector.exe injector\injector.cpp
    if errorlevel 1 goto error
    echo   Done.
) else (
    for %%t in (injector\injector.cpp) do (
        for %%o in (BroforceInjector.exe) do (
            if %%~tt GTR %%~to (
                echo [Injector] Recompiling BroforceInjector.exe...
                %CXX% -std=c++17 -O2 -Wall %COMMON_DEFINES% %EXE_LDFLAGS% -o BroforceInjector.exe injector\injector.cpp
                if errorlevel 1 goto error
                echo   Done.
            )
        )
    )
)

> build\build_flags.txt echo %BUILD_FLAGS%

echo.
echo ========================================
echo   BUILD SUCCESS!
echo ========================================
echo.
echo   BroforceTrainer.dll
echo   BroforceInjector.exe
echo.
echo   Notes:
echo   - Uses normal -O2 builds without strip-all, section GC, packers, or symbol-hiding size passes.
echo   - Statically links MinGW/Zig C++ runtime libraries where possible.
echo   - Windows system DLLs such as kernel32/user32/d3d11 remain dynamic by design.
echo   - The injector defaults to the BroforceTrainer.dll next to BroforceInjector.exe.
echo.
echo   Usage: BroforceInjector.exe
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
