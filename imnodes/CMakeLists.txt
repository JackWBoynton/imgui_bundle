cmake_minimum_required(VERSION 3.16)
# https://github.com/Nelarius/imnodes
project(imgui_imnodes)

set(CMAKE_CXX_STANDARD 17)

if(IMGUI_BUNDLE_BUILD_OBJECT_LIBS)
  add_library(imgui_imnodes OBJECT imnodes.cpp)

  target_include_directories(imgui_imnodes PUBLIC .)

  target_link_libraries(imgui_imnodes PRIVATE imgui_includes)
endif()

target_include_directories(imgui_all_includes
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
