#pragma once

#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <atomic>
#include <string>

#include "protocol.hpp"

// ============================================================
// ChatClient：命令行聊天客户端
// 网络部分基于 Muduo TcpClient
// 主线程读用户输入，网络线程接收服务器响应
// ============================================================
class ChatClient
{
public:
    ChatClient(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& serverAddr);

    // 发起连接
    void connect();

    // 在单独线程中运行：显示菜单、读用户输入、发送 JSON
    void handleInput();

private:
    // 连接回调：连接建立/断开
    void onConnection(const muduo::net::TcpConnectionPtr& conn);

    // 消息回调：收到服务器 JSON 响应
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp time);

    // 发送一个 JSON 对象
    void sendJson(const json& js);

    // 当前登录状态（原子变量，多线程访问）
    std::atomic<bool> _loggedIn{false};
    int               _userId = -1;
    std::string       _username;

    muduo::net::TcpClient   _client;
    muduo::net::EventLoop*  _loop;
    muduo::net::TcpConnectionPtr _conn;   // 当前连接
};