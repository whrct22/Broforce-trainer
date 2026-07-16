cmake_minimum_required(VERSION 3.15)
project(BroforceInjector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 构建为可执行文件
add_executable(${PROJECT_NAME} injector.cpp)

# 设置字符集为Unicode
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _UNICODE
    UNICODE
)

# 输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Windows子系统
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE FALSE
)
