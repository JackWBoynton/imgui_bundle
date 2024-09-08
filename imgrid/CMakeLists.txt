cmake_minimum_required(VERSION 3.16)
project(imgui_imgrid)

set(CMAKE_CXX_STANDARD 17)

if(IMGUI_BUNDLE_BUILD_OBJECT_LIBS)
  add_library(imgui_imgrid OBJECT imgrid.cpp)

  target_include_directories(imgui_imgrid PUBLIC .)

  target_link_libraries(imgui_imgrid PRIVATE imgui_includes)
endif()

target_include_directories(imgui_all_includes
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
