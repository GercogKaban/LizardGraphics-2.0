cmake_minimum_required (VERSION 3.20)
     
project (DesktopExample C CXX)

set(SOURCE_FILES main.cpp)
set(HEADER_FILES)

ADD_EXECUTABLE(DesktopExample ${SOURCE_FILES} ${HEADER_FILES})

include_directories(${PROJECT_SOURCE_DIR})