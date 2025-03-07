cmake_minimum_required (VERSION 3.20)
     
project (DesktopExample CXX)

file(GLOB SOURCE_FILES src/*.h src/*.cpp)

find_package(Vulkan REQUIRED FATAL_ERROR)

add_subdirectory(LizardGraphics)
include_directories(LizardGraphics ${Vulkan_INCLUDE_DIR})
add_executable(DesktopExample ${SOURCE_FILES})

target_compile_features(DesktopExample PRIVATE cxx_std_17)
target_link_libraries(DesktopExample LizardGraphics ${Vulkan_LIBRARIES} glfw)