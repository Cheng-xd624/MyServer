#pragma once

#include "user.hpp"

// ============================================================
// UserModel：用户表的数据库操作
// 单例，供 ChatService 调用
// ============================================================
class UserModel
{
public:
    static UserModel* instance();

    UserModel(const UserModel&) = delete;
    UserModel& operator=(const UserModel&) = delete;

    // 插入新用户，成功时回填自增 id 到 user.id
    bool insert(User& user);

    // 按 id 查询用户，查不到返回 id = -1 的 User
    User query(int id);

    // 更新用户在线状态
    bool updateState(const User& user);

    // 服务器启动时，把所有用户状态重置为 offline
    void resetState();

private:
    UserModel() = default;
};