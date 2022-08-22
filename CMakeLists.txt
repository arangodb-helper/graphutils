cmake_minimum_required (VERSION 3.5)

project (GraphUtils CXX)
set (GraphUtils_VERSION_MAJOR 0)
set (GraphUtils_VERSION_MINOR 3)

configure_file (
  "${PROJECT_SOURCE_DIR}/include/GraphUtilsConfig.h.in"
  "${PROJECT_BINARY_DIR}/GraphUtilsConfig.h"
)

# required for clang completion in editors - must be set after creating project
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories("${PROJECT_BINARY_DIR}")

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3")

add_subdirectory(3rdParty)

add_executable(sampleGraphMaker src/sampleGraphMaker.cpp)
target_include_directories(sampleGraphMaker
    PUBLIC
    3rdParty/docopt.cpp
)
target_link_libraries(sampleGraphMaker velocypack docopt)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD 20)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(smartifier src/smartifier.cpp)
target_include_directories(smartifier
    PUBLIC
    3rdParty/docopt.cpp
)
target_link_libraries(smartifier velocypack docopt)
set_property(TARGET smartifier PROPERTY CXX_STANDARD 20)
set_property(TARGET smartifier PROPERTY CXX_STANDARD_REQUIRED ON)
