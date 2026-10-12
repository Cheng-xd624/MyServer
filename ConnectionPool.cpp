#include "ConnectionPool.hpp"
#include <muduo/base/Logging.h>
#include <chrono>

// 单例：C++11 静态局部变量，线程安全
ConnectionPool* ConnectionPool::instance()
{
    static ConnectionPool pool;
    return &pool;
}

// 构造：初始化连接池
ConnectionPool::ConnectionPool()
{
    if (!init())
    {
        LOG_ERROR << "ConnectionPool init failed!";
    }
}

// 析构：关闭所有连接
ConnectionPool::~ConnectionPool()
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    while (!_connectionQue.empty())
    {
        MYSQL* conn = _connectionQue.front();
        _connectionQue.pop();
        mysql_close(conn);
    }
}

// 预创建连接
bool ConnectionPool::init()
{
    for (size_t i = 0; i < _initSize; ++i)
    {
        MYSQL* conn = createConnection();
        if (conn)
        {
            _connectionQue.push(conn);
            ++_connectionCnt;
        }
    }
    return _connectionCnt > 0;
}

// 创建一条新连接
MYSQL* ConnectionPool::createConnection()
{
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return nullptr;

    if (!mysql_real_connect(conn,
                            _ip.c_str(),
                            _user.c_str(),
                            _password.c_str(),
                            _database.c_str(),
                            _port,
                            nullptr,
                            0))
    {
        LOG_ERROR << "MySQL connect failed: " << mysql_error(conn);
        mysql_close(conn);
        return nullptr;
    }

    // 设置字符集，防止中文乱码
    mysql_set_character_set(conn, "utf8mb4");
    return conn;
}

// 取连接
MYSQL* ConnectionPool::getConnection()
{
    // 锁是RAII的，离开作用域会自动释放
    std::unique_lock<std::mutex> lock(_queueMutex);

    // 等待：队列非空 或 还能新建连接
    // wait_for是条件变量_cv的API，
    // wait_for(锁，等待时长，谓词lambda)
    // 一直***阻塞***等待直到谓词返回结果为true -> 不等待，直接往下走
    // false -> 释放lock，阻塞当前线程，等待被notify唤醒或者时间到了自己醒来
    // 线程被唤醒后，会**重新获取锁，再次检查 lambda**（防止虚假唤醒 spurious wakeup）
    bool ok = _cv.wait_for(
        lock,
        std::chrono::milliseconds(_connectionTimeout),
        [this] {
            // 队列里面有空闲连接，可以直接拿；|| 总链接数还没到池子可以容纳的连接数上限
            return !_connectionQue.empty() || _connectionCnt < _maxSize;
        });

    if (!ok)
    {
        LOG_WARN << "getConnection timeout";
        return nullptr;
    }

    // 空闲连接队列为空（所有建好的连接都在被使用），
    // 但是当前总连接数（连接池还能容纳连接）还没到上限，就新建一个连接加入到连接池里并把它拿走使用。
    if (_connectionQue.empty() && _connectionCnt < _maxSize)
    {
        MYSQL* conn = createConnection();
        if (conn) ++_connectionCnt;
        return conn;
    }

    // 从队列取一条
    MYSQL* conn = _connectionQue.front();
    _connectionQue.pop();
    return conn;
}

// 归还连接
void ConnectionPool::releaseConnection(MYSQL* conn)
{
    if (!conn) return;

    std::lock_guard<std::mutex> lock(_queueMutex);
    _connectionQue.push(conn);
    // _cv.notify_one():去看看**有没有线程卡在`getConnection`的`wait_for`那里睡着**
    // 如果有等待线程 → 唤醒其中一个；
    _cv.notify_one();
}