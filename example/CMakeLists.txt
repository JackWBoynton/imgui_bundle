project(imgui_bundle_example)

find_package(glfw3 CONFIG REQUIRED)
add_executable(example ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(example PUBLIC glfw imgui_all_includes ${IMGUI_LIBRARIES})
