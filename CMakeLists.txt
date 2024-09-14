cmake_minimum_required(VERSION 3.16)
project(imgui)

set(CMAKE_CXX_STANDARD 17)

include(cmake/os.cmake)
detect_os()

# determine whether this is a standalone project or included by other projects
if(NOT DEFINED IMGUI_BUNDLE_STANDALONE_PROJECT)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(IMGUI_BUNDLE_STANDALONE_PROJECT ON)
  else()
    set(IMGUI_BUNDLE_STANDALONE_PROJECT OFF)
  endif()
endif()

option(IMGUI_BUNDLE_BUILD_OBJECT_LIBS "Build bundled lib's object libraries" ON)
option(IMGUI_BUNDLE_BUILD_EXAMPLE "Build bundled example"
       ${IMGUI_BUNDLE_STANDALONE_PROJECT})

add_library(imgui_all_includes INTERFACE)

add_subdirectory(imgui) # adds library imgui_imgui and includes imgui_includes
add_subdirectory(implot)
add_subdirectory(imnodes)
add_subdirectory(custom)
add_subdirectory(imgui_notify)
add_subdirectory(TextEditor)

set(IMGRID_IMGUI_TARGET imgui_imgui)
add_subdirectory(imgrid)

set(IMGUI_LIBRARIES imgui_imgui imgui_implot imgui_imnodes imgui_custom
                    imgui_color_text_editor imgui_imgrid)

if(IMGUI_BUNDLE_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
