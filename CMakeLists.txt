cmake_minimum_required (VERSION 3.20)
project (LizardGraphics C CXX)

set(CMAKE_SKIP_INSTALL_RULES ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

file(GLOB HEADER_FILES src/*.h)
file(GLOB SOURCE_FILES src/*.cpp src/shaders/*.frag src/shaders/*.vert)

if(WIN32)
    set(PYTHON_COMMAND python)
else()
    set(PYTHON_COMMAND python3)
endif()

find_package(Vulkan REQUIRED)

add_library(LizardGraphics STATIC ${SOURCE_FILES} ${HEADER_FILES})
target_include_directories(LizardGraphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/glfw/include/GLFW>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/magic_enum/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator/include>
    $<INSTALL_INTERFACE:src>
    ${Vulkan_INCLUDE_DIR}
)

#glfw /////////////////////////////////////////////

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)

#/////////////////////////////////////////////////

add_custom_target(run_script ALL 
    COMMAND ${CMAKE_COMMAND} -E env PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR} ${PYTHON_COMMAND} ${PROJECT_SOURCE_DIR}/build_shaders.py
    COMMENT "Running script before build"
)

add_dependencies(LizardGraphics run_script)

target_precompile_headers(LizardGraphics PUBLIC src/pch.h)
target_compile_features(LizardGraphics PRIVATE cxx_std_20)