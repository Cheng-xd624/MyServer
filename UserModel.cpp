#include "UserModel.hpp"
#include "ConnectionPool.hpp"
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>
#include <cstdio>
#include <cstdlib>

UserModel* UserModel::instance()
{
    static UserModel model;
    return &model;
}

// 插入新用户
bool UserModel::insert(User& user)
{
    char sql[512];
    // 把 `user.name`、`password`、`state` 填到 `INSERT ... VALUES('%s','%s','%s')` 的占位符`%s`。
    snprintf(sql, sizeof(sql),
             "INSERT INTO user(name, password, state) VALUES('%s','%s','%s')",
             user.name.c_str(), user.password.c_str(), user.state.c_str());

    // 第一次调用 instance ()：创建 pool 对象，返回它的地址。
    // **之后不管调用多少次 instance ()，都直接返回同一个 pool 对象的指针，不会再次构造！**
    ConnectionRAII raii(ConnectionPool::instance());
    if (!raii.valid()) return false;

    MYSQL* conn = raii.get();
    // 将sql语句通过conn（数据库连接句柄）执行给数据库
    if (mysql_query(conn, sql) != 0)
    {
        LOG_ERROR << "insert failed: " << mysql_error(conn);
        return false;
    }

    // 回填自增 id
    // mysql_insert_id()返回的是uint64_t，强制转换成int
    // C++ 推荐优先用 static_cast，而不是 C 风格强制转换`(int)xxx`，可读性更好、转换类型更严格。
    user.id = static_cast<int>(mysql_insert_id(conn)); //函数将连接conn插入到数据库后，数据库给这个连接分配一个id，再赋值回user.id
    return true;
}

// 按 id 查询
User UserModel::query(int id)
{
    User user;   // 默认 id = -1

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT id, name, password, state FROM user WHERE id=%d", id);

    ConnectionRAII raii(ConnectionPool::instance());
    if (!raii.valid()) return user;

    MYSQL* conn = raii.get();
    if (mysql_query(conn, sql) != 0)
    {
        LOG_ERROR << "query failed: " << mysql_error(conn);
        return user;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return user;

    // `MYSQL_ROW`本质是`char**`字符串数组
    // `mysql_fetch_row`：**从 res 结果集中取出一行记录**。
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row)
    {
        user.id       = std::atoi(row[0]);
        user.name     = row[1];
        user.password = row[2];
        user.state    = row[3];
    }
    // 释放res
    mysql_free_result(res);
    return user;
}

// 更新状态
bool UserModel::updateState(const User& user)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE user SET state='%s' WHERE id=%d",
             user.state.c_str(), user.id);

    ConnectionRAII raii(ConnectionPool::instance());
    if (!raii.valid()) return false;

    MYSQL* conn = raii.get();
    return mysql_query(conn, sql) == 0;
}

// 重置所有用户状态
void UserModel::resetState()
{
    ConnectionRAII raii(ConnectionPool::instance());
    if (!raii.valid()) return;

    mysql_query(raii.get(), "UPDATE user SET state='offline'");
}