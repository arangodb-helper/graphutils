# docopt
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/docopt.cpp)

# velocypack
set(BuildVelocyPackExamples OFF CACHE BOOL "Flag if velocypack examples are built.")
#set(BuildTools OFF CACHE BOOL "Flag if velocypack tools are built.")
#set(BuildTests OFF CACHE BOOL "Flag if velocypack tests are built.")
#set(BuildAsmTest OFF CACHE BOOL "Flag if velocypack asm tests are built.")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/velocypack)
