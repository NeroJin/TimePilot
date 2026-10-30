# TimePilot - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(TimePilot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)


set(TIMEPILOT_SOURCES
    src/main.cpp
    src/fixes.cpp
    src/hooks.cpp
)

if(WIN32)
    add_executable(TimePilot WIN32 ${TIMEPILOT_SOURCES})
else()
    add_executable(TimePilot ${TIMEPILOT_SOURCES})
endif()

# GPU_PLUGINS xenos: 构建时把 SDK 的 rexgpu-xenos.dll 搬到 exe 旁边，
# 运行时 gpu_plugin cvar 填 "xenos" 即可按 <exe目录>/rexgpu-xenos.dll 找到
rexglue_setup_target(TimePilot FORCE GPU_PLUGINS xenos)

