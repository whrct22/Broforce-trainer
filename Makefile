# Broforce Trainer - MinGW Makefile
# 使用方法: mingw32-make 或 make

# 编译器
CXX = g++
CC = gcc

# 目标
TARGET = BroforceTrainer.dll
INJECTOR = BroforceInjector.exe

# 路径
IMGUI_DIR = vendor/imgui
MINHOOK_DIR = MinHook_134_lib
SRC_DIR = src
BUILD_DIR = build

# MinHook库 (64位)
MINHOOK_LIB = $(MINHOOK_DIR)/lib/libMinHook.x64.lib

# ImGui源文件
IMGUI_SRCS = \
    $(IMGUI_DIR)/imgui.cpp \
    $(IMGUI_DIR)/imgui_draw.cpp \
    $(IMGUI_DIR)/imgui_tables.cpp \
    $(IMGUI_DIR)/imgui_widgets.cpp \
    $(IMGUI_DIR)/imgui_demo.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_dx11.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_win32.cpp

# Trainer源文件
TRAINER_SRCS = \
    $(SRC_DIR)/dllmain.cpp \
    $(SRC_DIR)/memory.cpp \
    $(SRC_DIR)/cheat.cpp \
    $(SRC_DIR)/gui.cpp \
    $(SRC_DIR)/hook.cpp \
    $(SRC_DIR)/config.cpp

# 所有源文件
ALL_SRCS = $(TRAINER_SRCS) $(IMGUI_SRCS)

# 头文件搜索路径
INCLUDES = -I$(IMGUI_DIR) \
           -I$(IMGUI_DIR)/backends \
           -I$(MINHOOK_DIR)/include \
           -I$(SRC_DIR)

# 编译选项
CXXFLAGS = -std=c++17 -O2 -Wall \
           -D_CRT_SECURE_NO_WARNINGS \
           -DWIN32_LEAN_AND_MEAN \
           -DNOMINMAX \
           $(INCLUDES)

# 链接选项
LDFLAGS = -shared -static-libgcc -static-libstdc++
LDLIBS = -ld3d11 -ld3dcompiler -ldxgi -lgdi32 -lole32 -limm32

# 目标文件
OBJS = $(ALL_SRCS:.cpp=.o)

# ============================================================
# 规则
# ============================================================

.PHONY: all clean injector

all: $(TARGET) $(INJECTOR)

# 编译.cpp文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 链接DLL
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS) -Wl,--allow-multiple-definition

# 注入器
$(INJECTOR): injector/injector.cpp
	$(CXX) -std=c++17 -O2 -DUNICODE -D_UNICODE -o $@ $< -lpsapi

clean:
	del /q *.o src\*.o vendor\imgui\*.o vendor\imgui\backends\*.o 2>nul || true
	del /q $(TARGET) $(INJECTOR) 2>nul || true
