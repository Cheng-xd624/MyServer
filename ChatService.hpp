#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

#include <muduo/net/TcpConnection.h>

#include "protocol.hpp"
#include "user.hpp"

// // ============================================================
// // 用户信息（内存版，后面会换成 MySQL）
// // ============================================================
// struct User
// {
//     int    id;        // 用户 id
//     std::string name; // 用户名
//     std::string password; // 密码
//     std::string state;    // 状态：online / offline
// };

// ============================================================
// ChatService：业务层单例
// 负责登录、注册、聊天等业务处理
// 新增----> 负责维护在线用户表+单聊转发
// ============================================================
class ChatService
{
public:
    // 获取单例（懒汉式 + C++11 静态局部变量，天然线程安全）
    static ChatService* instance();

    // 禁止拷贝
    ChatService(const ChatService&) = delete;
    ChatService& operator=(const ChatService&) = delete;

    // 处理登录业务，返回给客户端的 JSON 字符串 ----新增---->登录成功时把 conn 记入在线表
    std::string login(int id, const std::string& password,const muduo::net::TcpConnectionPtr& conn);

    // 新增---->注销：移除在线记录，更新数据库状态
    std::string logout(int id);
    // 处理注册业务，返回给客户端的 JSON 字符串
    std::string reg(const std::string& name, const std::string& password);

    // 单聊转发:查找目标用户的连接并转发
    bool oneChat(int toId,const json& message);

    // 客户端断开连接时调用：从在线表移除
    void clientCloseException(const muduo::net::TcpConnectionPtr& conn);

private:
    // 构造函数私有，单例
    ChatService() = default;
    
    // 在线用户表
    std::unordered_map<int,muduo::net::TcpConnectionPtr> _onlineUserMap;

    // 保护_onlineUserMap，多线程环境下业务可能被并发调用
    std::mutex _connMutex;

    // // 内存版用户表：id -> User
    // std::unordered_map<int, User> _userMap;

    // // 下一个要分配的 id
    // int _nextId = 1000;

    // // 保护 _userMap 的互斥锁（多线程下业务层可能被多线程访问）
    // std::mutex _mutex;
};