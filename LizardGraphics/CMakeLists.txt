cmake_minimum_required (VERSION 3.20)
     
project (LizardGraphics C CXX)

file(GLOB SOURCE_FILES *.cpp)
file(GLOB HEADER_FILES *.h)

ADD_LIBRARY( LizardGraphics STATIC
             ${SOURCE_FILES}
             ${HEADER_FILES})

get_filename_component(PARENT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

target_include_directories (LizardGraphics PUBLIC ${PARENT_DIRECTORY}/ThirdParty/glfw)
target_include_directories (LizardGraphics PUBLIC ${PARENT_DIRECTORY}/ThirdParty/glfw/include)
target_include_directories (LizardGraphics PUBLIC ${PARENT_DIRECTORY}/ThirdParty/glm)
target_include_directories (LizardGraphics PUBLIC ${PARENT_DIRECTORY}/ThirdParty/vulkan/Include)

target_link_libraries(LizardGraphics ../ThirdParty/glfw ../ThirdParty/vulkan/Lib/vulkan-1)
