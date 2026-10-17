#include "ChatClient.hpp"
#include "protocol.hpp"
#include "user.hpp"

#include <muduo/base/Logging.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

#include <termios.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

// ============================================================
// 终端原始模式 RAII：进入时关闭行缓冲+回显，离开时自动恢复
// ============================================================
class TerminalRaw
{
    termios _old;

public:
    TerminalRaw()
    {
        tcgetattr(STDIN_FILENO, &_old);
        termios t = _old;
        t.c_lflag &= ~(ICANON | ECHO); // 关闭规范模式和回显，逐字节读取
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    ~TerminalRaw()
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &_old);
    }
};

enum FilterMode
{
    FILTER_USERNAME,
    FILTER_PASSWORD
};

// 校验一个输入字节；合法则把整段字符(含多字节UTF-8)填进 out，width=终端显示宽度
// 返回 1=合法，0=非法（调用方响铃拒绝）
static int checkChar(char firstByte, FilterMode mode, std::string &out, int &width)
{
    unsigned char c = static_cast<unsigned char>(firstByte);
    out.clear();

    if (mode == FILTER_USERNAME)
    {
        // 字母、数字
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            out.push_back(firstByte);
            width = 1;
            return 1;
        }
        // 中文 UTF-8 三字节序列 (0xE0~0xEF 开头)
        if (c >= 0xE0 && c <= 0xEF)
        {
            out.push_back(firstByte);
            char b2, b3;
            if (read(STDIN_FILENO, &b2, 1) == 1 && read(STDIN_FILENO, &b3, 1) == 1)
            {
                out.push_back(b2);
                out.push_back(b3);
                width = 2; // 中文占终端 2 列
                return 1;
            }
            return 0;
        }
    }
    else // FILTER_PASSWORD：可打印 ASCII 0x21~0x7E，不含空格
    {
        if (c >= 0x21 && c <= 0x7E)
        {
            out.push_back(firstByte);
            width = 1;
            return 1;
        }
    }
    return 0;
}

// 逐字符读取一行：非法字符响铃拒绝、不进字符串；退格时整行重绘，避免宽字符擦除残留
static std::string readFiltered(const std::string &prompt, FilterMode mode)
{
    std::cout << prompt;
    std::cout.flush();

    TerminalRaw raw;
    std::string result;

    // 删掉 result 末尾的一个 UTF-8 字符（兼容中文3字节、ASCII 1字节）
    auto eraseLastUtf8 = [](std::string &s)
    {
        if (s.empty())
            return;
        size_t pos = s.size();
        // 回退跳过所有 UTF-8 后续字节(10xxxxxx)
        while (pos > 0 && (static_cast<unsigned char>(s[pos - 1]) & 0xC0) == 0x80)
            --pos;
        if (pos > 0)
            --pos; // 再跳一个首字节
        s.erase(pos);
    };

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == '\n' || c == '\r') // 回车结束
        {
            std::cout << std::endl;
            break;
        }
        if (c == '\b' || c == 0x7f) // 退格
        {
            if (!result.empty())
            {
                eraseLastUtf8(result);
                // 整行重绘：\r 回行首 -> \x1b[K 清到行尾 -> 重新打印 prompt + result
                std::cout << "\r\x1b[K" << prompt << result;
                std::cout.flush();
            }
            continue;
        }

        std::string chBytes;
        int width = 0;
        if (checkChar(c, mode, chBytes, width) == 1)
        {
            result += chBytes;
            std::cout << chBytes;
            std::cout.flush();
        }
        else
        {
            std::cout << '\a'; // 响铃提示非法字符
            std::cout.flush();
        }
    }
    return result;
}

ChatClient::ChatClient(EventLoop *loop, const InetAddress &serverAddr)
    : _client(loop, serverAddr, "ChatClient"),
      _loop(loop)
{
    _client.setConnectionCallback(
        std::bind(&ChatClient::onConnection, this, std::placeholders::_1));

    _client.setMessageCallback(
        std::bind(&ChatClient::onMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));
}

void ChatClient::connect()
{
    _client.connect();
}

// 连接回调
void ChatClient::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        std::cout << "[系统] 已连接到服务器 "
                  << conn->peerAddress().toIpPort() << std::endl;
        _conn = conn;
    }
    else
    {
        std::cout << "[系统] 与服务器断开连接" << std::endl;
        _conn.reset();
    }
}

