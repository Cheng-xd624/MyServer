#pragma once

#include <string>

// ============================================================
// 用户信息结构体
// 对应 MySQL 里 chat.user 表的一行
// ============================================================
struct User
{
    int         id = -1;      // 用户 id，-1 表示无效用户
    std::string name;         // 用户名
    std::string password;     // 密码
    std::string state;        // 状态：online / offline
};