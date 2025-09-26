cmake_minimum_required(VERSION 3.18)
project(productivity_tracker_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG latest
)
FetchContent_MakeAvailable(glfw)

# GLAD
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)
FetchContent_MakeAvailable(glad)

# cimgui (Dear ImGui C wrapper)
FetchContent_Declare(
  cimgui
  GIT_REPOSITORY https://github.com/cimgui/cimgui.git
  GIT_TAG master
)
FetchContent_MakeAvailable(cimgui)

# stb (for file IO, optional)
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG master 
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static lib (including backends we'll use)
set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_lib STATIC ${IMGUI_SRC})
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_link_libraries(imgui_lib PUBLIC glfw glad)

# Main exe
add_executable(productivity_tracker main.cpp)
target_link_libraries(productivity_tracker PRIVATE imgui_lib glfw glad)

# Platform OpenGL linking
if (WIN32)
    target_link_libraries(productivity_tracker PRIVATE opengl32)
elseif(APPLE)
    find_library(COCOA Cocoa REQUIRED)
    find_library(IOKIT IOKit REQUIRED)
    find_library(COREVIDEO CoreVideo REQUIRED)
    find_library(OPENGL OpenGL REQUIRED)
    target_link_libraries(productivity_tracker PRIVATE ${COCOA} ${IOKIT} ${COREVIDEO} ${OPENGL})
else()
    target_link_libraries(productivity_tracker PRIVATE GL)
endif()

# Ensure include dirs for glad are available to the target (FetchContent provides glad target)
target_include_directories(productivity_tracker PRIVATE ${glad_SOURCE_DIR}/include)
