cmake_minimum_required (VERSION 3.5)

project (GraphUtils CXX)
set (GraphUtils_VERSION_MAJOR 0)
set (GraphUtils_VERSION_MINOR 3)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

configure_file (
  "${PROJECT_SOURCE_DIR}/include/GraphUtilsConfig.h.in"
  "${PROJECT_BINARY_DIR}/GraphUtilsConfig.h"
)

# required for clang completion in editors - must be set after creating project
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories("${PROJECT_BINARY_DIR}")

find_package(Threads REQUIRED)

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

find_package(OpenSSL REQUIRED)

add_executable(smartifier2
  src/smartifier2.cpp
  src/CommandLineParsing.cpp)
target_include_directories(smartifier2 PUBLIC)
target_link_libraries(smartifier2
  velocypack
  ${CMAKE_THREAD_LIBS_INIT}
  OpenSSL::SSL
  OpenSSL::Crypto
)
set_property(TARGET smartifier2 PROPERTY CXX_STANDARD 20)
set_property(TARGET smartifier2 PROPERTY CXX_STANDARD_REQUIRED ON)

if(COVERAGE)
  if(CMAKE_COMPILER_IS_GNUCXX)
    include(CodeCoverage)
    append_coverage_compiler_flags()
    setup_target_for_coverage_gcovr_html(
      NAME tests_coverage
      EXECUTABLE ../testRunner.sh
      #EXECUTABLE tests 1200000 ..
      DEPENDENCIES smartifier2
      EXCLUDE "3rdParty/*"
    )
  endif()
endif()
