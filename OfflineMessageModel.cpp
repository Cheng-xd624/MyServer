#include "OfflineMessageModel.hpp"
#include "ConnectionPool.hpp"
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>
#include <cstdio>

OfflineMessageModel* OfflineMessageModel::instance() {
    static OfflineMessageModel instance;
    return &instance;
}

// 插入离线消息
bool OfflineMessageModel::insert(int userId, const std::string& message)
{
    // 使用RAII管理数据库连接，确保在函数结束时自动释放连接
    ConnectionRAII raii(ConnectionPool::instance());
    if(!raii.valid()) {
        LOG_ERROR << "连接数据库失败";
        return false;
    }

    MYSQL* conn = raii.get();

    // 构建SQL语句
    char escaped[8192];
    // mysql_real_escape_string将消息内容escaped进行一次转义，防止单引号等特殊字符破坏SQL语句结构
    // message.c_str()是要插入的消息内容，message.length()是消息内容的长度
    unsigned long escapedLength = mysql_real_escape_string(
        conn, escaped, message.c_str(), message.length());
    escaped[escapedLength] = '\0'; // 确保字符串以null结尾,这个位置本来就是无效的，不会毁坏最后一个字节的内容
    char sql[16384];
    snprintf(sql, sizeof(sql),
            "INSERT INTO offline_messages (userid, message) VALUES (%d, '%s')", userId, escaped);

    // 执行SQL语句
    if (mysql_query(conn, sql)) {
        LOG_ERROR << "Failed to insert offline message: " << mysql_error(conn);
        return false;
    }

    return true;
}

// 查询离线消息
std::vector<std::string> OfflineMessageModel::query(int userId)
{
    std::vector<std::string> messages;

    char sql[256];
    snprintf(sql, sizeof(sql), 
        "SELECT message FROM offline_messages WHERE userid = %d ORDER BY id ASC", userId);
    
    ConnectionRAII raii(ConnectionPool::instance());
    if(!raii.valid()) {
        LOG_ERROR << "连接数据库失败";
        return messages;
    }

    MYSQL* conn = raii.get();
    if(mysql_query(conn, sql)) {
        LOG_ERROR << "查询没连接上数据库，离线查询失败: " << mysql_error(conn);
        return messages;
    }

    // 获取查询结果
    MYSQL_RES* res = mysql_store_result(conn);
    if(res == nullptr) {
        LOG_ERROR << "查询离线消息失败: " << mysql_error(conn);
        return messages;
    }

    MYSQL_ROW row;
    while((row = mysql_fetch_row(res)) != nullptr) {
        if(row[0]) {
            // emplace_back直接在容器末尾构造一个新的元素，避免了不必要的拷贝，提高性能
            messages.emplace_back(row[0]);
        }
    }
    
    // 释放结果集资源
    mysql_free_result(res);
    return messages;
};
    
// 删除离线消息
bool OfflineMessageModel::remove(int userId)
{
    char sql[256];
    snprintf(sql, sizeof(sql), 
        "DELETE FROM offline_messages WHERE userid = %d", userId);
    
    ConnectionRAII raii(ConnectionPool::instance());
    if(!raii.valid()) {
        LOG_ERROR << "连接数据库失败";
        return false;
    }

    MYSQL* conn = raii.get();
    if(mysql_query(conn, sql)) {
        LOG_ERROR << "删除离线消息失败: " << mysql_error(conn);
        return false;
    }

    return true;
}