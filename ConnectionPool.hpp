#pragma once

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

// ============================================================
// 数据库连接池（单例）
// 作用：预先创建若干 MySQL 连接，业务层用时取、用完还，
//       避免每次请求都 mysql_real_connect / mysql_close。
// ============================================================
class ConnectionPool
{
public:
    // 获取单例
    static ConnectionPool* instance();

    // 禁止拷贝
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // 从池中取一条连接，超时返回 nullptr
    MYSQL* getConnection();

    // 归还连接
    void releaseConnection(MYSQL* conn);

    // 析构：关闭所有连接
    ~ConnectionPool();

private:
    ConnectionPool();   // 构造时初始化连接池

    // 创建一条新连接
    MYSQL* createConnection();

    // 预创建 _initSize 条连接
    bool init();

    // 空闲连接队列
    std::queue<MYSQL*> _connectionQue;

    // 保护队列的互斥锁
    std::mutex _queueMutex;

    // 条件变量：队列空时等待，归还时唤醒
    std::condition_variable _cv;

    // 数据库配置
    std::string    _ip       = "127.0.0.1";
    unsigned short _port     = 3306;
    std::string    _user     = "root";
    std::string    _password = "zjcsql";   // ← 改成你的 MySQL 密码
    std::string    _database = "chat";

    // 连接池参数
    size_t _initSize          = 5;     // 初始连接数
    size_t _maxSize           = 20;    // 最大连接数
    int    _connectionTimeout = 3000;  // 获取连接超时（毫秒）

    // 当前已创建的连接总数
    size_t _connectionCnt = 0;
};

// ============================================================
// RAII 包装：离开作用域自动归还连接
// ============================================================
class ConnectionRAII
{
public:
    // explicit可以阻止隐式转换
    // 一般建议：
    // 单参数构造函数默认加 explicit，除非你确实希望它支持隐式转换。
    // 资源管理类、数值包装类、指针包装类、状态类，通常都应该加 explicit。
    explicit ConnectionRAII(ConnectionPool* pool)
        : _pool(pool), _conn(pool->getConnection()) {}

    ~ConnectionRAII()
    {
        if (_conn) _pool->releaseConnection(_conn);
    }

    MYSQL*  get()   const { return _conn; }
    // 判断拿到的连接是不是有效
    bool    valid() const { return _conn != nullptr; }

private:
    ConnectionPool* _pool;
    MYSQL*          _conn;
};