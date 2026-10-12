#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <thread>

#include "ChatClient.hpp"

using namespace muduo::net;

int main()
{
    // 1. 创建事件循环对象，muduo的反应堆Reactor，IO线程就在这里跑
    EventLoop loop;
    // 2. 服务器地址：本机127.0.0.1，端口6000
    InetAddress serverAddr("127.0.0.1", 6000);

    // 3. ChatClient muduo封装的Tcp客户端，绑定事件循环 + 服务端地址
    ChatClient client(&loop, serverAddr);
    // 4. 发起TCP连接，非阻塞！不会等连接完成才返回
    client.connect();

    // 5. 新开**独立子线程**专门处理控制台输入(stdin)
    // lambda捕获引用&client，子线程调用handleInput()读取键盘
    std::thread inputThread([&client] {
        client.handleInput();
    });

    // 6. 主线程进入事件循环，阻塞在这里！处理网络IO（收发消息、连接事件）
    loop.loop();

    // 7. loop退出之后，等待输入线程结束，回收线程资源
    if (inputThread.joinable()) inputThread.join();
    return 0;
}