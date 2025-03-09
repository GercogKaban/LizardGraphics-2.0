cmake_minimum_required (VERSION 3.20)
project (LizardGraphics C CXX)

set(CMAKE_SKIP_INSTALL_RULES ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

file(GLOB HEADER_FILES src/*.h *.txt)
file(GLOB SOURCE_FILES src/*.cpp)

find_package(Vulkan REQUIRED FATAL_ERROR)

add_library(LizardGraphics STATIC ${SOURCE_FILES} ${HEADER_FILES})
include_directories(${PROJECT_SOURCE_DIR} ${Vulkan_INCLUDE_DIR} glfw glfw/include/GLFW magic_enum/include/magic_enum)

#glfw /////////////////////////////////////////////

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)

#/////////////////////////////////////////////////

target_precompile_headers(LizardGraphics PUBLIC src/pch.h)
target_compile_features(LizardGraphics PRIVATE cxx_std_20)