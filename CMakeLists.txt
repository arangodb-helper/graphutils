cmake_minimum_required (VERSION 3.5)

project (GraphUtils CXX)
set (GraphUtils_VERSION_MAJOR 0)
set (GraphUtils_VERSION_MINOR 1)

configure_file (
  "${PROJECT_SOURCE_DIR}/GraphUtilsConfig.h.in"
  "${PROJECT_BINARY_DIR}/GraphUtilsConfig.h"
)

include_directories("${PROJECT_BINARY_DIR} ${PROJECT_CURRENT_SOURCE_DIR}")

add_subdirectory(VelocyPack)

add_executable(sampleGraphMaker sampleGraphMaker.cpp)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD 11)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(smartifier smartifier.cpp)
target_link_libraries(smartifier velocypack)
set_property(TARGET smartifier PROPERTY CXX_STANDARD 11)
set_property(TARGET smartifier PROPERTY CXX_STANDARD_REQUIRED ON)
