cmake_minimum_required (VERSION 3.5)

project (GraphUtils CXX)
set (GraphUtils_VERSION_MAJOR 0)
set (GraphUtils_VERSION_MINOR 2)

configure_file (
  "${PROJECT_SOURCE_DIR}/GraphUtilsConfig.h.in"
  "${PROJECT_BINARY_DIR}/GraphUtilsConfig.h"
)

include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/VelocyPack/include" "${PROJECT_SOURCE_DIR}/docopt")

add_subdirectory(VelocyPack)
add_subdirectory(docopt)

add_executable(sampleGraphMaker sampleGraphMaker.cpp)
target_link_libraries(sampleGraphMaker velocypack docopt_s)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD 11)
set_property(TARGET sampleGraphMaker PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(smartifier smartifier.cpp)
target_link_libraries(smartifier velocypack docopt_s)
set_property(TARGET smartifier PROPERTY CXX_STANDARD 11)
set_property(TARGET smartifier PROPERTY CXX_STANDARD_REQUIRED ON)
