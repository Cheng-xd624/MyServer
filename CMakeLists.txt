cmake_minimum_required(VERSION 3.10)
project(MyChatServer)

# 使用 C++11 标准
set(CMAKE_CXX_STANDARD 11)

# 可执行文件输出到项目根目录的 bin/ 下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 假设 Muduo 安装在 /usr/local
include_directories(/usr/local/include)
# 让 #include "ChatServer.hpp" 能找到
include_directories(${PROJECT_SOURCE_DIR})

link_directories(/usr/local/lib)

# 把所有源文件一起编译成ChatServer
add_executable(ChatServer
    main.cpp
    ChatServer.cpp
    ChatService.cpp
    UserModel.cpp
    ConnectionPool.cpp
    )

# 链接 Muduo 网络库、基础库和 pthread
target_link_libraries(ChatServer
    muduo_net
    muduo_base
    pthread
    mysqlclient #加上链接数据库
)

# 新增：客户端可执行程序 ChatClient
add_executable(ChatClient
    main_client.cpp
    ChatClient.cpp
)

target_link_libraries(ChatClient
    muduo_net
    muduo_base
    pthread
)