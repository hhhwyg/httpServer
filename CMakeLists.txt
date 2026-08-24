cmake_minimum_required(VERSION 3.21)

project(httpServer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(HTTPSERVER_BUILD_EXAMPLES "Build manual example programs" OFF)
option(HTTPSERVER_BUILD_MANUAL_TESTS "Build long-running manual test programs" OFF)
option(HTTPSERVER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(HTTPSERVER_SANITIZER "" CACHE STRING
    "Sanitizer to enable: address,undefined or thread")

set(HTTPSERVER_CORE_SOURCES
    src/IoUring.cpp
    src/Poller.cpp
    src/config/ServerConfig.cpp
    src/base/AsyncLogging.cpp
    src/base/CountDownLatch.cpp
    src/base/FileUtil.cpp
    src/base/LogFile.cpp
    src/base/Logging.cpp
    src/base/LogStream.cpp
    src/base/Thread.cpp
    src/channel/Channel.cpp
    src/chat/ChatManager.cpp
    src/chat/ChatRoom.cpp
    src/chat/RoomController.cpp
    src/chat/UserController.cpp
    src/epoll/Epoll.cpp
    src/eventloop/EventLoop.cpp
    src/eventloop/EventLoopThread.cpp
    src/eventloop/EventLoopThreadPool.cpp
    src/httpdata/HttpData.cpp
    src/httpdata/MimeType.cpp
    src/server/Server.cpp
    src/sql/SqlConnPool.cpp
    src/thread/ThreadPool.cpp
    src/timer/Timer.cpp
    src/util/CryptoUtil.cpp
    src/util/Util.cpp
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h REQUIRED)
find_library(MYSQL_LIBRARY NAMES mysqlclient mariadb REQUIRED)
find_path(URING_INCLUDE_DIR NAMES liburing.h REQUIRED)
find_library(URING_LIBRARY NAMES uring REQUIRED)

# Legacy headers still use short cross-module includes such as "Channel.h".
# Keep the compatibility include list private until the Phase 4 module split.
set(HTTPSERVER_LEGACY_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/base
    ${PROJECT_SOURCE_DIR}/src/channel
    ${PROJECT_SOURCE_DIR}/src/chat
    ${PROJECT_SOURCE_DIR}/src/config
    ${PROJECT_SOURCE_DIR}/src/epoll
    ${PROJECT_SOURCE_DIR}/src/eventloop
    ${PROJECT_SOURCE_DIR}/src/httpdata
    ${PROJECT_SOURCE_DIR}/src/server
    ${PROJECT_SOURCE_DIR}/src/sql
    ${PROJECT_SOURCE_DIR}/src/thread
    ${PROJECT_SOURCE_DIR}/src/timer
    ${PROJECT_SOURCE_DIR}/src/util
)

add_library(httpserver_options INTERFACE)
target_compile_features(httpserver_options INTERFACE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(httpserver_options INTERFACE -Wall -Wextra -Wpedantic)
    if(HTTPSERVER_WARNINGS_AS_ERRORS)
        target_compile_options(httpserver_options INTERFACE -Werror)
    endif()
endif()

if(HTTPSERVER_SANITIZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    if(NOT HTTPSERVER_SANITIZER MATCHES "^(address,undefined|thread)$")
        message(FATAL_ERROR
            "HTTPSERVER_SANITIZER must be address,undefined or thread")
    endif()
    target_compile_options(httpserver_options INTERFACE
        "-fsanitize=${HTTPSERVER_SANITIZER}" -fno-omit-frame-pointer)
    target_link_options(httpserver_options INTERFACE
        "-fsanitize=${HTTPSERVER_SANITIZER}")
endif()

add_library(httpserver_core STATIC ${HTTPSERVER_CORE_SOURCES})
add_library(httpserver::core ALIAS httpserver_core)
target_include_directories(httpserver_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party
    PRIVATE
        ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
        ${MYSQL_INCLUDE_DIR}
        ${URING_INCLUDE_DIR}
)
target_link_libraries(httpserver_core
    PUBLIC
        httpserver_options
    PRIVATE
        Threads::Threads
        ${MYSQL_LIBRARY}
        ${URING_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_executable(WebServer src/Main.cpp)
target_include_directories(WebServer PRIVATE ${HTTPSERVER_LEGACY_INCLUDE_DIRS})
target_link_libraries(WebServer PRIVATE httpserver::core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(HTTPSERVER_BUILD_EXAMPLES)
    add_executable(HTTPClient tests/HTTPClient.cpp)
    target_link_libraries(HTTPClient PRIVATE httpserver_options)
endif()