// 发送 JSON 给客户端
void ChatClient::sendJson(const json &js)
{
    if (_conn && _conn->connected())
    {
        // **TcpConnection 的内部数据，只允许它所属 IO 线程读写；别的线程碰它的 send，会并发读写缓冲区，造成内存错乱、崩溃**。
        // 而handleInput在单独开辟的线程中运行，调用这个sendJson函数时，非主IO线程读写这块缓冲区
        // 所以send()内部会调用私有成员函数sendInLoop(),sendInLoop()会检查当前线程是不是主IO线程，
        // 不是的话就把任务发给主线程的任务队列，交由主IO线程读写
        _conn->send(js.dump());
    }
    else
    {
        std::cout << "[错误] 未连接到服务器" << std::endl;
    }
}

/**
 * @brief muduo网络库的消息回调函数：处理服务器下发的响应消息
 * @param conn 当前TCP连接的智能指针 TcpConnectionPtr
 * @param buf 接收缓冲区，存放从网络读到的二进制数据
 * @param time 消息到达的时间戳
 * @note 当客户端收到服务端发来的数据时，muduo自动调用此回调
 */
void ChatClient::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buf,
                           Timestamp time)
{
    // // 把缓冲区所有数据取出，转为std::string字符串
    std::string msg = buf->retrieveAllAsString();

    try
    {
        // 将字符串反序列化为nlohmann::json对象
        json js = json::parse(msg);
        // 取出消息类型msgid，判断是哪一类服务器应答
        int msgid = js["msgid"].get<int>();

        switch (msgid)
        {
        case LOGIN_MSG_ACK:
        {
            if (js["errno"].get<int>() == SUCCESS)
            {
                // contains()检查json对象中是否包含指定的键，返回true/false
                if(js.contains("offlinemsgs") && !js["offlinemsgs"].empty())
                {
                    std::cout<<"[系统] 收到离线消息:\n";
                    // 离线消息由服务器存放再"offlinemsgs"数组里，每个元素都是一个json对象
                    // offlinemsgs 数组里混着两种消息：
                    //   1. 单聊消息   {"msgid":5,"from":x,"to":当前id,"message":"..."}
                    //   2. 好友通知   {"msgid":12,"fromid":x,"fromname":"...","type":"request"/"accept"/"reject"}
                    // 必须按 msgid 分流，不能一刀切取 ["from"]/["message"]，否则取好友通知时会拿到 null 崩溃。
                    for(auto& msg : js["offlinemsgs"])
                    {
                        int subId = msg["msgid"].get<int>();
                        if (subId == ONE_CHAT_MSG) // 单聊消息
                        {
                            int form = msg["from"].get<int>();
                            std::string message = msg["message"].get<std::string>();
                            std::cout<<" 【单聊】 来自 "<<form<<" 的离线消息: "<<message<<std::endl;
                        }
                        else if (subId == FRIEND_REQUEST_NOTICE) // 好友通知
                        {
                            int fromId = msg["fromid"].get<int>();
                            std::string fromName = msg["fromname"].get<std::string>();
                            std::string type = msg["type"].get<std::string>();
                            if(type=="accept")
                                std::cout<<" 【好友】 "<<fromName<<" ("<<fromId<<") 已同意你的好友申请"<<std::endl;
                            else
                                std::cout<<" 【好友】 "<<fromName<<" ("<<fromId<<") 请求添加你为好友"<<std::endl;
                        }
                        else
                        {
                            std::cout<<" 【未知】 收到未知类型离线消息: "<<msg.dump()<<std::endl;
                        }
                    }
                }
                // _friends 用于存储当前用户的好友列表，方便在客户端显示和操作
                std::vector<User> _friends;
                _friends.clear();
                if(js.contains("friends") && !js["friends"].empty())
                {
                    for (const auto& friendJson : js["friends"])
                    {
                        User friendInfo;
                        friendInfo.id = friendJson["id"].get<int>();
                        friendInfo.name = friendJson["name"].get<std::string>();
                        friendInfo.state = friendJson["state"].get<std::string>();
                        _friends.push_back(friendInfo);
                    }
                }
                
                std::cout << "\n[系统] 好友列表:\n";
                for (const auto& friendInfo : _friends)
                {
                    std::cout << "  id: " << friendInfo.id
                              << ", name: " << friendInfo.name
                              << ", state: " << friendInfo.state << std::endl;
                }

                _loggedIn = true;
                _userId = js["id"].get<int>();
                _username = js["name"].get<std::string>();
                std::cout << "\n[系统] 登录成功，欢迎 "
                          << _username << " (id=" << _userId << ")\n"
                          << "输入 'chat <id> <msg>' 发送消息\n"
                          << "输入 'addfriend <id>' 添加好友\n"
                          << "输入 'acceptfriend <id>' 同意好友申请\n"
                          << "输入 'rejectfriend <id>' 拒绝好友申请\n"
                          << "输入 'logout' 注销\n"
                          << std::endl;
            }
            else
            {
                std::cout << "\n[系统] 登录失败: "
                          << js["errmsg"].get<std::string>() << std::endl;
            }
            break;
        }

        case REG_MSG_ACK:
        {
            if (js["errno"].get<int>() == SUCCESS)
            {
                std::cout << "\n[系统] 注册成功，你的 id 是 "
                          << js["id"].get<int>() << "，请记住它\n"
                          << std::endl;
            }
            else
            {
                std::cout << "\n[系统] 注册失败: "
                          << js["errmsg"].get<std::string>() << std::endl;
            }
            break;
        }

        case ONE_CHAT_MSG:
        {
            if (js.contains("errno"))
            {
                // 服务器返回的错误
                std::cout << "\n[系统]" << js["errmsg"].get<std::string>() << std::endl;
            }
            else
            {
                // 正常转发的消息
                std::cout << "\n[新消息] 来自 " << js["from"].get<int>()
                          << ": " << js["message"].get<std::string>() << std::endl;
            }
            break;
        }

        case ADD_FRIEND_MSG:
        {
            if (js["errno"].get<int>() == SUCCESS)
            {
                std::cout << "\n[系统] " << js["errmsg"].get<std::string>() << std::endl;
            }
            else
            {
                std::cout << "\n[系统] 添加好友失败: "
                          << js["errmsg"].get<std::string>() << std::endl;
            }
            break;
        }

        case ACCEPT_FRIEND_MSG:
        {
            if (js["errno"].get<int>() == SUCCESS)
            {
                std::cout << "\n[系统] " << js["errmsg"].get<std::string>() << std::endl;
            }
            else
            {
                std::cout << "\n[系统] 同意好友请求失败: "
                          << js["errmsg"].get<std::string>() << std::endl;
            }
            break;
        }

        case FRIEND_REQUEST_NOTICE:
        {
            std::string type = js["type"].get<std::string>();
            if (type == "accept")
            {
                std::cout << "\n[系统] 用户 " << js["fromname"].get<std::string>()
                          << " (id=" << js["fromid"].get<int>()
                          << ") 同意了你的好友申请" << std::endl;
            }
            else if (type == "reject")
            {
                std::cout << "\n[系统] 用户 " << js["fromname"].get<std::string>()
                          << " (id=" << js["fromid"].get<int>()
                          << ") 拒绝了你的好友申请" << std::endl;
            }
            else if (type == "request")
            {
                std::cout << "\n[系统] 用户 " << js["fromname"].get<std::string>()
                          << " (id=" << js["fromid"].get<int>()
                          << ") 请求添加你为好友，输入 'acceptfriend "
                          << js["fromid"].get<int>() << "' 同意" << std::endl;
            }
            else
            {
                std::cout << "\n[系统] 收到未知类型的好友通知: " << msg << std::endl;
            }
            break;
        }

        default:
            std::cout << "\n[系统] 收到未处理消息: " << msg << std::endl;
            break;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "\n[错误] 解析响应失败: " << e.what() << std::endl;
    }
}

// 主循环：显示菜单，读 stdin
/**
 * @brief 客户端输入处理主循环，运行在独立线程，负责读取控制台标准输入
 * @note 作用：和用户交互，根据是否登录展示不同菜单，组装对应JSON消息，调用sendJson发给服务端
 * @attention 注意：muduo的IO事件循环在另一个线程，这个函数单独跑输入逻辑，属于控制台交互层
 */
void ChatClient::handleInput()
{
    // 循环等待连接建立，100ms轮询一次
    while (!_conn)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 主循环，持续不停的读取用户控制台输入
    while (true)
    {
        // 未登录：显示登录/注册菜单
        if (!_loggedIn)
        {
            std::cout << "\n========== 菜单 ==========\n"
                      << "1. 登录\n"
                      << "2. 注册\n"
                      << "3. 退出\n"
                      << "请选择: ";

            int choice = 0;
            std::cin >> choice;

            // =========关键修复=========
            // 如果cin读取出错（输入字母等非数字），清除错误标志，清空缓冲区
            if (std::cin.fail())
            {
                std::cin.clear();                                                   // 清除failbit错误标记
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 丢弃整行垃圾输入
                continue;                                                           // 直接重来一轮菜单
            }

            // 忽略cin留下的换行符，防止getline读到空行
            // 从输入缓冲区丢弃最多1024个字符，直到遇到分隔符 `\n`，并且把分隔符也丢掉。
            std::cin.ignore(1024, '\n');

            if (choice == 1)
            {
                int id;
                std::cout << "请输入 id: ";
                std::cin >> id;
                std::cin.ignore(1024, '\n');
                std::string pwd = readFiltered("请输入密码（字母/数字/符号）: ", FILTER_PASSWORD);

                json js;
                js["msgid"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                sendJson(js);

                // 等待服务器响应，登录成功后 _loggedIn 会被置为 true，进入下一轮循环显示已登录菜单
                while (!_loggedIn)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            else if (choice == 2)
            {
                std::string name = readFiltered("请输入用户名（中文/字母/数字）: ", FILTER_USERNAME);
                std::string pwd = readFiltered("请输入密码（字母/数字/符号）: ", FILTER_PASSWORD);

                json js;
                js["msgid"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                sendJson(js);
            }

            else if (choice == 3)
            {
                std::cout << "再见" << std::endl;
                _loop->quit();
                return;
            }
        }
        // 已登录：读命令
        else
        {
            // 控制台提示符，打印 `>`，提示用户在这里输入聊天命令，类似 shell。
            std::cout << "> ";
            std::string line;
            // 读取一整行输入，直到回车，回车不会存入 line 字符串;不用cin是因为cin >> str：*遇到空格 / 换行就停止*，无法读取带空格的消息。
            std::getline(std::cin, line);

            // 如果用户直接敲回车，没有输入任何文字，直接continue跳过这次循环，不处理这条空命令
            if (line.empty())
                continue;

            if (line == "logout")
            {
                json js;
                js["msgid"] = LOGINOUT_MSG;
                js["id"] = _userId;
                sendJson(js);
                _loggedIn = false;
                _userId = -1;
                _username.clear();
                continue;
            }

            // 从下标0开始查找子串"chat "
            if (line.rfind("chat ", 0) == 0)
            {
                // 格式: chat <to> <message> -> chat 【用户id】 【消息内容（可带空格）】
                // istringstream把一个普通 C++ 字符串，包装成一个**输入流对象**
                std::istringstream iss(line);
                // cmd用来存读到的命令字符串
                std::string cmd;
                int toId;
                // 第一步：iss>>cmd , cmd截取iss里第一个不是空白分隔的字符，截取到遇到空白分隔符为止(分隔符不截走),iss也发生变化（被截取了）
                // 空白分隔符就是：空格" "、制表符"\t"、换行符"\n"
                // 第二步：iss>>toId , 截取后的iss再截取一次
                iss >> cmd >> toId;
                std::string msg;
                std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ')
                    msg.erase(0, 1);

                json js;
                js["msgid"] = ONE_CHAT_MSG;
                js["from"] = _userId;
                js["to"] = toId;
                js["message"] = msg;
                sendJson(js);
                continue;
            }
            if(line.rfind("addfriend ", 0) == 0)
            {
                std::istringstream iss(line);
                std::string cmd;
                int friendId;
                iss >> cmd >> friendId;

                json js;
                js["msgid"] = ADD_FRIEND_MSG;
                js["id"] = _userId;
                js["friendid"] = friendId;
                sendJson(js);
                continue;
            }
            if(line.rfind("acceptfriend ", 0) == 0)
            {
                std::istringstream iss(line);
                std::string cmd;
                int requesterId;
                iss >> cmd >> requesterId;

                json js;
                js["msgid"] = ACCEPT_FRIEND_MSG; 
                js["id"] = _userId;
                js["requesterid"] = requesterId;
                js["type"] = "accept";
                sendJson(js);
                continue;
            }
            if(line.rfind("rejectfriend ", 0) == 0)
            {
                std::istringstream iss(line);
                std::string cmd;
                int requesterId;
                iss >> cmd >> requesterId;

                json js;
                js["msgid"] = ACCEPT_FRIEND_MSG; 
                js["id"] = _userId;
                js["requesterid"] = requesterId;
                js["type"] = "reject";
                sendJson(js);
                continue;
            }

            std::cout << "未知命令，可用命令: chat <to> <msg> / addfriend <id> / acceptfriend <id> / logout" << std::endl;
        }
    }
}