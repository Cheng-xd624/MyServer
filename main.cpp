#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<muduo/net/InetAddress.h>
#include<muduo/base/Logging.h>
#include"ChatServer.hpp"
#include<string>

using namespace muduo;
using namespace muduo::net;



int main()
{
    //1、创建事件循环（Reactor的核心）
    EventLoop loop;
    
    //2、监听地址：0.0.0.0:6000,即本机所有网卡的6000端口
    InetAddress addr(6000);

    ChatServer server(&loop,addr,"ChatServer");

    //5、启动服务器（开始监听）
    server.start();

    //6、进入事件循环，阻塞等待事件发生
    loop.loop();

    return 0;
}