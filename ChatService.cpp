#include "ChatService.hpp"
#include "UserModel.hpp"
#include "FriendModel.hpp"
#include <vector>
#include <muduo/base/Logging.h>

using namespace muduo::net;

// 单例实现：C++11 保证静态局部变量初始化线程安全
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 登录业务
std::string ChatService::login(int id, const std::string &password, const muduo::net::TcpConnectionPtr &conn)
{
    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    { // 这个锁的生命周期仅在这个大括号里，离开了自动释放
        // 千万不要持锁做 IO！数据库查询、mysql_query 是 IO 操作，很慢！
        // 锁只保护共享内存数据（`_onlineUserMap`）；数据库 IO、业务计算全部放锁外面执行。
        std::lock_guard<std::mutex> lock(_connMutex);
        if (_onlineUserMap.count(id) > 0)
        {
            response["errno"] = LOGIN_FAILED;
            response["errmsg"] = "该用户已在线，请勿重复登录";
            return response.dump();
        }
    } // 锁释放了

    // auto it = _userMap.find(id);
    // if (it == _userMap.end())
    // {
    //     // 用户不存在
    //     response["errno"] = USER_NOT_EXIST;
    //     response["errmsg"] = "用户不存在";
    //     // dump()函数：将js里面的数据全部转换成一个字符串如：{"msgid":1001,"errno":0}
    //     return response.dump();
    // }
    //-----------------淘汰-连接上真实mysql数据库，换成下面------------------
    User user = UserModel::instance()->query(id);
    if (user.id == -1)
    {
        response["errno"] = USER_NOT_EXIST;
        response["errmsg"] = "用户不存在";
        return response.dump();
    }

    // 淘汰 User& user = it->second;
    if (user.password != password)
    {
        // 密码错误
        response["errno"] = WRONG_PASSWORD;
        response["errmsg"] = "密码错误";
        return response.dump();
    }

    // 登录成功，更新数据库状态
    user.state = "online";
    UserModel::instance()->updateState(user);

    {
        std::lock_guard<std::mutex> lock(_connMutex);
        _onlineUserMap[id] = conn;
    }
    std::vector<User> friends = queryFriend(id);
    if(!friends.empty())
    {
        response["friends"] = json::array();
        for (const User& friendUser : friends)
        {
            json friendJson;
            friendJson["id"] = friendUser.id;
            friendJson["name"] = friendUser.name;
            friendJson["state"] = friendUser.state;
            response["friends"].push_back(friendJson);
        }
    }

    response["errno"] = SUCCESS;
    response["id"] = user.id;
    response["name"] = user.name;
    return response.dump();
}

// 注销
// 用户主动下线
std::string ChatService::logout(int id)
{
    // 1.从在线表移除（锁内只做内存操作）
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        _onlineUserMap.erase(id);
    }

    // 2.更新数据库状态（锁外）
    User user;
    user.id = id;
    user.state = "offline";
    UserModel::instance()->updateState(user);

    json response;
    response["msgid"] = LOGINOUT_MSG;
    response["errno"] = SUCCESS;
    return response.dump();
}

// 注册业务
std::string ChatService::reg(const std::string &name, const std::string &password)
{
    json response;
    response["msgid"] = REG_MSG_ACK;

    // 内存版先不做重名检查（后面换 MySQL 再做）
    // 淘汰 int id = _nextId++;
    User user;
    // user.id       = id;
    user.name = name;
    user.password = password;
    user.state = "offline";
    // _userMap[id]  = user;

    if (!UserModel::instance()->insert(user))
    {
        response["errno"] = REG_FAILED;
        response["errmsg"] = "注册失败,用户名已存在";
        return response.dump();
    }

    response["errno"] = SUCCESS;
    response["id"] = user.id;
    return response.dump();
}

// 单聊：查找目标连接并转发
bool ChatService::oneChat(int toId, const json &message)
{
    TcpConnectionPtr targetConn;
    // 1.在锁内查找目标连接，拷贝shared_ptr出来
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _onlineUserMap.find(toId);
        if (it == _onlineUserMap.end())
        {
            return false;
        }
        targetConn = it->second;
    }
    // 2.锁外发送，避免持锁做IO
    targetConn->send(message.dump());
    return true;
}

