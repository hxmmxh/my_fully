cmake_minimum_required (VERSION 3.10)
project(HxmFolly)

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
link_directories(${PROJECT_BINARY_DIR}/lib)

include_directories(${PROJECT_SOURCE_DIR})

# aux_source_directory(. WebServer_srcs)
add_library(HString STATIC hstring.cpp)

add_executable(stest test/stest.cpp)
target_link_libraries(stest HString)