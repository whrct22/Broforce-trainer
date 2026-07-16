cmake_minimum_required(VERSION 3.15)
project(BroforceTrainer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# MinHook - 使用预编译库
set(MINHOOK_DIR ${CMAKE_SOURCE_DIR}/MinHook_134_lib)

# Trainer sources
set(TRAINER_SOURCES
    src/dllmain.cpp
    src/memory.cpp
    src/cheat.cpp
    src/gui.cpp
    src/hook.cpp
    src/config.cpp
)

# Build as DLL
add_library(${PROJECT_NAME} SHARED
    ${TRAINER_SOURCES}
    ${IMGUI_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${MINHOOK_DIR}/include
    src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    d3dcompiler
    dxgi
)

# 链接MinHook静态库
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64位
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${MINHOOK_DIR}/lib/libMinHook.x64.lib
    )
else()
    # 32位
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${MINHOOK_DIR}/lib/libMinHook.x86.lib
    )
endif()

# Preprocessor definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Injector executable
add_subdirectory(injector)
