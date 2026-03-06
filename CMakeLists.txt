cmake_minimum_required(VERSION 3.0)
project(WebServer)

# --- +新增: 查找 OpenCV 包 (必需) ---
find_package(OpenCV REQUIRED)
# 打印一下找到的信息，方便调试
message(STATUS "OpenCV found! Version: ${OpenCV_VERSION}")
# ----------------------------------

set(SRCS
    Channel.cpp
    Epoll.cpp
    EventLoop.cpp
    EventLoopThread.cpp
    EventLoopThreadPool.cpp
    HttpData.cpp
    Main.cpp
    Server.cpp
    #ThreadPool.cpp
    Timer.cpp
    Util.cpp
)

include_directories(${PROJECT_SOURCE_DIR}/base)
# --- +新增: 包含 OpenCV 头文件路径 ---
include_directories(${OpenCV_INCLUDE_DIRS})
# ----------------------------------

# 建议把 add_subdirectory 放在 add_executable 之前
# 这样能保证 libserver_base 在链接前就已经定义好了
add_subdirectory(base)

add_executable(WebServer ${SRCS})

# --- +新增: 链接 OpenCV 库 ---
# 注意：${OpenCV_LIBS} 必须加在最后
target_link_libraries(WebServer libserver_base ${OpenCV_LIBS})
# ----------------------------

add_subdirectory(tests)