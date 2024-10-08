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
option(IMGUI_BUNDLE_BUILD_EXAMPLE "Build bundled example" ${IMGUI_BUNDLE_STANDALONE_PROJECT})

add_library(imgui_all_includes INTERFACE)

add_subdirectory(imgui) # adds library img
add_subdirectory(implot)

set(IMNODES_IMGUI_TARGET_NAME imgui_imgui)
add_subdirectory(imnodes)

add_subdirectory(custom)
add_subdirectory(imgui_notify)
add_subdirectory(TextEditor)

set(IMGRID_IMGUI_TARGET imgui_imgui)
add_subdirectory(imgrid)
target_include_directories(imgui_all_includes INTERFACE imnodes)
target_include_directories(imgui_all_includes INTERFACE imgrid)

set(IMGUI_LIBRARIES
    imgui_imgui
    imgui_implot
    imnodes
    imgui_custom
    imgui_color_text_editor
    imgrid
    imnodes
    PARENT_SCOPE)

if(IMGUI_BUNDLE_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
