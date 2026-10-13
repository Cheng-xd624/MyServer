#include "ChatServer.hpp"
#include "ChatService.hpp"
#include "protocol.hpp"

#include <muduo/base/Logging.h>

using namespace muduo;
using namespace muduo::net;

ChatServer::ChatServer(EventLoop* loop,
                       const InetAddress& addr,
                       const std::string& name)
    : _server(loop, addr, name),
      _loop(loop)
{
    // 绑定回调
    _server.setConnectionCallback(
        // `this`：当前 ChatServer 实例的指针，传给成员函数，相当于`onConnection(this, 参数)`
        // `std::placeholders::_1`：**占位符** 含义：muduo 调用回调onConnection的时候，**muduo 传进来的第 1 个参数**，直接放到这个位置。
        std::bind(&ChatServer::onConnection, this, std::placeholders::_1));

        // std::placeholders::_1,2,3代表onMessage三个参数conn,buf,time
    _server.setMessageCallback(
        std::bind(&ChatServer::onMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));

    // 设置 I/O 线程数（0 表示只用主线程）
    _server.setThreadNum(4);
}

void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        // peerAddress():返回 `InetAddress` 对象，代表**客户端的 IP + 端口**
        // .toIpPort():把`InetAddress`对象，序列化成字符串，类似 `"127.0.0.1:54321"`，方便打印日志。
        LOG_INFO << "客户端上线: " << conn->peerAddress().toIpPort();
    }
    else
    {
        LOG_INFO << "客户端下线: " << conn->peerAddress().toIpPort();
        // TODO: 后续在这里维护在线用户表
        ChatService::instance()->clientCloseException(conn);
    }
}

//消息回调函数：当服务器收到客户端消息是自动调用这个函数
//参数：
//  conn: TCP连接对象指针，代表当前客户端连接
//  buf:  接收缓冲区，存放收到的数据
//  time: 收到数据的时间戳
void ChatServer::onMessage(
    //typedef的std::shared_ptr(智能指针类),传引用，不拷贝，原子操作，节省开销
    //`std::shared_ptr` 的引用计数 `refcount` 是**std::atomic**！
    //`atomic<int>` 的自增，CPU 硬件保证**读、加、写合并成不可打断的一步**
    //传引用不会增加拷贝计数refcount，这样就防止此函数阻止销毁conn指针
            const muduo::net::TcpConnectionPtr& conn,
            muduo::net::Buffer* buf,
            muduo::Timestamp time)
{
    // 1 从缓冲区取出所有可读数据，转成字符串
    std::string msg = buf->retrieveAllAsString();
    // 打印日志
    LOG_INFO<<"recv: "<<msg;

    // 2 解析JSON
    try
    {
        json js = json::parse(msg);

        // 3 取出msgid，判断消息类型
        int msgid = js["msgid"].get<int>();
        LOG_INFO<<"msgid="<<msgid;

        // 4 根据msgid分发到不同业务（目前只打印。。。
        switch(msgid)
        {
        // ---------------- 登录 ----------------
        case LOGIN_MSG:
        // C++ 规则：不能在 case 标签下面直接定义带初始化的局部变量
        // 不加`{}`，`id password response`的作用域扩散到整个 switch。
        {
            int id = js["id"].get<int>();
            LOG_INFO<<"收到登录请求,id="<<id;
            std::string password = js["password"].get<std::string>();
            // 把conn登记上在线表
            std::string response = ChatService::instance()->login(id,password,conn);
            conn->send(response);
            break;
        }
         // ---------------- 注册 ----------------
        case REG_MSG:
        {
            std::string name     = js["name"].get<std::string>();
            std::string password = js["password"].get<std::string>();
            std::string response = ChatService::instance()->reg(name, password);
            conn->send(response);
            break;
        }
        // ---------------- 注销 ----------------
         case LOGINOUT_MSG:
        {
            int id = js["id"].get<int>();
            std::string response = ChatService::instance()->logout(id);
            conn->send(response);
            break;
        }
        // ---------------- 单聊 ----------------
        case ONE_CHAT_MSG:
        {
            int toId = js["to"].get<int>();

            // 转发给目标用户
            bool ok = ChatService::instance()->oneChat(toId, js);

            if (!ok)
            {
                // 对方不在线，回给发送方一个错误
                json response;
                response["msgid"]  = ONE_CHAT_MSG;
                response["errno"]  = USER_OFFLINE;
                response["errmsg"] = "对方不在线";
                // 这个case里的response是json的，所以要用dump()转成字符串才能发送
                conn->send(response.dump());
            }
            break;
        }
        // ---------------- 添加好友 ----------------
        case ADD_FRIEND_MSG:
        {
            int userId   = js["id"].get<int>();
            int friendId = js["friendid"].get<int>();
            std::string response = ChatService::instance()->addFriend(userId, friendId);
            conn->send(response);
            break;
        }
        // ---------------- 同意好友请求 ----------------
        case ACCEPT_FRIEND_MSG:
        {
            int userId = js["id"].get<int>();
            int friendId = js["friendid"].get<int>();
            std::string response = ChatService::instance()->acceptFriend(userId, friendId);
            conn->send(response);
            break;
        }
        case GROUP_CHAT_MSG:
            LOG_INFO<<"收到群聊消息,groupid="<<js["groupid"].get<int>();
            break;
        default:
            LOG_WARN<<"未知消息类型 msgid="<<msgid;
            break;
        }
        
    }
    catch(const std::exception& e)
    {
        //JSON解析失败或字段缺失都会走到这里
        LOG_ERROR<<"JSON解析失败:"<<e.what();
        json err;
        err["errno"] = -1;
        err["errmsg"] = "非法消息";
        conn->send(err.dump());
    }
    
}