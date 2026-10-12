#pragma once

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

// ============================================================
// ChatServer：网络层，封装 Muduo 的 TcpServer
// 只负责接收连接、接收消息、把消息交给 ChatService 处理
// ============================================================
class ChatServer
{
public:
    // 构造函数：传入事件循环、监听地址、服务器名
    ChatServer(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& addr,
               const std::string& name);

    // 启动服务器
    void start();

private:
    // 连接回调：客户端连接建立/断开时被调用
    void onConnection(const muduo::net::TcpConnectionPtr& conn);

    // 消息回调：收到客户端数据时被调用
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp time);

    muduo::net::TcpServer _server;   // Muduo 的 TcpServer 对象
    muduo::net::EventLoop* _loop;    // 事件循环指针
};