cmake_minimum_required(VERSION 3.16)
# https://github.com/ocornut/imgui
project(imgui_imnotify)

set(CMAKE_CXX_STANDARD 17)

target_include_directories(imgui_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(imgui_all_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
