cmake_minimum_required (VERSION 3.20)
     
project (DesktopExample CXX)

set(CMAKE_SKIP_INSTALL_RULES ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

file(GLOB SOURCE_FILES src/*.h src/*.cpp)

find_package(Vulkan REQUIRED)

#add_subdirectory(../LizardGraphics LizardGraphics_build)
include_directories(LizardGraphics ${Vulkan_INCLUDE_DIR})
add_executable(DesktopExample ${SOURCE_FILES})

target_compile_features(DesktopExample PRIVATE cxx_std_20)
target_link_libraries(DesktopExample LizardGraphics ${Vulkan_LIBRARIES} glfw)