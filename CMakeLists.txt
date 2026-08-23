cmake_minimum_required(VERSION 3.10)
project(WebServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -O3 -D_PTHREADS")

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
)

# OpenSSL
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# OpenCV (if needed, but it was in the original CMakeLists.txt)
find_package(OpenCV)
if(OpenCV_FOUND)
    message(STATUS "OpenCV found! Version: ${OpenCV_VERSION}")
    include_directories(${OpenCV_INCLUDE_DIRS})
endif()

# Source files
file(GLOB_RECURSE SRCS "src/*.cpp")

# Executable
add_executable(WebServer ${SRCS})

# Link libraries
target_link_libraries(WebServer 
    pthread 
    rt 
    mysqlclient 
    uring 
    OpenSSL::SSL 
    OpenSSL::Crypto
)

if(OpenCV_FOUND)
    target_link_libraries(WebServer ${OpenCV_LIBS})
endif()

# Tests
# add_subdirectory(tests) # We will handle tests separately if needed
# Add module directories to include path to support existing code
include_directories(
    ${PROJECT_SOURCE_DIR}/src/base
    ${PROJECT_SOURCE_DIR}/src/channel
    ${PROJECT_SOURCE_DIR}/src/chat
    ${PROJECT_SOURCE_DIR}/src/epoll
    ${PROJECT_SOURCE_DIR}/src/eventloop
    ${PROJECT_SOURCE_DIR}/src/httpdata
    ${PROJECT_SOURCE_DIR}/src/server
    ${PROJECT_SOURCE_DIR}/src/sql
    ${PROJECT_SOURCE_DIR}/src/thread
    ${PROJECT_SOURCE_DIR}/src/timer
    ${PROJECT_SOURCE_DIR}/src/util
)
