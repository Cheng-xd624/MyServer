#pragma once

#include <vector>
#include "user.hpp"

// ============================================================
// FriendModel：好友业务操作类，也是单例
// 负责添加好友、查询好友列表等操作，供 ChatService 调用
// ============================================================

class FriendModel
{
public:
    // 单例
    static FriendModel* instance();
    
    FriendModel(const FriendModel&) = delete;
    FriendModel& operator=(const FriendModel&) = delete;

    // 添加好友:双向插入两条记录
    bool insert(int userId, int friendId);

    // 查询某个用户的所有好友列表
    std::vector<User> query(int userId);

private:
    FriendModel() = default;
};
