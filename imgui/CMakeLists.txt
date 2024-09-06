cmake_minimum_required(VERSION 3.16)
# https://github.com/ocornut/imgui
project(imgui_imgui)

set(CMAKE_CXX_STANDARD 17)

if (NOT INSTRONIMBUS_EXTERNAL_PLUGIN_BUILD)
  add_library(
    imgui_imgui OBJECT
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_spectrum.cpp
    misc/freetype/imgui_freetype.cpp)

  target_include_directories(imgui_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} misc/freetype)

  target_compile_options(imgui_imgui PRIVATE -Wno-unknown-warning-option)

  if (NOT EMSCRIPTEN)
    find_package(Freetype REQUIRED)
  else ()
    find_package(Freetype CONFIG)
    if (NOT Freetype_FOUND)
      if (NOT DEFINED INSTRONIMBUS_CUSTOM_FREETYPE_DIR)
        message(FATAL_ERROR "Freetype not found. Please set INSTRONIMBUS_CUSTOM_FREETYPE_DIR to the directory containing the FreetypeConfig.cmake file.")
      else ()
        set(FREETYPE_INCLUDE_DIRS "${INSTRONIMBUS_CUSTOM_FREETYPE_DIR}/include")
        set(FREETYPE_LIBRARY_DIRS "${INSTRONIMBUS_CUSTOM_FREETYPE_DIR}/lib")
      endif ()
    endif ()
  endif ()
  message(STATUS "Freetype include dirs: ${FREETYPE_INCLUDE_DIRS}")
  message(STATUS "Freetype library dirs: ${FREETYPE_LIBRARY_DIRS}")

  target_include_directories(imgui_imgui PUBLIC ${FREETYPE_INCLUDE_DIRS})
  target_link_directories(imgui_imgui PUBLIC ${FREETYPE_LIBRARY_DIRS})
  target_link_libraries(imgui_imgui PUBLIC ${FREETYPE_LIBRARIES})
endif ()

add_library(imgui_includes INTERFACE)
target_include_directories(imgui_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(imgui_all_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} misc/freetype)
