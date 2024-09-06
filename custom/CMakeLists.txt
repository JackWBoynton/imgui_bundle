cmake_minimum_required(VERSION 3.16)
project(imgui_custom)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

if(NOT INSTRONIMBUS_EXTERNAL_PLUGIN_BUILD)
  add_library(imgui_custom OBJECT imgui_impl_opengl3.cpp imgui_impl_glfw.cpp)

  target_include_directories(imgui_custom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  target_link_libraries(imgui_custom PRIVATE imgui_includes)

  find_package(OpenGL REQUIRED)

  find_package(GLFW QUIET)
  if(NOT GLFW_FOUND OR "${GLFW_LIBRARIES}" STREQUAL "")
    find_package(glfw3 QUIET)
    set(GLFW_INCLUDE_DIRS ${glfw3_INCLUDE_DIRS})
    set(GLFW_LIBRARIES ${glfw3_LIBRARIES})

    if(NOT glfw3_FOUND OR "${GLFW_LIBRARIES}" STREQUAL "")
      find_package(PkgConfig REQUIRED)
      pkg_search_module(GLFW REQUIRED glfw3)
    endif()
  endif()

  if("${GLFW_LIBRARIES}" MATCHES ".+dll")
    set(GLFW_LIBRARIES "glfw3")
  endif()

  target_include_directories(imgui_custom PUBLIC ${FREETYPE_INCLUDE_DIRS} ${OpenGL_INCLUDE_DIRS})
  target_link_directories(imgui_custom PUBLIC ${FREETYPE_LIBRARY_DIRS} ${OpenGL_LIBRARY_DIRS})
  target_link_libraries(imgui_custom PUBLIC ${OPENGL_LIBRARIES} ${GLFW_LIBRARIES})
  if(WIN32)
    target_link_libraries(imgui_custom PUBLIC opengl32)
  elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(CORE_VIDEO_FRAMEWORK CoreVideo)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation)
    target_link_libraries(imgui_custom PUBLIC ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${CORE_VIDEO_FRAMEWORK} ${CORE_FOUNDATION_FRAMEWORK})
  else()
    target_link_libraries(imgui_custom PUBLIC dl X11 pthread)
  endif()
endif()

target_include_directories(imgui_all_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
