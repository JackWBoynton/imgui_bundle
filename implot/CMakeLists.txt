cmake_minimum_required(VERSION 3.16)
# https://github.com/epezent/implot
project(imgui_implot)

set(CMAKE_CXX_STANDARD 17)

if(IMGUI_BUNDLE_BUILD_OBJECT_LIBS)
  add_library(imgui_implot OBJECT implot.cpp implot_items.cpp implot_demo.cpp)

  target_include_directories(imgui_implot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  target_link_libraries(imgui_implot PRIVATE imgui_includes)
endif()

target_include_directories(imgui_all_includes
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