// ============================================================
// 连接断开：从在线表移除，并把用户状态改成 offline
// 与注销不同在于：连接是异常关闭
// ============================================================
void ChatService::clientCloseException(const muduo::net::TcpConnectionPtr &conn)
{
    int userId = -1;
    // 1.锁内找到并移除
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        for (auto it = _onlineUserMap.begin(); it != _onlineUserMap.end(); ++it)
        {
            if (it->second == conn)
            {
                userId = it->first;
                // unordered_map的erase有两个重载，一个根据键值key，一个根据迭代器it
                _onlineUserMap.erase(it);
                break;
            }
        }
    }
    // 2.锁外更新数据库
    if (userId != -1)
    {
        User user;
        user.id = userId;
        user.state = "offline";
        UserModel::instance()->updateState(user);
        LOG_INFO << "用户" << userId << "已下线";
    }
}

// 添加好友，改成不能直接加好友，而是给目标用户发一条好友申请
std::string ChatService::addFriend(int requesterId, int accepterId)
{
    json response;
    response["msgid"] = ADD_FRIEND_MSG;

    if (requesterId == accepterId)
    {
        response["errno"] = -1;
        response["errmsg"] = "不能添加自己为好友";
        return response.dump();
    }

    User targetUser = UserModel::instance()->query(accepterId);
    if (targetUser.id == -1)
    {
        response["errno"] = USER_NOT_EXIST;
        response["errmsg"] = "好友不存在";
        return response.dump();
    }
    // 通知目标用户
    TcpConnectionPtr targetConn;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _onlineUserMap.find(accepterId);
        if (it != _onlineUserMap.end())
        {
            targetConn = it->second;
        }
    }

    if(targetConn)
    {
        json notifyMsg;
        notifyMsg["msgid"] = FRIEND_REQUEST_NOTICE;
        notifyMsg["fromid"] = requesterId;
        User fromUser = UserModel::instance()->query(requesterId);
        notifyMsg["fromname"] = fromUser.name;
        notifyMsg["type"] = "request";
        targetConn->send(notifyMsg.dump());

        response["errno"] = SUCCESS;
        response["errmsg"] = "好友请求已发送，等待对方同意";
    }
    else
    {
        response["errno"] = USER_OFFLINE;
        response["errmsg"] = "对方不在线，无法发送好友请求";
    }

    return response.dump();
}

// 同意好友请求:并通知申请人
// accepterId: 当前操作同意的用户（自己）, requesterId: 发起好友申请的用户
std::string ChatService::acceptFriend(int accepterId, int requesterId)
{
    json response;
    response["msgid"] = ADD_FRIEND_MSG;

    if (accepterId == requesterId)
    {
        response["errno"] = -1;
        response["errmsg"] = "不能添加自己为好友";
        return response.dump();
    }

    User targetUser = UserModel::instance()->query(requesterId);
    if (targetUser.id == -1)
    {
        response["errno"] = USER_NOT_EXIST;
        response["errmsg"] = "用户不存在";
        return response.dump();
    }
    if (!FriendModel::instance()->insert(accepterId, requesterId))
    {
        response["errno"] = -1;
        response["errmsg"] = "添加好友失败";
        return response.dump();
    }

    // 通知申请人：对方同意了
    TcpConnectionPtr requesterConn;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _onlineUserMap.find(requesterId);
        if (it != _onlineUserMap.end())
        {
            requesterConn = it->second;
        }
    }
    if (requesterConn)
    {
        User accepter = UserModel::instance()->query(accepterId);
        json notifyMsg;
        notifyMsg["msgid"] = FRIEND_REQUEST_NOTICE;
        notifyMsg["fromid"] = accepterId;
        notifyMsg["fromname"] = accepter.name;
        notifyMsg["type"] = "accept";
        requesterConn->send(notifyMsg.dump());
    }

    response["errno"] = SUCCESS;
    response["errmsg"] = "已同意好友申请";
    return response.dump();
}

// 查询某个用户的所有好友列表
std::vector<User> ChatService::queryFriend(int userId)
{
    return FriendModel::instance()->query(userId);
}