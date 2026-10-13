#include "FriendModel.hpp"
#include "ConnectionPool.hpp"
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>
#include <cstdio>
#include <cstdlib>

FriendModel* FriendModel::instance()
{
    static FriendModel model;
    return &model;
}

// 添加好友:双向插入两条记录
bool FriendModel::insert(int userId, int friendId)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "INSERT  IGNORE INTO friend(userId, friendId) VALUES(%d,%d),(%d,%d)", userId, friendId, friendId, userId);

    ConnectionRAII raii(ConnectionPool::instance());
    if(raii.valid() == false)
    {
        LOG_ERROR << "获取数据库连接失败";
        return false;
    }

    // 2.获取数据库连接
    MYSQL* conn = raii.get();
    if (mysql_query(conn, sql) != 0)
    {
        LOG_ERROR << "添加好友失败: " << mysql_error(conn);
        return false;
    }
    return true;
}

// 查询某个用户的所有好友列表
std::vector<User> FriendModel::query(int userId)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT a.id, a.name, a.state FROM user AS a INNER JOIN friend AS b ON a.id = b.friendId WHERE b.userId = %d", userId);

    ConnectionRAII raii(ConnectionPool::instance());
    if(raii.valid() == false)
    {
        LOG_ERROR << "获取数据库连接失败";
        return {};
    }

    // 2.获取数据库连接
    MYSQL* conn = raii.get();
    if (mysql_query(conn, sql) != 0)
    {
        LOG_ERROR << "查询好友列表失败: " << mysql_error(conn);
        return {};
    }

    // 3.获取结果集
    // mysql_store_result()函数用于检索查询的结果集。它将结果集存储在客户端内存中，
    // 并返回一个指向MYSQL_RES结构的指针，该结构包含了查询结果的所有行和列信息。
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr)
    {
        LOG_ERROR << "查询好友列表失败: " << mysql_error(conn);
        return {};
    }

    // 4.遍历结果集，封装好友列表
    std::vector<User> friends;
    // MYSQL_ROW是一个指向字符串数组的指针，每个字符串表示结果集中的一行数据。
    // mysql_fetch_row()函数用于从结果集中获取下一行数据，并返回一个MYSQL_ROW类型的指针。如果没有更多的行可供检索，它将返回nullptr。
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        User user;
        // 将查询结果的每一行数据封装到User对象中
        // atoi()函数用于将字符串转换为整数。它将字符串表示的数字转换为对应的整数值。
        user.id = std::atoi(row[0]);
        user.name = row[1];
        user.state = row[2];
        friends.push_back(user);
    }

    // 4.释放结果集
    mysql_free_result(res);

    return friends;
}