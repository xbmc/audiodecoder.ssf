project(audiodecoder.ssf)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(xbmc REQUIRED)

include(ExternalProject)

include_directories(${XBMC_INCLUDE_DIR}
                    ${PROJECT_SOURCE_DIR}/lib/psflib
                    ${PROJECT_SOURCE_DIR}/lib/Highly_Theoretical/Core)

add_subdirectory(lib/psflib)
add_subdirectory(lib/Highly_Theoretical)

set(SSF_SOURCES src/SSFCodec.cpp)

set(DEPLIBS highly_theoretical psflib)

build_addon(audiodecoder.ssf SSF DEPLIBS)

include(CPack)
